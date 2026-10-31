/* SPDX-License-Identifier: GPL-2.0-only
 * Off-mode charging UI for the Galaxy J7 Prime (Exynos 7870).
 * Reads the S2MU005 fuel gauge and charger status; charger/PMIC configuration is untouched.
 */
#include <stdint.h>
#include "charging.h"
#include "../fdt.h"
#include "display_console.h"

#define FG_I2C_BASE    0x13830000UL /* Exynos I2C_FUELGAUGE, from on7xelte DTS */
#define CHG_I2C_BASE   0x13870000UL /* S2MU005 charger bus, from on7xelte DTS */
#define I2C_CON        0x00
#define I2C_STAT       0x04
#define I2C_DS         0x0c
#define I2C_CON_ACK    (1u << 7)
#define I2C_CON_DIV512 (1u << 6)
#define I2C_CON_PEND   (1u << 4)
#define I2C_STAT_START (1u << 5)
#define I2C_STAT_TXEN  (1u << 4)
#define I2C_STAT_MTX   (3u << 6)
#define I2C_STAT_MRX   (2u << 6)
#define I2C_STAT_NACK  (1u << 0)
#define FG_ADDR        0x3b
#define CHG_ADDR       0x3d
#define CHG_STATUS0    0x08
#define CHG_VBUS_OK    (1u << 7)
#define J7_FORCE_CHARGING_UI 0
#define FG_RVBAT       0x04
#define FG_RCUR_CC     0x06
#define FG_RSOC        0x08
#define GA0_DATA       0x139f0044UL /* pinctrl 0 + GPA0 bank + DAT register */
#define CNT_FALLBACK   26000000ULL

static uint64_t ticks(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntpct_el0" : "=r" (v));
	return v;
}

static uint64_t frequency(void)
{
	uint64_t v;
	__asm__ volatile("mrs %0, cntfrq_el0" : "=r" (v));
	return v ? v : CNT_FALLBACK;
}

static void wait_ms(unsigned ms)
{
	uint64_t start = ticks();
	uint64_t delta = (frequency() / 1000) * ms;
	while (ticks() - start < delta)
		__asm__ volatile("yield");
}

#define HSI2C_CTL          0x00
#define HSI2C_FIFO_CTL     0x04
#define HSI2C_INT_STATUS   0x24
#define HSI2C_FIFO_STATUS  0x30
#define HSI2C_TX_DATA      0x34
#define HSI2C_RX_DATA      0x38
#define HSI2C_AUTO_CONF    0x44
#define HSI2C_ADDR         0x70
#define HSI2C_MASTER       (1u << 3)
#define HSI2C_RXCHON       (1u << 6)
#define HSI2C_TXCHON       (1u << 7)
#define HSI2C_FIFO_ENABLE  ((1u << 0) | (1u << 1) | (0x8u << 4) | (0x8u << 16))
#define HSI2C_INT_DONE     (1u << 7)
#define HSI2C_INT_ERR      (0xfu << 8)
#define HSI2C_RX_EMPTY     (1u << 24)
#define HSI2C_TX_FULL      (1u << 7)
#define HSI2C_READ_WRITE   (1u << 16)
#define HSI2C_STOP_AFTER   (1u << 17)
#define HSI2C_MASTER_RUN   (1u << 31)

static int i2c_wait(uintptr_t base, unsigned off, uint32_t mask, int want_set)
{
	for (unsigned i = 0; i < 20000; ++i) {
		uint32_t v = *(volatile uint32_t *)(base + off);
		if (want_set ? (v & mask) : !(v & mask)) return 1;
		__asm__ volatile("yield");
	}
	return 0;
}

static int hsi2c_xfer(uintptr_t base, uint8_t addr, uint8_t *buf, unsigned len, int is_read, int stop)
{
	volatile uint32_t *r = (volatile uint32_t *)base;
	uint32_t ctl = r[HSI2C_CTL / 4];
	r[HSI2C_INT_STATUS / 4] = HSI2C_INT_DONE | HSI2C_INT_ERR;
	r[HSI2C_FIFO_CTL / 4] = HSI2C_FIFO_ENABLE;
	ctl &= ~(HSI2C_RXCHON | HSI2C_TXCHON);
	ctl |= HSI2C_MASTER | (is_read ? HSI2C_RXCHON : HSI2C_TXCHON);
	uint32_t a = r[HSI2C_ADDR / 4];
	a &= ~((0x3ffu << 10) | 0x3ffu | (0xffu << 24));
	a |= (uint32_t)(addr & 0x7f) << 10;
	r[HSI2C_ADDR / 4] = a;
	r[HSI2C_CTL / 4] = ctl;

	uint32_t ac = r[HSI2C_AUTO_CONF / 4];
	ac &= ~0xffffu;
	ac |= len & 0xffffu;
	ac = is_read ? (ac | HSI2C_READ_WRITE) : (ac & ~HSI2C_READ_WRITE);
	ac = stop ? (ac | HSI2C_STOP_AFTER) : (ac & ~HSI2C_STOP_AFTER);
	r[HSI2C_AUTO_CONF / 4] = ac;

	if (!is_read) {
		for (unsigned i = 0; i < len; ++i) {
			if (!i2c_wait(base, HSI2C_FIFO_STATUS, HSI2C_TX_FULL, 0)) return 0;
			r[HSI2C_TX_DATA / 4] = buf[i];
		}
		r[HSI2C_AUTO_CONF / 4] |= HSI2C_MASTER_RUN;
	} else {
		r[HSI2C_AUTO_CONF / 4] |= HSI2C_MASTER_RUN;
		for (unsigned i = 0; i < len; ++i) {
			if (!i2c_wait(base, HSI2C_FIFO_STATUS, HSI2C_RX_EMPTY, 0)) return 0;
			buf[i] = (uint8_t)(r[HSI2C_RX_DATA / 4] & 0xff);
		}
	}
	if (!i2c_wait(base, HSI2C_INT_STATUS, HSI2C_INT_DONE, 1)) return 0;
	if (r[HSI2C_INT_STATUS / 4] & HSI2C_INT_ERR) return 0;
	r[HSI2C_INT_STATUS / 4] = HSI2C_INT_DONE;
	return 1;
}

static int read_i2c_byte(uintptr_t base, uint8_t address, uint8_t reg, uint8_t *out)
{
	if (!hsi2c_xfer(base, address, &reg, 1, 0, 0)) return 0;
	return hsi2c_xfer(base, address, out, 1, 1, 1);
}

static int read_fg_word(uint8_t reg, uint16_t *out)
{
	uint8_t lo, hi;
	if (!read_i2c_byte(FG_I2C_BASE, FG_ADDR, reg, &lo) ||
	    !read_i2c_byte(FG_I2C_BASE, FG_ADDR, (uint8_t)(reg + 1), &hi))
		return 0;
	*out = (uint16_t)lo | ((uint16_t)hi << 8);
	return 1;
}

int j7_battery_sample(int *percent, int *power_mw)
{
	uint16_t raw_soc, raw_vbat, raw_current;
	/* Configure only the controller; never write to the PMIC/fuel gauge. */
	if (!read_fg_word(FG_RSOC, &raw_soc) ||
	    !read_fg_word(FG_RVBAT, &raw_vbat) ||
	    !read_fg_word(FG_RCUR_CC, &raw_current))
		return 0;
	if ((raw_soc & 0x8000) || raw_vbat == 0)
		return 0;
	int soc = (raw_soc & 0x7fff) * 100 / 16384;
	int mv = ((uint32_t)raw_vbat * 1000) >> 13;
	if (soc > 100 || mv < 2500 || mv > 5000)
		return 0;
	int ma;
	if (raw_current & 0x8000) {
		uint32_t magnitude = ((~raw_current & 0xffff) + 1) * 1000U >> 12;
		ma = (int)magnitude;
	} else {
		ma = -(int)(((uint32_t)raw_current * 1000U) >> 12);
	}
	*percent = soc;
	/* Battery-side power estimate, not USB adapter input power. */
	*power_mw = mv * ma / 1000;
	if (*power_mw < 0) *power_mw = 0;
	return 1;
}

static int charger_bootargs(uintptr_t fdt)
{
	char args[1024];
	if (!fdt_chosen_string(fdt, "bootargs", args, sizeof(args)))
		return 0;
	for (const char *p = args; *p;) {
		while (*p == ' ' || *p == '\t') ++p;
		const char *start = p;
		while (*p && *p != ' ' && *p != '\t') ++p;
		static const char key[] = "androidboot.mode=";
		unsigned k = 0;
		while (k < sizeof(key) - 1 && start + k < p && start[k] == key[k]) ++k;
		if (k == sizeof(key) - 1) {
			const char *v = start + k;
			unsigned n = (unsigned)(p - v);
			if ((n == 7 && v[0]=='c' && v[1]=='h' && v[2]=='a' && v[3]=='r' && v[4]=='g' && v[5]=='e' && v[6]=='r') ||
			    (n == 6 && v[0]=='c' && v[1]=='h' && v[2]=='a' && v[3]=='r' && v[4]=='g' && v[5]=='e') ||
			    (n == 8 && v[0]=='c' && v[1]=='h' && v[2]=='a' && v[3]=='r' && v[4]=='g' && v[5]=='i' && v[6]=='n' && v[7]=='g'))
				return 1;
		}
	}
	return 0;
}

/* S2MU005 CHG_STATUS0 bit 7 is VBUS_OK (Linux on7xelte charger driver). */
static int charger_vbus_present(void)
{
	uint8_t status = 0;
	return read_i2c_byte(CHG_I2C_BASE, CHG_ADDR, CHG_STATUS0, &status) &&
	       (status & CHG_VBUS_OK) != 0;
}

void j7_charging_screen(uintptr_t fdt)
{
	/* Neither S-Boot's bootargs nor the PMIC VBUS bit has triggered on hardware yet.
	 * Keep the UI visible in this diagnostic image; Power held for 2 seconds exits. */
	int charger_detected = charger_bootargs(fdt) || charger_vbus_present();
#if !J7_FORCE_CHARGING_UI
	if (!charger_detected)
		return;
#endif
	int percent = -1, power_mw = -1;
	int valid = j7_battery_sample(&percent, &power_mw);
	j7_display_charging(valid ? percent : -1, valid && power_mw >= 0 ? power_mw : -1, charger_detected);
	uint64_t pressed_at = 0;
	for (;;) {
		/* Power key is GPA0-0, active-low per on7xelte GPIO-keys DTS. */
		if (((*(volatile uint32_t *)GA0_DATA) & 1u) == 0) {
			if (!pressed_at) pressed_at = ticks();
			if (ticks() - pressed_at >= frequency() * 2 / 1)
				return; /* Held for two seconds: continue into XNU. */
		} else {
			pressed_at = 0;
		}
		wait_ms(1000);
		valid = j7_battery_sample(&percent, &power_mw);
		j7_display_charging(valid ? percent : -1, valid && power_mw >= 0 ? power_mw : -1, charger_detected);
	}
}

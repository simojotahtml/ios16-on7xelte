/* SPDX-License-Identifier: GPL-2.0-only
 * Galaxy J7 Prime early screen. S-Boot leaves a continuously scanned framebuffer at 0x67000000
 * (1080x1920, a8r8g8b8, 4320-byte rows) that CPU writes reach directly (verified with probe/),
 * so the A5's DECON handoff is not needed. Runs with the MMU off: aligned 32-bit stores only.
 */
#include <stdint.h>
#include "display_console.h"
#include "font.h"

#define FB_BASE 0x67000000UL
#define FB_W 1080U
#define FB_H 1920U
#define SCALE 4U
#define CELL_W ((5U + 1U) * SCALE)
#define CELL_H ((7U + 2U) * SCALE)
#define WHITE  0xffffffffu
#define YELLOW 0xffffff00u
#define BLACK  0xff000000u

static void glyph(char c, unsigned x, unsigned y, uint32_t fg)
{
    volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
	static const unsigned char percent_rows[7] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
    const unsigned char *rows = font_rows[0];
	if (c >= 'a' && c <= 'z') {
        c = (char)(c - 32);
	}
	if (c == '%') rows = percent_rows;
	else
		for (int i = 0; i < FONT_COUNT; ++i)
			if (font_chars[i] == c) {
				rows = font_rows[i];
				break;
			}
    for (unsigned r = 0; r < 7; ++r)
        for (unsigned b = 0; b < 5; ++b) {
            uint32_t col = ((rows[r] >> (4 - b)) & 1) ? fg : BLACK;
            for (unsigned dy = 0; dy < SCALE; ++dy)
                for (unsigned dx = 0; dx < SCALE; ++dx)
                    fb[(y + r * SCALE + dy) * FB_W + x + b * SCALE + dx] = col;
        }
}

static void line(unsigned row, const char *s, uint32_t fg)
{
    for (unsigned col = 0; s[col]; ++col)
        glyph(s[col], col * CELL_W, row * CELL_H, fg);
}

static void centered_line(unsigned row, const char *s, uint32_t fg)
{
	unsigned n = 0;
	while (s[n]) n++;
	unsigned x = n * CELL_W < FB_W ? (FB_W - n * CELL_W) / 2 : 0;
	for (unsigned i = 0; i < n; ++i)
		glyph(s[i], x + i * CELL_W, row * CELL_H, fg);
}

static void rect(unsigned x, unsigned y, unsigned w, unsigned h, uint32_t color)
{
	volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
	if (x + w > FB_W || y + h > FB_H) return;
	for (unsigned yy = y; yy < y + h; ++yy)
		for (unsigned xx = x; xx < x + w; ++xx)
			fb[yy * FB_W + xx] = color;
}

void j7_display_charging(int percent, int power_mw, int charger_detected)
{
	static int first = 1;
	char text[32];
	volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
	if (first) {
		for (unsigned i = 0; i < FB_W * FB_H; ++i) fb[i] = BLACK;
		first = 0;
		centered_line(17, charger_detected ? "iOS CHARGING" : "iOS BATTERY", WHITE);
		/* Simple battery outline, terminal, and lightning mark. */
		rect(424, 720, 232, 410, WHITE);
		rect(434, 730, 212, 390, BLACK);
		rect(494, 700, 92, 20, WHITE);
	}
	if (percent >= 0 && percent <= 100) {
		unsigned fill = (unsigned)percent * 390 / 100;
		rect(434, 730, 212, 390, BLACK);
		rect(444, 1120 - fill, 192, fill, 0xff36d66bU);
		unsigned n = 0, value = (unsigned)percent;
		char digits[3];
		do { digits[n++] = (char)('0' + value % 10); value /= 10; } while (value && n < 3);
		rect(0, 1160, FB_W, CELL_H, BLACK);
		unsigned x = (FB_W - (n + 1) * CELL_W) / 2;
		while (n) glyph(digits[--n], x, 1160, WHITE), x += CELL_W;
		glyph('%', x, 1160, WHITE);
	} else {
		rect(0, 34 * CELL_H, FB_W, CELL_H, BLACK);
		centered_line(34, "BATTERY --%", WHITE);
	}
	if (power_mw >= 0) {
		unsigned tenths = (unsigned)power_mw / 100;
		unsigned whole = tenths / 10, n = 0, p = 0;
		char reverse[8];
		do { reverse[n++] = (char)('0' + whole % 10); whole /= 10; } while (whole && n < sizeof(reverse));
		text[p++] = 'P'; text[p++] = 'O'; text[p++] = 'W'; text[p++] = 'E'; text[p++] = 'R'; text[p++] = ' ';
		while (n) text[p++] = reverse[--n];
		text[p++] = '.'; text[p++] = (char)('0' + tenths % 10); text[p++] = 'W'; text[p] = '\0';
		rect(0, 39 * CELL_H, FB_W, CELL_H, BLACK);
		centered_line(39, text, WHITE);
	} else {
		rect(0, 39 * CELL_H, FB_W, CELL_H, BLACK);
		centered_line(39, "POWER --.-W", WHITE);
	}
	rect(0, 47 * CELL_H, FB_W, CELL_H, BLACK);
	centered_line(47, "HOLD POWER TO BOOT", 0xffb8b8b8U);
}

int a5_display_console_start(volatile uint64_t *log)
{
    volatile uint32_t *fb = (volatile uint32_t *)FB_BASE;
    for (unsigned i = 0; i < FB_W * FB_H; ++i)
        fb[i] = BLACK;
    line(0, "J7 PRIME XNU LOADER", YELLOW);
    line(2, "PAYLOAD READY", WHITE);
    line(3, "STARTING XNU", WHITE);
    log[77] = 0;
    return 0;
}

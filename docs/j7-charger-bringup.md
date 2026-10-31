# J7 Prime S2MU005 charger bring-up (Stage 1 read-only probe)

## Build
Apply `xnu/patches/0321-j7prime-charger-probe.patch` on top of the J7 patch stack
(0300-0319). Build via GitHub Actions xnu workflow (MACHINE_CONFIG default SWEET
but J7 uses the same bsd/conf/files). Boot arg: `j7hw=1` (charger probe auto-starts;
disable with `j7charger=0`).

## Log lines to look for (persistent log ring)
- `J7 CHG: probe start (read-only, Stage 1)`
- `J7 CHG: t=<n> STATUS0=.. STATUS1=.. STATUS2=.. STATUS3=.. CTRL0=.. CTRL12=.. CTRL13=.. REG2A=.. FG_RVBAT=<mV*10? raw> FG_RSOC=<raw>`
- `J7 CHG: probe done`

Interpretation:
- CTRL12 bits[1:0] == 2 => watchdog enabled (S-Boot started it).
- If STATUS0 indicates charging then later stops ~80s after boot => watchdog timeout hypothesis.
- FG_RVBAT (reg 0x04, 2 bytes) ~ battery mV*10; FG_RSOC (reg 0x08) ~ percent*? (raw).

## Unverifiable from source in this environment
- CMU_PERI base for the PMIC bus and its clock gate bit are not confirmed (0x13870000
  is the HSI2C controller, not CMU). The probe assumes S-Boot left the bus clocked.
- Pinmux for GPD3 SDA/SCL on the PMIC bus is not configured by the probe (assumes
  S-Boot set function 2 with pull-ups), unlike the touch bus which is configured.
- HSI2C read sequence is simplified; the real Exynos HSI2C protocol (master run,
  i2c_con, status polling) must be ported from i2c-exynos5.c before reads are reliable.
- Fuel gauge voltage/SOC scaling factors not applied (raw register words logged).
- Monotonic timestamp is a placeholder (0); should use mach_absolute_time().
- Stage 2 writes are NOT enabled in 0322 (read-only rehearsal); enable after review.

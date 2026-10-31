/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once

#include <stdint.h>

/* Read-only S2MU005 fuel-gauge sample. Returns 1 on a valid sample. */
int j7_battery_sample(int *percent, int *power_mw);
void j7_charging_screen(uintptr_t fdt);

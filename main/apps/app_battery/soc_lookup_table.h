/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

/* Battery voltage to SOC lookup table, 3100~4200mV, 4mV per bin.
 *
 *   uint8_t pct = bat_soc_from_mv(voltage_mv);   // 3300mV -> 0%, 4164mV -> 100%
 */
#pragma once

#include <stdint.h>

#define BAT_SOC_MV_MIN  3100
#define BAT_SOC_MV_MAX  4200
#define BAT_SOC_MV_STEP 4
#define BAT_SOC_COUNT   276

static const uint8_t bat_soc_table[BAT_SOC_COUNT] = {
    0,  0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
    0,  0,  0,  0,  0,  0,   0,   0,   0,   0,   0,   0,   0,   1,   1,   1,   1,  1,  1,  1,  1,  1,  1,  1,  1,  1,
    1,  1,  1,  1,  1,  1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,  2,  2,  2,  2,  2,  3,  3,  3,  3,
    3,  3,  3,  4,  4,  4,   4,   4,   5,   5,   6,   6,   7,   8,   10,  12,  12, 13, 13, 14, 15, 15, 16, 17, 17, 18,
    19, 20, 21, 23, 24, 25,  26,  28,  29,  31,  33,  35,  37,  39,  40,  42,  43, 45, 46, 48, 49, 51, 52, 53, 54, 55,
    56, 57, 57, 58, 58, 59,  60,  60,  61,  61,  62,  63,  63,  64,  64,  65,  66, 66, 67, 68, 68, 69, 70, 70, 71, 71,
    72, 73, 73, 74, 75, 75,  76,  76,  77,  78,  78,  79,  79,  80,  80,  81,  82, 82, 83, 83, 84, 84, 84, 85, 85, 86,
    86, 87, 87, 88, 88, 89,  89,  90,  90,  91,  91,  92,  92,  93,  93,  93,  94, 94, 95, 95, 96, 96, 96, 97, 97, 98,
    98, 98, 99, 99, 99, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100, 100,
};

static inline uint8_t bat_soc_from_mv(int mv)
{
    int i = (mv - BAT_SOC_MV_MIN + BAT_SOC_MV_STEP / 2) / BAT_SOC_MV_STEP;
    if (i < 0) i = 0;
    if (i >= BAT_SOC_COUNT) i = BAT_SOC_COUNT - 1;
    return bat_soc_table[i];
}

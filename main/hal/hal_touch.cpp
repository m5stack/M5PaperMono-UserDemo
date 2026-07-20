/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <M5Unified.h>

/* -------------------------------------------------------------------------- */
/*                                    Touch                                   */
/* -------------------------------------------------------------------------- */
bool Hal::isTouchPressed()
{
    return M5.Touch.getCount() > 0;
}

const m5::Touch_Class::touch_detail_t& Hal::getTouchDetail()
{
    return M5.Touch.getDetail();
}

bool Hal::wasTouchClickedArea(int x, int y, int w, int h)
{
    if (isTouchPressed()) {
        const auto& touch = getTouchDetail();
        if (touch.wasPressed()) {
            const auto& raw = M5.Touch.getTouchPointRaw();
            return raw.x >= x && raw.x <= x + w && raw.y >= y && raw.y <= y + h;
        }
    }
    return false;
}

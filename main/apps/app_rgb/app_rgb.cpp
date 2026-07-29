/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <cstdio>
#include <hal.h>

static constexpr std::uint32_t RGB_TOUCH_COOLDOWN_MS = 250;
static constexpr std::uint8_t RGB_RAM_INDEX          = 3;
static constexpr std::uint8_t RGB_RAM_TAG            = 0xE0;
static constexpr std::uint8_t RGB_RAM_TAG_MASK       = 0xF0;
static constexpr std::uint8_t RGB_RAM_RED_BIT        = 0x01;
static constexpr std::uint8_t RGB_RAM_GREEN_BIT      = 0x02;
static constexpr std::uint8_t RGB_RAM_BLUE_BIT       = 0x04;

static constexpr int RGB_VALUE_X = 43;
static constexpr int RGB_VALUE_W = 86 - 43;
static constexpr int RGB_ROW_H   = 20;
static constexpr int RGB_R_Y     = 290;
static constexpr int RGB_G_Y     = 322;
static constexpr int RGB_B_Y     = 354;

static constexpr int RGB_TOUCH_X   = 0;
static constexpr int RGB_TOUCH_W   = 96;
static constexpr int RGB_TOUCH_R_Y = 286;
static constexpr int RGB_TOUCH_G_Y = 318;
static constexpr int RGB_TOUCH_B_Y = 350;
static constexpr int RGB_TOUCH_H   = 28;

static constexpr BuzzerTone RGB_SWITCH_ON_TONES[] = {
    {.start_freq_hz = 1800, .end_freq_hz = 1800, .duration_ms = 8, .gap_ms = 5},
    {.start_freq_hz = 2400, .end_freq_hz = 2400, .duration_ms = 10, .gap_ms = 0},
};

static constexpr BuzzerTone RGB_SWITCH_OFF_TONES[] = {
    {.start_freq_hz = 1500, .end_freq_hz = 1500, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 900, .end_freq_hz = 900, .duration_ms = 12, .gap_ms = 0},
};

static void loadRgbSettings(bool& red_on, bool& green_on, bool& blue_on)
{
    std::uint8_t value = 0;
    if (GetHAL().rx8130RamRead(RGB_RAM_INDEX, &value) && (value & RGB_RAM_TAG_MASK) == RGB_RAM_TAG) {
        red_on   = (value & RGB_RAM_RED_BIT) != 0;
        green_on = (value & RGB_RAM_GREEN_BIT) != 0;
        blue_on  = (value & RGB_RAM_BLUE_BIT) != 0;
    }
}

static void saveRgbSettings(bool red_on, bool green_on, bool blue_on)
{
    std::uint8_t value = RGB_RAM_TAG;
    if (red_on) {
        value |= RGB_RAM_RED_BIT;
    }
    if (green_on) {
        value |= RGB_RAM_GREEN_BIT;
    }
    if (blue_on) {
        value |= RGB_RAM_BLUE_BIT;
    }
    GetHAL().rx8130RamWrite(RGB_RAM_INDEX, value);
}

static void drawToggleSwitch(int x, int y, bool on)
{
    static constexpr int SWITCH_W    = 38;
    static constexpr int SWITCH_H    = 18;
    static constexpr int SWITCH_R    = 4;
    static constexpr int KNOB_W      = 12;
    static constexpr int KNOB_H      = 12;
    static constexpr int KNOB_R      = 2;
    static constexpr int KNOB_MARGIN = 3;

    const int switch_x = x + (RGB_VALUE_W - SWITCH_W) / 2;
    const int switch_y = y + (RGB_ROW_H - SWITCH_H) / 2;
    const int knob_x   = on ? switch_x + SWITCH_W - KNOB_W - KNOB_MARGIN : switch_x + KNOB_MARGIN;
    const int knob_y   = switch_y + KNOB_MARGIN;

    GetHAL().display.fillRect(x, y, RGB_VALUE_W, RGB_ROW_H, TFT_WHITE);
    GetHAL().display.drawRoundRect(switch_x, switch_y, SWITCH_W, SWITCH_H, SWITCH_R, TFT_BLACK);
    if (on) {
        GetHAL().display.fillRect(switch_x + 1, switch_y + 1, SWITCH_W - 2, SWITCH_H - 2, TFT_BLACK);
        GetHAL().display.fillRoundRect(knob_x, knob_y, KNOB_W, KNOB_H, KNOB_R, TFT_WHITE);
    } else {
        GetHAL().display.fillRoundRect(knob_x, knob_y, KNOB_W, KNOB_H, KNOB_R, TFT_BLACK);
    }
}

void AppRgb::onCreate()
{
    setAppInfo().name = "AppRgb";
    loadRgbSettings(_red_on, _green_on, _blue_on);
    if (GetHAL().isRgbReady()) {
        applyRgb();
    }
    open();
}

void AppRgb::onOpen()
{
    drawRgb();
}

void AppRgb::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const std::uint32_t now_ms = GetHAL().millis();
    const bool accept_input    = now_ms - _last_input_ms >= RGB_TOUCH_COOLDOWN_MS;
    bool changed               = false;
    bool switched_on           = false;

    if (accept_input) {
        if (GetHAL().wasTouchClickedArea(RGB_TOUCH_X, RGB_TOUCH_R_Y, RGB_TOUCH_W, RGB_TOUCH_H)) {
            _red_on     = !_red_on;
            switched_on = _red_on;
            changed     = true;
        } else if (GetHAL().wasTouchClickedArea(RGB_TOUCH_X, RGB_TOUCH_G_Y, RGB_TOUCH_W, RGB_TOUCH_H)) {
            _green_on   = !_green_on;
            switched_on = _green_on;
            changed     = true;
        } else if (GetHAL().wasTouchClickedArea(RGB_TOUCH_X, RGB_TOUCH_B_Y, RGB_TOUCH_W, RGB_TOUCH_H)) {
            _blue_on    = !_blue_on;
            switched_on = _blue_on;
            changed     = true;
        }

        if (changed) {
            _last_input_ms = now_ms;
            GetHAL().playBuzzerPattern(switched_on ? RGB_SWITCH_ON_TONES : RGB_SWITCH_OFF_TONES, 2);
            applyRgb();
            saveRgbSettings(_red_on, _green_on, _blue_on);
            GetHAL().requestRefresh();
        }
    }

    if (changed || !_has_drawn || GetHAL().isFullRedrawRequested()) {
        drawRgb();
    }
}

void AppRgb::applyRgb()
{
    GetHAL().setRgbLed(_red_on, _green_on ? 100 : 0, _blue_on ? 100 : 0);
}

void AppRgb::drawRgb()
{
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextSize(1.0f);
    drawToggleSwitch(RGB_VALUE_X, RGB_R_Y, GetHAL().isRgbReady() && _red_on);
    drawToggleSwitch(RGB_VALUE_X, RGB_G_Y, GetHAL().isRgbReady() && _green_on);
    drawToggleSwitch(RGB_VALUE_X, RGB_B_Y, GetHAL().isRgbReady() && _blue_on);

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(RGB_VALUE_X, RGB_R_Y, RGB_VALUE_W, RGB_B_Y + RGB_ROW_H - RGB_R_Y);
        GetHAL().display.waitDisplay();
    }

    _has_drawn = true;
}

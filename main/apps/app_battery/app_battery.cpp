/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <hal.h>
#include <limits>

#define APP_BATTERY_REFRESH_MS     (60000)
#define APP_BATTERY_FILTER_DIVISOR (4)

static constexpr std::uint32_t BATTERY_TOUCH_COOLDOWN_MS = 300;
static constexpr std::uint8_t BATTERY_RAM_INDEX          = 0;
static constexpr std::uint8_t BATTERY_RAM_TAG            = 0xB0;
static constexpr std::uint8_t BATTERY_RAM_TAG_MASK       = 0xF0;
static constexpr std::uint8_t BATTERY_RAM_VALUE_MASK     = 0x0F;
static constexpr int BATTERY_FILTER_DEADBAND_MV          = 3;

static constexpr int BATTERY_X              = 86;
static constexpr int BATTERY_Y              = 13;
static constexpr int BATTERY_W              = 129 - 86;
static constexpr int BATTERY_H              = 30 - 13;
static constexpr int BATTERY_TOUCH_MARGIN_X = 8;
static constexpr int BATTERY_TOUCH_MARGIN_Y = 8;

static constexpr int BATTERY_EMPTY_MV = 3300;
static constexpr int BATTERY_FULL_MV  = 4200;

enum BatteryDisplayMode {
    BATTERY_MODE_BLOCKS = 0,
    BATTERY_MODE_PERCENT,
    BATTERY_MODE_VOLTAGE,
    BATTERY_MODE_COUNT,
};

static constexpr BuzzerTone BATTERY_BLOCKS_TONES[] = {
    {.start_freq_hz = 720, .end_freq_hz = 720, .duration_ms = 18, .gap_ms = 0},
};

static constexpr BuzzerTone BATTERY_PERCENT_TONES[] = {
    {.start_freq_hz = 720, .end_freq_hz = 720, .duration_ms = 12, .gap_ms = 5},
    {.start_freq_hz = 960, .end_freq_hz = 960, .duration_ms = 16, .gap_ms = 0},
};

static constexpr BuzzerTone BATTERY_VOLTAGE_TONES[] = {
    {.start_freq_hz = 620, .end_freq_hz = 620, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 1040, .end_freq_hz = 1040, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 820, .end_freq_hz = 820, .duration_ms = 14, .gap_ms = 0},
};

static void playBatteryModeSound(std::uint8_t mode)
{
    if (mode == BATTERY_MODE_PERCENT) {
        GetHAL().playBuzzerPattern(BATTERY_PERCENT_TONES, 2);
    } else if (mode == BATTERY_MODE_VOLTAGE) {
        GetHAL().playBuzzerPattern(BATTERY_VOLTAGE_TONES, 3);
    } else {
        GetHAL().playBuzzerPattern(BATTERY_BLOCKS_TONES, 1);
    }
}

static std::uint8_t loadBatteryDisplayMode()
{
    std::uint8_t value = 0;
    if (GetHAL().rx8130RamRead(BATTERY_RAM_INDEX, &value) && (value & BATTERY_RAM_TAG_MASK) == BATTERY_RAM_TAG) {
        const std::uint8_t mode = value & BATTERY_RAM_VALUE_MASK;
        if (mode < BATTERY_MODE_COUNT) {
            return mode;
        }
    }
    return BATTERY_MODE_BLOCKS;
}

static void saveBatteryDisplayMode(std::uint8_t mode)
{
    GetHAL().rx8130RamWrite(BATTERY_RAM_INDEX, static_cast<std::uint8_t>(BATTERY_RAM_TAG | mode));
}

struct TextBounds {
    float min_x = 0.0f;
    float min_y = 0.0f;
    float max_x = 0.0f;
    float max_y = 0.0f;
};

static TextBounds getTextBounds(const char* text, const lgfx::GFXfont* font, float text_size)
{
    TextBounds bounds;
    bounds.min_x = std::numeric_limits<float>::max();
    bounds.min_y = std::numeric_limits<float>::max();
    bounds.max_x = std::numeric_limits<float>::lowest();
    bounds.max_y = std::numeric_limits<float>::lowest();

    lgfx::FontMetrics metrics;
    font->getDefaultMetric(&metrics);

    const uint16_t first = pgm_read_word(&font->first);
    const uint16_t last  = pgm_read_word(&font->last);
    const auto* glyphs   = reinterpret_cast<const lgfx::GFXglyph*>(pgm_read_ptr(&font->glyph));
    float cursor_x       = 0.0f;
    float start_x        = 0.0f;
    bool has_glyph       = false;
    for (const char* p = text; *p; ++p) {
        const uint8_t c = static_cast<uint8_t>(*p);
        if (c < first || c > last) {
            continue;
        }

        const auto* glyph     = &glyphs[c - first];
        const int8_t x_offset = static_cast<int8_t>(pgm_read_byte(&glyph->xOffset));
        if (!has_glyph && x_offset < 0) {
            start_x = -x_offset * text_size;
        }

        const int8_t y_offset   = static_cast<int8_t>(pgm_read_byte(&glyph->yOffset));
        const uint8_t width     = pgm_read_byte(&glyph->width);
        const uint8_t height    = pgm_read_byte(&glyph->height);
        const uint8_t x_advance = pgm_read_byte(&glyph->xAdvance);

        const float glyph_min_x = start_x + cursor_x + x_offset * text_size;
        const float glyph_max_x = glyph_min_x + width * text_size;
        const float glyph_min_y = (metrics.baseline + y_offset) * text_size;
        const float glyph_max_y = glyph_min_y + height * text_size;

        bounds.min_x = std::min(bounds.min_x, glyph_min_x);
        bounds.min_y = std::min(bounds.min_y, glyph_min_y);
        bounds.max_x = std::max(bounds.max_x, glyph_max_x);
        bounds.max_y = std::max(bounds.max_y, glyph_max_y);

        cursor_x += x_advance * text_size;
        has_glyph = true;
    }

    if (!has_glyph) {
        bounds.min_x = bounds.min_y = bounds.max_x = bounds.max_y = 0.0f;
    }

    return bounds;
}

static int getBatteryPercent(std::int16_t battery_mv)
{
    if (battery_mv <= 0) {
        return 0;
    }

    const int percent = (static_cast<int>(battery_mv) - BATTERY_EMPTY_MV) * 100 / (BATTERY_FULL_MV - BATTERY_EMPTY_MV);
    return std::clamp(percent, 0, 100);
}

static void drawCenteredBatteryText(const char* text, int x_offset = 0)
{
    const auto* font = &fonts::FreeSansBold9pt7b;
    float text_size  = 1.0f;
    GetHAL().display.setTextDatum(top_left);
    GetHAL().display.setFont(font);
    GetHAL().display.setTextSize(text_size);

    TextBounds bounds = getTextBounds(text, font, text_size);
    while (text_size > 0.45f && (bounds.max_x - bounds.min_x > BATTERY_W || bounds.max_y - bounds.min_y > BATTERY_H)) {
        text_size -= 0.05f;
        GetHAL().display.setTextSize(text_size);
        bounds = getTextBounds(text, font, text_size);
    }

    const float box_center_x  = BATTERY_X + BATTERY_W / 2.0f;
    const float box_center_y  = BATTERY_Y + BATTERY_H / 2.0f;
    const float text_center_x = (bounds.min_x + bounds.max_x) / 2.0f;
    const float text_center_y = (bounds.min_y + bounds.max_y) / 2.0f;
    const int draw_x          = static_cast<int>(box_center_x - text_center_x + 0.5f);
    const int draw_y          = static_cast<int>(box_center_y - text_center_y + 0.5f);

    GetHAL().display.drawString(text, draw_x + x_offset, draw_y);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setTextDatum(middle_center);
}

static void fillLeftRoundRect(int x, int y, int w, int h, int radius)
{
    GetHAL().display.fillRoundRect(x, y, w, h, radius, TFT_BLACK);
    GetHAL().display.fillRect(x + radius, y, w - radius, h, TFT_BLACK);
}

static void fillRightRoundRect(int x, int y, int w, int h, int radius)
{
    GetHAL().display.fillRoundRect(x, y, w, h, radius, TFT_BLACK);
    GetHAL().display.fillRect(x, y, w - radius, h, TFT_BLACK);
}

static void drawBatteryBlocks(int percent)
{
    static constexpr int BLOCK_COUNT  = 5;
    static constexpr int BLOCK_GAP    = 1;
    static constexpr int ROUND_RADIUS = 2;

    const int filled_blocks      = percent <= 0 ? 0 : std::clamp((percent * BLOCK_COUNT + 99) / 100, 1, BLOCK_COUNT);
    const int first_filled_index = BLOCK_COUNT - filled_blocks;
    const int total_gap          = BLOCK_GAP * (BLOCK_COUNT - 1);
    const int base_block_w       = (BATTERY_W - total_gap) / BLOCK_COUNT;
    const int extra_pixels       = (BATTERY_W - total_gap) % BLOCK_COUNT;
    int x                        = BATTERY_X;

    for (int i = 0; i < BLOCK_COUNT; ++i) {
        const int block_w = base_block_w + (i >= BLOCK_COUNT - extra_pixels ? 1 : 0);

        if (i >= first_filled_index) {
            if (i == 0) {
                fillLeftRoundRect(x, BATTERY_Y, block_w, BATTERY_H, ROUND_RADIUS);
            } else if (i == BLOCK_COUNT - 1) {
                fillRightRoundRect(x, BATTERY_Y, block_w, BATTERY_H, ROUND_RADIUS);
            } else {
                GetHAL().display.fillRect(x, BATTERY_Y, block_w, BATTERY_H, TFT_BLACK);
            }
        }
        if (i < BLOCK_COUNT - 1) {
            x += block_w + BLOCK_GAP;
        }
    }
}

void AppBattery::onCreate()
{
    setAppInfo().name = "AppBattery";
    _display_mode     = loadBatteryDisplayMode();
    open();
}

void AppBattery::onOpen()
{
    drawBattery(true);
}

void AppBattery::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const auto now_ms = GetHAL().millis();
    const bool battery_touched =
        GetHAL().wasTouchClickedArea(BATTERY_X - BATTERY_TOUCH_MARGIN_X, BATTERY_Y - BATTERY_TOUCH_MARGIN_Y,
                                     BATTERY_W + BATTERY_TOUCH_MARGIN_X * 2, BATTERY_H + BATTERY_TOUCH_MARGIN_Y * 2);

    if (battery_touched && now_ms - _last_mode_touch_ms >= BATTERY_TOUCH_COOLDOWN_MS) {
        _display_mode = static_cast<std::uint8_t>((_display_mode + 1) % BATTERY_MODE_COUNT);
        saveBatteryDisplayMode(_display_mode);
        _last_mode_touch_ms = now_ms;
        playBatteryModeSound(_display_mode);
        GetHAL().requestRefresh();
        drawBattery(false);
        return;
    }

    if (!_has_drawn || GetHAL().isRefreshRequested() || now_ms - _last_update_ms >= APP_BATTERY_REFRESH_MS) {
        drawBattery(true);
    }
}

void AppBattery::updateBatteryVoltageFilter()
{
    const std::int16_t battery_mv = GetHAL().getBatteryVoltageMv();
    if (battery_mv <= 0) {
        return;
    }

    if (!_has_filtered_battery) {
        _filtered_battery_mv  = battery_mv;
        _has_filtered_battery = true;
        return;
    }

    const std::int32_t diff = static_cast<std::int32_t>(battery_mv) - _filtered_battery_mv;
    if (std::abs(diff) <= BATTERY_FILTER_DEADBAND_MV) {
        return;
    }

    _filtered_battery_mv += diff / APP_BATTERY_FILTER_DIVISOR;
}

void AppBattery::drawBattery(bool sample_voltage)
{
    if (sample_voltage || !_has_filtered_battery) {
        updateBatteryVoltageFilter();
    }

    const std::int32_t battery_mv = _has_filtered_battery ? _filtered_battery_mv : 0;
    const int percent             = getBatteryPercent(static_cast<std::int16_t>(battery_mv));

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(BATTERY_X, BATTERY_Y, BATTERY_W, BATTERY_H, TFT_WHITE);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    if (_display_mode == BATTERY_MODE_PERCENT) {
        char percent_text[8] = {};
        snprintf(percent_text, sizeof(percent_text), "%d", percent);
        drawCenteredBatteryText(percent_text);
    } else if (_display_mode == BATTERY_MODE_VOLTAGE) {
        char voltage_text[16] = {};
        if (_has_filtered_battery) {
            snprintf(voltage_text, sizeof(voltage_text), "%.2fV", battery_mv / 1000.0f);
        } else {
            snprintf(voltage_text, sizeof(voltage_text), "--.--V");
        }
        drawCenteredBatteryText(voltage_text, 2);
    } else {
        drawBatteryBlocks(percent);
    }

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(BATTERY_X, BATTERY_Y, BATTERY_W, BATTERY_H);
        GetHAL().display.waitDisplay();
    }

    _last_update_ms = GetHAL().millis();
    _has_drawn      = true;
}

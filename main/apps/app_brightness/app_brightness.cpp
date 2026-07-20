/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <algorithm>
#include <cstdio>
#include <hal.h>
#include <limits>

static constexpr int TOUCH_PLUS_X = 4;
static constexpr int TOUCH_PLUS_Y = 41;
static constexpr int TOUCH_PLUS_W = 93 - 4;
static constexpr int TOUCH_PLUS_H = 117 - 41;

static constexpr int TOUCH_MINUS_X = 4;
static constexpr int TOUCH_MINUS_Y = 145;
static constexpr int TOUCH_MINUS_W = 93 - 4;
static constexpr int TOUCH_MINUS_H = 221 - 145;

static constexpr int VALUE_X = 39;
static constexpr int VALUE_Y = 122;
static constexpr int VALUE_W = 69 - 39;
static constexpr int VALUE_H = 139 - 122;

static constexpr bool ENABLE_TOUCH_CONTROL                          = true;
static constexpr std::uint32_t INPUT_COOLDOWN_MS                    = 120;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_INDEX            = 1;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_TAG              = 0xC0;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_TAG_MASK         = 0xE0;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_MUTED_BIT        = 0x01;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_BRIGHTNESS_MASK  = 0x1E;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_BRIGHTNESS_SHIFT = 1;
static constexpr std::uint8_t SOUND_BRIGHTNESS_DEFAULT_LEVEL        = 3;
static constexpr std::uint8_t BRIGHTNESS_MAX_LEVEL                  = 10;

static constexpr BuzzerTone BRIGHTNESS_UP_TONES[] = {
    {.start_freq_hz = 1047, .end_freq_hz = 1047, .duration_ms = 10, .gap_ms = 4},
    {.start_freq_hz = 1319, .end_freq_hz = 1319, .duration_ms = 10, .gap_ms = 4},
    {.start_freq_hz = 1760, .end_freq_hz = 1760, .duration_ms = 14, .gap_ms = 0},
};

static constexpr BuzzerTone BRIGHTNESS_DOWN_TONES[] = {
    {.start_freq_hz = 1568, .end_freq_hz = 1568, .duration_ms = 10, .gap_ms = 4},
    {.start_freq_hz = 1175, .end_freq_hz = 1175, .duration_ms = 10, .gap_ms = 4},
    {.start_freq_hz = 880, .end_freq_hz = 880, .duration_ms = 14, .gap_ms = 0},
};

static std::uint8_t loadBrightnessPercent()
{
    std::uint8_t value = 0;
    if (GetHAL().rx8130RamRead(SOUND_BRIGHTNESS_RAM_INDEX, &value) &&
        (value & SOUND_BRIGHTNESS_RAM_TAG_MASK) == SOUND_BRIGHTNESS_RAM_TAG) {
        const std::uint8_t level = static_cast<std::uint8_t>((value & SOUND_BRIGHTNESS_RAM_BRIGHTNESS_MASK) >>
                                                             SOUND_BRIGHTNESS_RAM_BRIGHTNESS_SHIFT);
        if (level <= BRIGHTNESS_MAX_LEVEL) {
            return static_cast<std::uint8_t>(level * 10);
        }
    }
    return static_cast<std::uint8_t>(SOUND_BRIGHTNESS_DEFAULT_LEVEL * 10);
}

static void saveBrightnessPercent(std::uint8_t percent)
{
    const std::uint8_t level = static_cast<std::uint8_t>(std::min<std::uint8_t>(percent / 10, BRIGHTNESS_MAX_LEVEL));
    std::uint8_t value       = 0;
    if (!GetHAL().rx8130RamRead(SOUND_BRIGHTNESS_RAM_INDEX, &value) ||
        (value & SOUND_BRIGHTNESS_RAM_TAG_MASK) != SOUND_BRIGHTNESS_RAM_TAG) {
        value = SOUND_BRIGHTNESS_RAM_TAG;
    }

    value = static_cast<std::uint8_t>((value & SOUND_BRIGHTNESS_RAM_MUTED_BIT) | SOUND_BRIGHTNESS_RAM_TAG |
                                      (level << SOUND_BRIGHTNESS_RAM_BRIGHTNESS_SHIFT));
    GetHAL().rx8130RamWrite(SOUND_BRIGHTNESS_RAM_INDEX, value);
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

void AppBrightness::onCreate()
{
    setAppInfo().name   = "AppBrightness";
    _brightness_percent = loadBrightnessPercent();
    applyBrightness();
    open();
}

void AppBrightness::onOpen()
{
    drawBrightness();
}

void AppBrightness::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    bool changed = false;

    const bool touch_plus =
        ENABLE_TOUCH_CONTROL && GetHAL().wasTouchClickedArea(TOUCH_PLUS_X, TOUCH_PLUS_Y, TOUCH_PLUS_W, TOUCH_PLUS_H);
    const bool touch_minus = ENABLE_TOUCH_CONTROL &&
                             GetHAL().wasTouchClickedArea(TOUCH_MINUS_X, TOUCH_MINUS_Y, TOUCH_MINUS_W, TOUCH_MINUS_H);
    const bool increase_requested = M5.BtnA.wasPressed() || touch_plus;
    const bool decrease_requested = M5.BtnB.wasPressed() || touch_minus;
    const std::uint32_t now_ms    = GetHAL().millis();
    const bool accept_input       = now_ms - _last_input_ms >= INPUT_COOLDOWN_MS;

    if (accept_input) {
        if (increase_requested) {
            const bool increased = increaseBrightness();
            if (increased) {
                GetHAL().playBuzzerPattern(BRIGHTNESS_UP_TONES, 3);
            }
            changed = increased;
        }
        if (decrease_requested) {
            const bool decreased = decreaseBrightness();
            if (decreased) {
                GetHAL().playBuzzerPattern(BRIGHTNESS_DOWN_TONES, 3);
            }
            changed = decreased || changed;
        }
        if (increase_requested || decrease_requested) {
            _last_input_ms = now_ms;
        }
    }

    if (changed) {
        applyBrightness();
        saveBrightnessPercent(_brightness_percent);
        GetHAL().requestRefresh();
    }

    if (changed || !_has_drawn || GetHAL().isRefreshRequested()) {
        drawBrightness();
    }
}

bool AppBrightness::increaseBrightness()
{
    return setBrightnessPercent(_brightness_percent + BRIGHTNESS_STEP);
}

bool AppBrightness::decreaseBrightness()
{
    return setBrightnessPercent(_brightness_percent - BRIGHTNESS_STEP);
}

bool AppBrightness::setBrightnessPercent(int percent)
{
    if (percent < 0) {
        percent = 0;
    } else if (percent > 100) {
        percent = 100;
    }

    if (percent == _brightness_percent) {
        return false;
    }

    _brightness_percent = static_cast<std::uint8_t>(percent);
    return true;
}

void AppBrightness::applyBrightness()
{
    const uint8_t brightness = static_cast<uint8_t>(_brightness_percent * 255 / 100);
    GetHAL().display.setBrightness(brightness);
}

void AppBrightness::drawBrightness()
{
    char value_text[8] = {};
    snprintf(value_text, sizeof(value_text), "%d", _brightness_percent);

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(VALUE_X, VALUE_Y, VALUE_W, VALUE_H, TFT_WHITE);
    GetHAL().display.setTextDatum(top_left);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);

    float text_size = 1.0f;
    GetHAL().display.setTextSize(text_size);
    TextBounds bounds = getTextBounds(value_text, &fonts::FreeSansBold9pt7b, text_size);
    while (text_size > 0.6f && (bounds.max_x - bounds.min_x > VALUE_W || bounds.max_y - bounds.min_y > VALUE_H)) {
        text_size -= 0.1f;
        GetHAL().display.setTextSize(text_size);
        bounds = getTextBounds(value_text, &fonts::FreeSansBold9pt7b, text_size);
    }

    const float box_center_x  = VALUE_X + VALUE_W / 2.0f;
    const float box_center_y  = VALUE_Y + VALUE_H / 2.0f;
    const float text_center_x = (bounds.min_x + bounds.max_x) / 2.0f;
    const float text_center_y = (bounds.min_y + bounds.max_y) / 2.0f;
    const int draw_x          = static_cast<int>(box_center_x - text_center_x + 0.5f);
    const int draw_y          = static_cast<int>(box_center_y - text_center_y + 0.5f);

    GetHAL().display.drawString(value_text, draw_x, draw_y);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setTextDatum(middle_center);
    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(VALUE_X, VALUE_Y, VALUE_W, VALUE_H);
        GetHAL().display.waitDisplay();
    }

    _has_drawn = true;
}

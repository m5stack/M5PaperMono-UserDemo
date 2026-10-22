/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <algorithm>
#include <assets.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <hal.h>
#include <limits>

#include "battery_soc.h"

#define APP_BATTERY_REFRESH_MS  (60000)
#define APP_BATTERY_SOC_TICK_MS (5000)

static constexpr std::uint32_t BATTERY_TOUCH_COOLDOWN_MS = 300;
static constexpr std::uint8_t BATTERY_RAM_INDEX          = 0;
static constexpr std::uint8_t BATTERY_RAM_TAG            = 0xB0;
static constexpr std::uint8_t BATTERY_RAM_TAG_MASK       = 0xF0;
static constexpr std::uint8_t BATTERY_RAM_VALUE_MASK     = 0x0F;

static constexpr int BATTERY_X              = 86;
static constexpr int BATTERY_Y              = 13;
static constexpr int BATTERY_W              = 129 - 86;
static constexpr int BATTERY_H              = 30 - 13;
static constexpr int BATTERY_OUTLINE_MARGIN = 4;
static constexpr int BATTERY_BLOCK_RADIUS   = 3;
static constexpr int BATTERY_OUTLINE_RADIUS = BATTERY_BLOCK_RADIUS + BATTERY_OUTLINE_MARGIN;
static constexpr int BATTERY_OUTLINE_WIDTH  = 2;
static constexpr int BATTERY_OUTLINE_X      = BATTERY_X - BATTERY_OUTLINE_MARGIN;
static constexpr int BATTERY_OUTLINE_Y      = BATTERY_Y - BATTERY_OUTLINE_MARGIN;
static constexpr int BATTERY_OUTLINE_W      = BATTERY_W + BATTERY_OUTLINE_MARGIN * 2;
static constexpr int BATTERY_OUTLINE_H      = BATTERY_H + BATTERY_OUTLINE_MARGIN * 2;
static constexpr int BATTERY_TOUCH_MARGIN_X = 8;
static constexpr int BATTERY_TOUCH_MARGIN_Y = 8;

/* Charging icon: 12x18 png drawn 1:1.
 * Right of the battery once a value is shown; in the middle of the battery
 * while no value is available yet. */
static constexpr int CHARGING_ICON_W                  = 12;
static constexpr int CHARGING_ICON_H                  = 18;
static constexpr int CHARGING_ICON_RIGHT_X            = 136;  // centre (141.5, 20.5)
static constexpr int CHARGING_ICON_RIGHT_Y            = 12;
static constexpr int CHARGING_ICON_CENTER_X           = 102;  // centre (107.5, 21.5)
static constexpr int CHARGING_ICON_CENTER_Y           = 13;
static constexpr std::uint32_t CHARGING_VALUE_HOLD_MS = 500;
/* Booting draws a burst of current (CPU, first panel refresh, radio init) that
 * pulls the terminal voltage down. Unplugging inside this window only shows
 * "--"; the estimate is taken after the window has passed. */
static constexpr std::uint32_t BOOT_QUIET_MS = 10000;

static constexpr std::uint16_t CHARGING_VIN_ON_MV   = 4200;
static constexpr std::uint32_t CHARGING_VIN_POLL_MS = 200;
static constexpr std::uint8_t CHARGING_VIN_DEBOUNCE = 2;

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

/* Battery SOC estimator. */
static bat_soc_t s_soc;

static int readBatteryMv()
{
    return GetHAL().getBatteryVoltageMv();
}

/* Boot sample captured before the peripherals were up, or 0 when unavailable. */
static int readBootBatteryMv()
{
    return GetHAL().bootBatteryVoltageMv();
}

/* Peak of a short burst. A load spike (WiFi scan, panel refresh) only pulls the
 * terminal voltage down, so the peak is the closest sample to the open-circuit
 * voltage and cannot be dragged low by a busy moment. */
static int samplePeakVoltageMv()
{
    constexpr int kSamples = 16;
    int peak               = 0;
    for (int i = 0; i < kSamples; ++i) {
        const int mv = readBatteryMv();
        if (mv > peak) {
            peak = mv;
        }
    }
    return peak;
}

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

static void drawCenteredBatteryText(const char* text, int x_offset = 0)
{
    const auto* font = &fonts::FreeSansBold9pt7b;
    float text_size  = 1.0f;
    GetHAL().display.setTextDatum(top_left);
    GetHAL().display.setFont(font);
    GetHAL().display.setTextSize(text_size);
    /* No background: the caller has already cleared the area, and a background
     * rect would paint white over the battery outline around the glyphs. */
    GetHAL().display.setTextColor(TFT_BLACK);

    TextBounds bounds = getTextBounds(text, font, text_size);
    while (text_size > 0.45f && (bounds.max_x - bounds.min_x > BATTERY_W || bounds.max_y - bounds.min_y > BATTERY_H)) {
        text_size -= 0.05f;
        GetHAL().display.setTextSize(text_size);
        bounds = getTextBounds(text, font, text_size);
    }
    /* Short placeholders ("--", "--.--V") are narrow, so scale them up a little
     * instead of leaving a tiny glyph in the middle. Kept modest so the glyphs
     * never reach the outline. */
    if (strlen(text) <= 6) {
        const float text_w = bounds.max_x - bounds.min_x;
        const float text_h = bounds.max_y - bounds.min_y;
        if (text_h > 0.0f) {
            const float fill     = std::min((BATTERY_H - 10) / text_h, (BATTERY_W - 20) / std::max(text_w, 1.0f));
            const float enlarged = std::min(text_size * fill, text_size * 1.6f);
            if (enlarged > text_size + 0.05f) {
                text_size = enlarged;
                GetHAL().display.setTextSize(text_size);
                bounds = getTextBounds(text, font, text_size);
            }
        }
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

static void drawBatteryOutline()
{
    GetHAL().display.fillSmoothRoundRect(BATTERY_OUTLINE_X, BATTERY_OUTLINE_Y, BATTERY_OUTLINE_W, BATTERY_OUTLINE_H,
                                         BATTERY_OUTLINE_RADIUS, TFT_BLACK);
    GetHAL().display.fillSmoothRoundRect(
        BATTERY_OUTLINE_X + BATTERY_OUTLINE_WIDTH, BATTERY_OUTLINE_Y + BATTERY_OUTLINE_WIDTH,
        BATTERY_OUTLINE_W - BATTERY_OUTLINE_WIDTH * 2, BATTERY_OUTLINE_H - BATTERY_OUTLINE_WIDTH * 2,
        BATTERY_OUTLINE_RADIUS - BATTERY_OUTLINE_WIDTH, TFT_WHITE);
}

void AppBattery::onCreate()
{
    setAppInfo().name = "AppBattery";
    _display_mode     = loadBatteryDisplayMode();
    _boot_ms          = GetHAL().millis();

    // Read USB presence before the first tick
    const std::uint16_t vbat_mv = M5.Power.M5pm1.getBatteryVoltage();
    const std::uint16_t vin_mv  = M5.Power.M5pm1.getVBUSVoltage();
    if (vbat_mv != 0 || vin_mv != 0) {
        _charging_icon_visible = (vin_mv >= CHARGING_VIN_ON_MV);
    }
    _value_hold_pending = false;

    bat_soc_init(&s_soc, readBatteryMv);
    bat_soc_set_load(&s_soc, BAT_LD_NORMAL);
    /* Only seed when not charging: a lifted terminal voltage must not anchor the
     * estimate, that case reports "--" until the charger is removed. */
    if (!_charging_icon_visible) {
        bat_soc_seed(&s_soc, readBootBatteryMv());
    }
    _last_soc_tick_ms = GetHAL().millis();
    bat_soc_tick(&s_soc, APP_BATTERY_SOC_TICK_MS, _charging_icon_visible ? 1 : 0);

    open();
}

void AppBattery::onOpen()
{
    drawBattery();
}

void AppBattery::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const auto now_ms = GetHAL().millis();

    const bool usb_changed = updateUsbState(now_ms);
    tickBatterySoc(now_ms);

    const bool battery_touched =
        GetHAL().wasTouchClickedArea(BATTERY_X - BATTERY_TOUCH_MARGIN_X, BATTERY_Y - BATTERY_TOUCH_MARGIN_Y,
                                     BATTERY_W + BATTERY_TOUCH_MARGIN_X * 2, BATTERY_H + BATTERY_TOUCH_MARGIN_Y * 2);

    if (battery_touched && now_ms - _last_mode_touch_ms >= BATTERY_TOUCH_COOLDOWN_MS) {
        _display_mode = static_cast<std::uint8_t>((_display_mode + 1) % BATTERY_MODE_COUNT);
        saveBatteryDisplayMode(_display_mode);
        _last_mode_touch_ms = now_ms;
        playBatteryModeSound(_display_mode);
        GetHAL().requestRefresh();
        drawBattery();
        return;
    }

    /* Everything below only decides whether to repaint; the paint itself is
     * always the same full-redraw function. */
    bool repaint = false;

    if (!_has_drawn || GetHAL().isFullRedrawRequested() || now_ms - _last_update_ms >= APP_BATTERY_REFRESH_MS) {
        repaint = true;
    } else if (usb_changed) {
        repaint = true;
    } else if (!_ready_drawn && bat_soc_ready(&s_soc)) {
        repaint = true;  // first estimate: no need to wait for the 60s refresh
    } else if (_value_hold_pending && (std::int32_t)(now_ms - _value_hold_until_ms) >= 0) {
        _value_hold_pending = false;
        /* No value yet (booted while charging): build one from the peak of a
         * short burst. A WiFi scan or a panel refresh only pulls the terminal
         * voltage down, so the peak avoids anchoring on a busy moment. */
        if (!bat_soc_ready(&s_soc)) {
            bat_soc_seed(&s_soc, samplePeakVoltageMv());
            bat_soc_tick(&s_soc, APP_BATTERY_SOC_TICK_MS, 0);
            _last_soc_tick_ms = now_ms;
        }
        repaint = true;
    }

    if (repaint) {
        drawBattery();
    }
}

/* Run the SOC estimator every 5s. */
void AppBattery::tickBatterySoc(std::uint32_t now_ms)
{
    if (now_ms - _last_soc_tick_ms < APP_BATTERY_SOC_TICK_MS) {
        return;
    }
    const std::uint32_t dt_ms = now_ms - _last_soc_tick_ms;
    _last_soc_tick_ms         = now_ms;

    bat_soc_set_load(&s_soc, BAT_LD_NORMAL);
    bat_soc_tick(&s_soc, dt_ms, _charging_icon_visible ? 1 : 0);
}

/* Poll PM1 VIN. Returns true when the USB state changed. */
bool AppBattery::updateUsbState(std::uint32_t now_ms)
{
    if (now_ms - _last_vin_ms < CHARGING_VIN_POLL_MS) {
        return false;
    }
    _last_vin_ms = now_ms;

    const std::uint16_t vbat_mv = M5.Power.M5pm1.getBatteryVoltage();
    const std::uint16_t vin_mv  = M5.Power.M5pm1.getVBUSVoltage();
    if (vbat_mv == 0 && vin_mv == 0) {
        return false;  // PM1 not responding, keep the current state
    }

    const bool present = vin_mv >= CHARGING_VIN_ON_MV;
    if (present == _charging_icon_visible) {
        _vin_debounce = 0;
        return false;
    }
    if (++_vin_debounce < CHARGING_VIN_DEBOUNCE) {
        return false;
    }

    _vin_debounce          = 0;
    _charging_icon_visible = present;

    /* Unplugged without a value (booted while charging): wait before building
     * one. Inside BOOT_QUIET_MS the boot current is still flowing, so the wait
     * is extended to the end of that window; afterwards the normal settle
     * applies. */
    if (!present && !bat_soc_ready(&s_soc)) {
        std::uint32_t hold_until           = now_ms + CHARGING_VALUE_HOLD_MS;
        const std::uint32_t boot_quiet_end = _boot_ms + BOOT_QUIET_MS;
        if ((std::int32_t)(boot_quiet_end - hold_until) > 0) {
            hold_until = boot_quiet_end;
        }
        _value_hold_until_ms = hold_until;
        _value_hold_pending  = true;
    }
    return true;
}

/* Draw the charging icon at one of its two positions. Only called from
 * drawBattery(), which has already cleared the whole area. */
void AppBattery::drawChargingIcon(bool visible, bool centered)
{
    if (!visible) {
        return;
    }
    const int x = centered ? CHARGING_ICON_CENTER_X : CHARGING_ICON_RIGHT_X;
    const int y = centered ? CHARGING_ICON_CENTER_Y : CHARGING_ICON_RIGHT_Y;
    GetHAL().display.drawPng(img_charging_start, img_charging_end - img_charging_start, x, y);
}

/* Snap a percentage to 5-point steps for display: 0, 5, 10 ... 100.
 * The estimator keeps its 1-point resolution; the panel is not a precision
 * instrument and a number that moves by one point is more distracting than
 * useful. 0 and 100 are kept exact so "empty" and "full" stay unambiguous. */
static int coarsePercent(int percent)
{
    if (percent <= 0) {
        return 0;
    }
    if (percent >= 100) {
        return 100;
    }
    return ((percent + 4) / 5) * 5;
}

/* The only paint path. It clears the whole area that may hold the battery, the
 * value or either icon spot, redraws everything from the current state and
 * pushes one refresh. Nothing outside this function touches the panel, so no
 * partial state can survive. */
void AppBattery::drawBattery()
{
    const bool ready     = bat_soc_ready(&s_soc);
    const int percent    = ready ? coarsePercent(bat_soc_percent(&s_soc)) : 0;
    const int battery_mv = bat_soc_voltage_live_mv(&s_soc);
    /* Icon position depends on whether a value exists, not on charging:
     *   no value  -> icon in the middle of the battery, value hidden
     *   value     -> icon right of the battery, value stays visible */
    const bool icon_centered = _charging_icon_visible && !ready;

    /* One rect covering the battery outline and the right icon spot. */
    const int area_x0 = BATTERY_OUTLINE_X;
    const int area_y0 = std::min(BATTERY_OUTLINE_Y, CHARGING_ICON_RIGHT_Y);
    const int area_x1 = CHARGING_ICON_RIGHT_X + CHARGING_ICON_W;
    const int area_y1 = std::max(BATTERY_OUTLINE_Y + BATTERY_OUTLINE_H, CHARGING_ICON_RIGHT_Y + CHARGING_ICON_H);

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(area_x0, area_y0, area_x1 - area_x0, area_y1 - area_y0, TFT_WHITE);

    drawBatteryOutline();
    if (icon_centered) {
        drawChargingIcon(true, true);
    } else if (_display_mode == BATTERY_MODE_PERCENT) {
        char percent_text[8] = {};
        if (ready) {
            snprintf(percent_text, sizeof(percent_text), "%d", percent);
        } else {
            snprintf(percent_text, sizeof(percent_text), "--");
        }
        drawCenteredBatteryText(percent_text);
    } else if (_display_mode == BATTERY_MODE_VOLTAGE) {
        char voltage_text[16] = {};
        if (ready) {
            snprintf(voltage_text, sizeof(voltage_text), "%.2fV", battery_mv / 1000.0f);
        } else {
            snprintf(voltage_text, sizeof(voltage_text), "--.--V");
        }
        drawCenteredBatteryText(voltage_text, 2);
    } else {
        drawBatteryBlocks(percent);
    }
    /* Icon right of the battery once the value is shown. */
    if (_charging_icon_visible && !icon_centered) {
        drawChargingIcon(true, false);
    }

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(area_x0, area_y0, area_x1 - area_x0, area_y1 - area_y0);
        GetHAL().display.waitDisplay();
    }

    _last_update_ms = GetHAL().millis();
    _has_drawn      = true;
    _ready_drawn    = ready;
}

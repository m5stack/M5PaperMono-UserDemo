/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <hal.h>

#define APP_MIC_REFRESH_MS (30000)
#define APP_MIC_SAMPLE_MS  (100)

static constexpr int MIC_X = 43;
static constexpr int MIC_Y = 234;
static constexpr int MIC_W = 92 - 43;
static constexpr int MIC_H = 266 - 234;

static constexpr int MIC_TOUCH_X = 1;
static constexpr int MIC_TOUCH_Y = 225;
static constexpr int MIC_TOUCH_W = 96 - 1;
static constexpr int MIC_TOUCH_H = 277 - 225;

static constexpr BuzzerTone MIC_TOUCH_TONES[] = {
    {.start_freq_hz = 1500, .end_freq_hz = 1500, .duration_ms = 8, .gap_ms = 5},
    {.start_freq_hz = 2100, .end_freq_hz = 2100, .duration_ms = 10, .gap_ms = 0},
};

void AppMicWave::onCreate()
{
    setAppInfo().name = "AppMicWave";
    _mic_ready        = initMic();
    open();
}

void AppMicWave::onOpen()
{
    updateMicBuffer(true);
    drawMicWave();
}

void AppMicWave::onRunning()
{
    const auto now_ms = GetHAL().millis();
    if (!_has_sample || now_ms - _last_sample_ms >= APP_MIC_SAMPLE_MS) {
        updateMicBuffer(false);
    }

    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    bool touch_changed       = false;
    const bool touch_pressed = GetHAL().isTouchPressed();
    std::int16_t touch_x     = -1;
    std::int16_t touch_y     = -1;
    if (touch_pressed) {
        const auto raw = M5.Touch.getTouchPointRaw();
        touch_x        = static_cast<std::int16_t>(raw.x);
        touch_y        = static_cast<std::int16_t>(raw.y);
    }

    const bool touch_in_mic_area = touch_pressed && touch_x >= MIC_TOUCH_X && touch_x <= MIC_TOUCH_X + MIC_TOUCH_W &&
                                   touch_y >= MIC_TOUCH_Y && touch_y <= MIC_TOUCH_Y + MIC_TOUCH_H;
    if (touch_pressed != _last_touch_pressed || touch_x != _last_touch_x || touch_y != _last_touch_y) {
        touch_changed = touch_in_mic_area && !_last_touch_pressed;
        if (touch_changed && !_last_touch_pressed) {
            GetHAL().playBuzzerPattern(MIC_TOUCH_TONES, sizeof(MIC_TOUCH_TONES) / sizeof(MIC_TOUCH_TONES[0]));
            updateMicBuffer(true);
        }
        _last_touch_pressed = touch_pressed;
        _last_touch_x       = touch_x;
        _last_touch_y       = touch_y;
    }

    if (!_has_drawn || GetHAL().isRefreshRequested() || touch_changed ||
        now_ms - _last_update_ms >= APP_MIC_REFRESH_MS) {
        if (touch_changed && !GetHAL().isRefreshRequested()) {
            GetHAL().prepareDisplayRefreshObservers(this);
        }
        drawMicWave();
        if (touch_changed) {
            _last_touch_refresh_ms = now_ms;
        }
    }
}

void AppMicWave::prepareForDisplayRefresh()
{
    if (GetHAL().isUiModalActive()) {
        return;
    }

    drawMicWave();
}

bool AppMicWave::initMic()
{
    if (_mic_ready) {
        return true;
    }

    if (!GetHAL().initMic()) {
        return false;
    }

    GetHAL().readMicSamples(_sample_buffer, SAMPLE_COUNT);
    std::fill(std::begin(_levels), std::end(_levels), 0);

    return true;
}

void AppMicWave::updateMicBuffer(bool force)
{
    const auto now_ms = GetHAL().millis();
    if (!force && _has_sample && now_ms - _last_sample_ms < APP_MIC_SAMPLE_MS) {
        return;
    }

    if (!_mic_ready) {
        _mic_ready = initMic();
        if (!_mic_ready) {
            std::fill(std::begin(_levels), std::end(_levels), 0);
            _last_sample_ms = now_ms;
            _has_sample     = true;
            return;
        }
    }

    if (!GetHAL().readMicSamples(_sample_buffer, SAMPLE_COUNT)) {
        _last_sample_ms = now_ms;
        return;
    }

    processSamples();
    _last_sample_ms = now_ms;
    _has_sample     = true;
}

void AppMicWave::processSamples()
{
    std::int32_t mean = 0;
    for (const auto sample : _sample_buffer) {
        mean += sample;
    }
    mean /= SAMPLE_COUNT;

    std::int32_t peak = 0;
    for (const auto sample : _sample_buffer) {
        const std::int32_t centered   = static_cast<std::int32_t>(sample) - mean;
        const std::int32_t abs_sample = std::abs(centered);
        peak                          = std::max(peak, abs_sample);
    }
    const int max_level = MIC_H / 2 - 2;
    const int divisor   = std::max<std::int32_t>(1, peak / std::max(1, max_level));

    for (int x = 0; x < LEVEL_COUNT; ++x) {
        const int start  = x * SAMPLE_COUNT / LEVEL_COUNT;
        const int end    = std::max(start + 1, (x + 1) * SAMPLE_COUNT / LEVEL_COUNT);
        std::int32_t sum = 0;
        for (int i = start; i < end; ++i) {
            sum += std::abs(static_cast<int>(_sample_buffer[i]) - static_cast<int>(mean));
        }
        const std::int32_t avg = sum / (end - start);
        const int level        = std::clamp(static_cast<int>(avg / divisor), 0, max_level);
        _levels[x]             = static_cast<std::uint8_t>(level);
    }
}

void AppMicWave::drawMicWave()
{
    updateMicBuffer(false);

    auto& display = GetHAL().display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    display.fillRect(MIC_X, MIC_Y, MIC_W, MIC_H, TFT_WHITE);
    display.setClipRect(MIC_X, MIC_Y, MIC_W, MIC_H);

    const int center_y = MIC_Y + MIC_H / 2;

    if (_mic_ready || _has_sample) {
        for (int x = 0; x < LEVEL_COUNT; ++x) {
            const int level = _levels[x];
            const int px    = MIC_X + x;
            display.drawFastVLine(px, center_y - level, level * 2 + 1, TFT_BLACK);
        }
    }

    display.clearClipRect();
    display.setTextDatum(middle_center);
    display.setTextSize(1.0f);

    if (!GetHAL().isRefreshRequested() && !GetHAL().isPreparingDisplayRefreshObservers()) {
        GetHAL().flushDisplay(MIC_X, MIC_Y, MIC_W, MIC_H);
        display.waitDisplay();
    }

    _last_update_ms = GetHAL().millis();
    _has_drawn      = true;
}

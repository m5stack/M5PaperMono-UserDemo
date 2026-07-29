/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <assets.h>
#include <cstdint>
#include <hal.h>

static constexpr int BUZZER_X                                       = 366;
static constexpr int BUZZER_Y                                       = 480;
static constexpr int BUZZER_W                                       = 473 - 366;
static constexpr int BUZZER_H                                       = 522 - 480;
static constexpr std::uint32_t BUZZER_TOUCH_COOLDOWN_MS             = 250;
static constexpr std::uint32_t BUZZER_MUTE_SOUND_MS                 = 48;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_INDEX            = 1;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_TAG              = 0xC0;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_TAG_MASK         = 0xE0;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_MUTED_BIT        = 0x01;
static constexpr std::uint8_t SOUND_BRIGHTNESS_RAM_BRIGHTNESS_SHIFT = 1;
static constexpr std::uint8_t SOUND_BRIGHTNESS_DEFAULT_LEVEL        = 3;

static constexpr BuzzerTone BUZZER_UNMUTE_TONES[] = {
    {.start_freq_hz = 880, .end_freq_hz = 880, .duration_ms = 18, .gap_ms = 6},
    {.start_freq_hz = 1320, .end_freq_hz = 1320, .duration_ms = 24, .gap_ms = 0},
};

static constexpr BuzzerTone BUZZER_MUTE_TONES[] = {
    {.start_freq_hz = 1200, .end_freq_hz = 1200, .duration_ms = 18, .gap_ms = 6},
    {.start_freq_hz = 760, .end_freq_hz = 760, .duration_ms = 24, .gap_ms = 0},
};

static bool loadBuzzerMuted()
{
    std::uint8_t value = 0;
    if (GetHAL().rx8130RamRead(SOUND_BRIGHTNESS_RAM_INDEX, &value) &&
        (value & SOUND_BRIGHTNESS_RAM_TAG_MASK) == SOUND_BRIGHTNESS_RAM_TAG) {
        return (value & SOUND_BRIGHTNESS_RAM_MUTED_BIT) != 0;
    }
    return false;
}

static void saveBuzzerMuted(bool muted)
{
    std::uint8_t value = 0;
    if (!GetHAL().rx8130RamRead(SOUND_BRIGHTNESS_RAM_INDEX, &value) ||
        (value & SOUND_BRIGHTNESS_RAM_TAG_MASK) != SOUND_BRIGHTNESS_RAM_TAG) {
        value = static_cast<std::uint8_t>(SOUND_BRIGHTNESS_RAM_TAG |
                                          (SOUND_BRIGHTNESS_DEFAULT_LEVEL << SOUND_BRIGHTNESS_RAM_BRIGHTNESS_SHIFT));
    }

    value = static_cast<std::uint8_t>((value & ~SOUND_BRIGHTNESS_RAM_MUTED_BIT) |
                                      (muted ? SOUND_BRIGHTNESS_RAM_MUTED_BIT : 0));
    GetHAL().rx8130RamWrite(SOUND_BRIGHTNESS_RAM_INDEX, value);
}

void AppBuzzer::onCreate()
{
    setAppInfo().name = "AppBuzzer";
    GetHAL().setBuzzerMuted(loadBuzzerMuted());
    open();
}

void AppBuzzer::onOpen()
{
    drawBuzzer();
}

void AppBuzzer::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const std::uint32_t now_ms = GetHAL().millis();
    if (_pending_mute && now_ms >= _mute_apply_ms) {
        GetHAL().setBuzzerMuted(true);
        saveBuzzerMuted(true);
        _pending_mute = false;
    }

    const bool touched = GetHAL().wasTouchClickedArea(BUZZER_X, BUZZER_Y, BUZZER_W, BUZZER_H);
    if (touched && now_ms - _last_input_ms >= BUZZER_TOUCH_COOLDOWN_MS) {
        _last_input_ms   = now_ms;
        const bool muted = GetHAL().isBuzzerMuted() || _pending_mute;
        if (muted) {
            _pending_mute = false;
            GetHAL().setBuzzerMuted(false);
            saveBuzzerMuted(false);
            GetHAL().playBuzzerPattern(BUZZER_UNMUTE_TONES, 2);
        } else {
            GetHAL().playBuzzerPattern(BUZZER_MUTE_TONES, 2);
            _pending_mute  = true;
            _mute_apply_ms = now_ms + BUZZER_MUTE_SOUND_MS;
        }
        drawBuzzer(true);
        return;
    }

    if (!_has_drawn || GetHAL().isFullRedrawRequested()) {
        drawBuzzer();
    }
}

void AppBuzzer::drawBuzzer(bool force_refresh)
{
    const bool muted          = GetHAL().isBuzzerMuted() || _pending_mute;
    const std::uint8_t* start = muted ? img_MUTE_ON_start : img_MUTE_OFF_start;
    const std::uint8_t* end   = muted ? img_MUTE_ON_end : img_MUTE_OFF_end;

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fast);
    GetHAL().display.drawPng(start, end - start, BUZZER_X, BUZZER_Y);

    if (force_refresh || !GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(BUZZER_X, BUZZER_Y, BUZZER_W, BUZZER_H);
        GetHAL().display.waitDisplay();
    }
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);

    _has_drawn = true;
}

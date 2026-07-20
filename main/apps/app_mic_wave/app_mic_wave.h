/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <hal.h>
#include <mooncake.h>
#include <cstdint>

class AppMicWave : public mooncake::AppAbility, public DisplayRefreshObserver {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void prepareForDisplayRefresh() override;

private:
    static constexpr int SAMPLE_COUNT = 1024;
    static constexpr int LEVEL_COUNT  = 93 - 34;

    std::int16_t _sample_buffer[SAMPLE_COUNT] = {};
    std::uint8_t _levels[LEVEL_COUNT]         = {};
    std::uint32_t _last_update_ms             = 0;
    std::uint32_t _last_sample_ms             = 0;
    std::uint32_t _last_touch_refresh_ms      = 0;
    std::int16_t _last_touch_x                = -1;
    std::int16_t _last_touch_y                = -1;
    bool _last_touch_pressed                  = false;
    bool _mic_ready                           = false;
    bool _has_sample                          = false;
    bool _has_drawn                           = false;

    bool initMic();
    void updateMicBuffer(bool force = false);
    void processSamples();
    void drawMicWave();
};

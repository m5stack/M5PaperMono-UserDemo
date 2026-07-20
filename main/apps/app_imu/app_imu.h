/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <hal.h>
#include <mooncake.h>
#include <cstdint>

class AppImu : public mooncake::AppAbility, public DisplayRefreshObserver {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void prepareForDisplayRefresh() override;

private:
    char _accel_text[3][16]              = {};
    char _gyro_text[3][16]               = {};
    std::uint32_t _last_update_ms        = 0;
    std::uint32_t _last_sample_ms        = 0;
    std::uint32_t _last_touch_refresh_ms = 0;
    std::int16_t _last_touch_x           = -1;
    std::int16_t _last_touch_y           = -1;
    bool _last_touch_pressed             = false;
    bool _has_sample                     = false;
    bool _has_drawn                      = false;

    void updateImuBuffer(bool force = false);
    void drawImu();
};

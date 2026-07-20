/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>

class AppBattery : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    std::uint32_t _last_update_ms     = 0;
    std::uint32_t _last_mode_touch_ms = 0;
    std::int32_t _filtered_battery_mv = 0;
    std::uint8_t _display_mode        = 0;
    bool _has_filtered_battery        = false;
    bool _has_drawn                   = false;

    void drawBattery(bool sample_voltage = true);
    void updateBatteryVoltageFilter();
};

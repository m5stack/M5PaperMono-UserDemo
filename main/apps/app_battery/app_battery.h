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
    std::uint32_t _last_update_ms      = 0;
    std::uint32_t _last_mode_touch_ms  = 0;
    std::uint32_t _last_soc_tick_ms    = 0;
    std::uint32_t _value_hold_until_ms = 0;
    std::uint32_t _last_vin_ms         = 0;
    std::uint32_t _boot_ms             = 0;
    std::uint8_t _display_mode         = 0;
    std::uint8_t _vin_debounce         = 0;
    bool _has_drawn                    = false;
    bool _ready_drawn                  = false;
    bool _value_hold_pending           = false;
    // USB presence from PM1 VIN; also drives the charging icon
    bool _charging_icon_visible = false;

    void drawBattery();
    void drawChargingIcon(bool visible, bool centered);
    void tickBatterySoc(std::uint32_t now_ms);
    bool updateUsbState(std::uint32_t now_ms);
};

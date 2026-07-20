/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>

class AppBrightness : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    static constexpr std::uint8_t BRIGHTNESS_STEP = 10;
    std::uint8_t _brightness_percent              = 30;
    std::uint32_t _last_input_ms                  = 0;
    bool _has_drawn                               = false;

    bool increaseBrightness();
    bool decreaseBrightness();
    bool setBrightnessPercent(int percent);
    void applyBrightness();
    void drawBrightness();
};

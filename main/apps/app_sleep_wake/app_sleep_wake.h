/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>

class AppSleepWake : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    std::uint32_t _last_input_ms = 0;
    bool _has_drawn              = false;

    void drawPressedImage(const std::uint8_t* start, const std::uint8_t* end, int x, int y, int w, int h);
    void enterImuWakeShutdown();
    void enterTouchWakeDeepSleep();
    void enterRtc10sWakeShutdown();
};

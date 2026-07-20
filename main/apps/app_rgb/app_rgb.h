/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>

class AppRgb : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    bool _red_on                 = true;
    bool _green_on               = false;
    bool _blue_on                = false;
    std::uint32_t _last_input_ms = 0;
    bool _has_drawn              = false;

    void drawRgb();
    void applyRgb();
};

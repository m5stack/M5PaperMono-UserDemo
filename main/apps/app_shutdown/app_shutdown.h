/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>

class AppShutdown : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    std::uint32_t _last_input_ms = 0;
    bool _has_drawn              = false;

    void drawShutdownPressed();
    void drawShutdownCleanScreen();
    void drawShutdownLogo();
    void enterShutdown();
};

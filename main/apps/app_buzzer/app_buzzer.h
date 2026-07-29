/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>

class AppBuzzer : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    std::uint32_t _last_input_ms = 0;
    std::uint32_t _mute_apply_ms = 0;
    bool _has_drawn              = false;
    bool _pending_mute           = false;

    void drawBuzzer(bool force_refresh = false);
};

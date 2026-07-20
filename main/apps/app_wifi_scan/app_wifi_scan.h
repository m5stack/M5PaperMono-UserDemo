/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <mooncake.h>
#include <cstdint>
#include <string>
#include <vector>

class AppWifiScan : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    struct WifiNetwork {
        std::string ssid;
        std::int32_t rssi = 0;
        bool secure       = false;
    };

    std::vector<WifiNetwork> _networks;
    std::uint32_t _last_input_ms = 0;
    bool _scan_running           = false;
    bool _auto_scan_started      = false;
    bool _has_drawn              = false;

    void startScan(bool play_sound = false);
    void finishScan(int count);
    void clearResults();
    void drawWifi();
    void drawStatusText(const char* text);
    std::int16_t visibleRows() const;
};

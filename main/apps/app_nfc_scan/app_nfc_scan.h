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

class AppNfcScan : public mooncake::AppAbility {
public:
    struct NfcInfo {
        std::string label;
        std::string value;
    };

    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    std::vector<NfcInfo> _info_items;
    std::uint32_t _last_input_ms = 0;
    std::uint16_t _info_index    = 0;
    bool _scan_running           = false;
    bool _has_drawn              = false;

    void startScan();
    void finishScanIfReady();
    void drawNfc();
    void setStatus(const char* text);
    void setNoCard();
    void showNextInfo();
};

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <app_keyboard/app_keyboard.h>
#include <mooncake.h>
#include <cstdint>
#include <string>
#include <vector>

class AppClock : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    struct WifiChoice {
        std::string ssid;
        std::int32_t rssi = 0;
    };

    ScreenKeyboard _keyboard;
    std::vector<WifiChoice> _ntp_networks;
    std::vector<std::uint8_t> _ntp_background;
    std::string _ntp_ssid;
    std::string _ntp_password;
    std::string _ntp_status;
    std::uint32_t _last_update_ms         = 0;
    std::uint32_t _last_minute_check_ms   = 0;
    std::int64_t _last_display_minute_key = -1;
    std::uint32_t _last_timezone_touch_ms = 0;
    std::uint32_t _last_ntp_touch_ms      = 0;
    std::uint32_t _ntp_connect_start_ms   = 0;
    std::uint32_t _ntp_success_close_ms   = 0;
    std::uint32_t _next_auto_ntp_ms       = 0;
    std::int16_t _ntp_list_scroll         = 0;
    std::int16_t _ntp_last_touch_y        = 0;
    std::int16_t _ntp_touch_start_x       = 0;
    std::int16_t _ntp_touch_start_y       = 0;
    std::int16_t _ntp_background_w        = 0;
    std::int16_t _ntp_background_h        = 0;
    std::uint8_t _ntp_edit_field          = 0;
    std::uint8_t _timezone_index          = 0;
    bool _ntp_dialog_open                 = false;
    bool _ntp_scan_running                = false;
    bool _ntp_connecting                  = false;
    bool _ntp_syncing                     = false;
    bool _ntp_auto_enabled                = false;
    bool _ntp_auto_syncing                = false;
    bool _ntp_touching                    = false;
    bool _ntp_list_touching               = false;
    bool _ntp_list_dragged                = false;
    bool _ntp_background_valid            = false;
    bool _has_drawn                       = false;

    void drawTime();
    void switchTimezone();
    void openNtpDialog();
    void closeNtpDialog();
    void drawNtpDialog();
    void drawNtpInputFields();
    void handleNtpDialog();
    void startNtpScan();
    void finishNtpScan(int count);
    void startNtpSync(bool automatic = false);
    void pollNtpSync();
    void openNtpKeyboard(std::uint8_t field);
    void commitNtpKeyboard();
    void loadNtpSettings();
    void saveNtpSettings();
    void scheduleNextAutoNtp(std::uint32_t delay_ms);
    void captureNtpBackground();
    void restoreNtpBackground();
};

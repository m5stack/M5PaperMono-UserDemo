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

class AppLora : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    ScreenKeyboard _keyboard;
    std::string _rx_text;
    std::string _tx_text         = "Hello!";
    std::uint32_t _last_touch_ms = 0;
    std::uint32_t _tx_tick_ms    = 0;
    bool _has_drawn              = false;
    bool _lora_init_attempted    = false;
    bool _sending                = false;

    void drawLora();
    void drawReceive();
    void drawInput(bool immediate_refresh = true);
    void drawSendButton(bool active = false);
    void openKeyboard();
    void handleKeyboard();
    void startSend();
    void finishSend(bool success);
};

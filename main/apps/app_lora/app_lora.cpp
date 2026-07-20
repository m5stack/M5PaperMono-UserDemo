/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <LGFX_Fonts.hpp>
#include <M5Unified.h>
#include <algorithm>
#include <cstdint>
#include <esp_log.h>
#include <hal.h>

static constexpr const char* APP_LORA_TAG               = "AppLora";
static constexpr std::uint32_t LORA_TOUCH_COOLDOWN_MS   = 250;
static constexpr std::uint32_t LORA_TX_TICK_INTERVAL_MS = 900;
static constexpr int LORA_RX_X                          = 150;
static constexpr int LORA_RX_Y                          = 44;
static constexpr int LORA_RX_W                          = 473 - 150;
static constexpr int LORA_RX_H                          = 108 - 44;

static constexpr int LORA_TX_X = 149;
static constexpr int LORA_TX_Y = 134;
static constexpr int LORA_TX_W = 370 - 149;
static constexpr int LORA_TX_H = 180 - 134;

static constexpr int LORA_SEND_X = 370;
static constexpr int LORA_SEND_Y = 132;
static constexpr int LORA_SEND_W = 475 - 370;
static constexpr int LORA_SEND_H = 182 - 132;

// Short rising sweep for the send button press.
static constexpr BuzzerTone LORA_SEND_START_TONES[] = {
    {.start_freq_hz = 650, .end_freq_hz = 900, .duration_ms = 28, .gap_ms = 6},
    {.start_freq_hz = 900, .end_freq_hz = 900, .duration_ms = 22, .gap_ms = 0},
};

// Periodic tick while a transmit is in progress.
static constexpr BuzzerTone LORA_TX_TICK_TONES[] = {
    {.start_freq_hz = 800, .end_freq_hz = 800, .duration_ms = 60, .gap_ms = 60},
    {.start_freq_hz = 800, .end_freq_hz = 800, .duration_ms = 60, .gap_ms = 60},
    {.start_freq_hz = 800, .end_freq_hz = 800, .duration_ms = 60, .gap_ms = 0},
};

// Two-tone confirmation for a successful transmit.
static constexpr BuzzerTone LORA_SEND_DONE_TONES[] = {
    {.start_freq_hz = 780, .end_freq_hz = 780, .duration_ms = 40, .gap_ms = 30},
    {.start_freq_hz = 1050, .end_freq_hz = 1050, .duration_ms = 55, .gap_ms = 0},
};

// Descending tone for transmit failure.
static constexpr BuzzerTone LORA_SEND_FAIL_TONES[] = {
    {.start_freq_hz = 720, .end_freq_hz = 720, .duration_ms = 40, .gap_ms = 20},
    {.start_freq_hz = 480, .end_freq_hz = 380, .duration_ms = 80, .gap_ms = 0},
};

// Two short tones when a packet is received.
static constexpr BuzzerTone LORA_RX_TONES[] = {
    {.start_freq_hz = 700, .end_freq_hz = 700, .duration_ms = 45, .gap_ms = 25},
    {.start_freq_hz = 960, .end_freq_hz = 960, .duration_ms = 45, .gap_ms = 0},
};

static bool pointInRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

static std::string fitCenterText(const std::string& text, int max_width)
{
    if (max_width <= 0 || text.empty()) {
        return "";
    }
    if (GetHAL().display.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    static constexpr const char* kEllipsis = "...";
    if (GetHAL().display.textWidth(kEllipsis) > max_width) {
        return "";
    }

    std::string clipped = text;
    while (!clipped.empty()) {
        clipped.pop_back();
        const std::string candidate = clipped + kEllipsis;
        if (GetHAL().display.textWidth(candidate.c_str()) <= max_width) {
            return candidate;
        }
    }
    return kEllipsis;
}

void AppLora::onCreate()
{
    setAppInfo().name = "AppLora";
    open();
}

void AppLora::onOpen()
{
    drawLora();
}

void AppLora::onRunning()
{
    if (_keyboard.isOpen()) {
        handleKeyboard();
        return;
    }

    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const auto now_ms = GetHAL().millis();
    if (!_lora_init_attempted) {
        _lora_init_attempted = true;
        if (!GetHAL().initLora()) {
            _rx_text = "INIT FAIL";
            drawReceive();
        }
    }

    LoraEvent lora_event = {};
    if (GetHAL().pollLora(lora_event)) {
        if (lora_event.received) {
            _rx_text = lora_event.text.empty() ? "(EMPTY)" : lora_event.text;
            GetHAL().playBuzzerPattern(LORA_RX_TONES, sizeof(LORA_RX_TONES) / sizeof(LORA_RX_TONES[0]));
            drawReceive();
        }
        if (lora_event.transmit_done) {
            finishSend(!lora_event.error);
        } else if (lora_event.error && _sending) {
            finishSend(false);
        }
    }

    if (_sending && now_ms - _tx_tick_ms >= LORA_TX_TICK_INTERVAL_MS) {
        _tx_tick_ms = now_ms;
        GetHAL().playBuzzerPattern(LORA_TX_TICK_TONES, sizeof(LORA_TX_TICK_TONES) / sizeof(LORA_TX_TICK_TONES[0]));
    }

    if (!_has_drawn || GetHAL().isRefreshRequested()) {
        drawLora();
    }

    if (!GetHAL().isTouchPressed()) {
        return;
    }
    const auto& touch = GetHAL().getTouchDetail();
    if (!touch.wasPressed() || now_ms - _last_touch_ms < LORA_TOUCH_COOLDOWN_MS) {
        return;
    }

    const auto point = M5.Touch.getTouchPointRaw();
    if (pointInRect(point.x, point.y, LORA_TX_X, LORA_TX_Y, LORA_TX_W, LORA_TX_H)) {
        _last_touch_ms = now_ms;
        openKeyboard();
    } else if (pointInRect(point.x, point.y, LORA_SEND_X, LORA_SEND_Y, LORA_SEND_W, LORA_SEND_H)) {
        _last_touch_ms = now_ms;
        startSend();
    }
}

void AppLora::drawLora()
{
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    drawReceive();
    drawInput();
    drawSendButton(_sending);
    _has_drawn = true;
}

void AppLora::drawReceive()
{
    auto& display = GetHAL().display;
    display.fillRect(LORA_RX_X + 2, LORA_RX_Y + 2, LORA_RX_W - 4, LORA_RX_H - 4, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setTextSize(1.0f);
    display.setFont(&fonts::lvFontMontserrat24);
    display.setClipRect(LORA_RX_X + 6, LORA_RX_Y + 2, LORA_RX_W - 12, LORA_RX_H - 4);
    display.drawString(fitCenterText(_rx_text, LORA_RX_W - 12).c_str(), LORA_RX_X + LORA_RX_W / 2,
                       LORA_RX_Y + LORA_RX_H / 2);
    display.clearClipRect();

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().prepareDisplayRefreshObservers();
        GetHAL().flushDisplay(LORA_RX_X + 2, LORA_RX_Y + 2, LORA_RX_W - 4, LORA_RX_H - 4);
        display.waitDisplay();
    }
}

void AppLora::drawInput(bool immediate_refresh)
{
    auto& display = GetHAL().display;
    display.fillRect(LORA_TX_X + 2, LORA_TX_Y + 2, LORA_TX_W - 4, LORA_TX_H - 4, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setTextSize(1.0f);
    display.setFont(&fonts::lvFontMontserrat24);
    display.setClipRect(LORA_TX_X + 6, LORA_TX_Y + 2, LORA_TX_W - 12, LORA_TX_H - 4);
    display.drawString(fitCenterText(_tx_text, LORA_TX_W - 12).c_str(), LORA_TX_X + LORA_TX_W / 2,
                       LORA_TX_Y + LORA_TX_H / 2);
    display.clearClipRect();

    if (immediate_refresh && !GetHAL().isRefreshRequested()) {
        GetHAL().prepareDisplayRefreshObservers();
        GetHAL().flushDisplay(LORA_TX_X + 2, LORA_TX_Y + 2, LORA_TX_W - 4, LORA_TX_H - 4);
        display.waitDisplay();
    }
}

void AppLora::drawSendButton(bool active)
{
    (void)active;
}

void AppLora::openKeyboard()
{
    GetHAL().setUiModalActive(true);
    _keyboard.open("LoRa Tx", _tx_text, false, 64);
}

void AppLora::handleKeyboard()
{
    GetHAL().setUiModalActive(true);
    auto commitAndCloseKeyboard = [&]() {
        _tx_text = _keyboard.text();
        GetHAL().drawBackground(m5gfx::epd_mode_t::epd_fastest, false);
        _has_drawn = false;
        GetHAL().releaseModalNextFrame();
    };

    const KeyboardEvent event = _keyboard.update(false);
    if (event == KeyboardEvent::Closed) {
        commitAndCloseKeyboard();
        return;
    }
    if (event == KeyboardEvent::None && GetHAL().isTouchPressed()) {
        const auto& touch = GetHAL().getTouchDetail();
        const auto point  = M5.Touch.getTouchPointRaw();
        if (touch.wasPressed() && !_keyboard.containsPoint(point.x, point.y) &&
            !pointInRect(point.x, point.y, LORA_TX_X, LORA_TX_Y, LORA_TX_W, LORA_TX_H)) {
            _keyboard.close();
            commitAndCloseKeyboard();
        }
        return;
    }
    if (event != KeyboardEvent::Changed) {
        return;
    }

    _tx_text = _keyboard.text();
    drawInput(false);
    const int combined_y = std::min(LORA_TX_Y, _keyboard.refreshTop());
    GetHAL().prepareDisplayRefreshObservers();
    GetHAL().flushDisplay(0, combined_y, GetHAL().display.width(), GetHAL().display.height() - combined_y);
    GetHAL().display.waitDisplay();
}

void AppLora::startSend()
{
    if (_sending) {
        return;
    }

    _sending    = true;
    _tx_tick_ms = GetHAL().millis();
    GetHAL().playBuzzerPattern(LORA_SEND_START_TONES, sizeof(LORA_SEND_START_TONES) / sizeof(LORA_SEND_START_TONES[0]));
    ESP_LOGI(APP_LORA_TAG, "TX requested: len=%u text=\"%s\"", static_cast<unsigned>(_tx_text.size()),
             _tx_text.c_str());
    if (!GetHAL().startLoraTransmit(_tx_text)) {
        ESP_LOGW(APP_LORA_TAG, "TX start failed");
        finishSend(false);
        return;
    }
    drawSendButton(true);
}

void AppLora::finishSend(bool success)
{
    _sending = false;
    if (success) {
        ESP_LOGI(APP_LORA_TAG, "TX success");
        GetHAL().playBuzzerPattern(LORA_SEND_DONE_TONES,
                                   sizeof(LORA_SEND_DONE_TONES) / sizeof(LORA_SEND_DONE_TONES[0]));
    } else {
        ESP_LOGW(APP_LORA_TAG, "TX failed");
        GetHAL().playBuzzerPattern(LORA_SEND_FAIL_TONES,
                                   sizeof(LORA_SEND_FAIL_TONES) / sizeof(LORA_SEND_FAIL_TONES[0]));
        _rx_text = "SEND FAIL";
        drawReceive();
    }
    drawSendButton(false);
}

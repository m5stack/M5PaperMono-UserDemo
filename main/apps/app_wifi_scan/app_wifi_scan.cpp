/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <WiFi.h>
#include <algorithm>
#include <cstdio>
#include <hal.h>

static constexpr std::uint32_t WIFI_TOUCH_COOLDOWN_MS = 300;

static constexpr int WIFI_SCAN_X      = 372;
static constexpr int WIFI_SCAN_Y      = 221;
static constexpr int WIFI_SCAN_W      = 476 - 372;
static constexpr int WIFI_SCAN_H      = 269 - 221;
static constexpr int WIFI_LITE_SCAN_X = 374;
static constexpr int WIFI_LITE_SCAN_Y = 359;
static constexpr int WIFI_LITE_SCAN_W = 476 - 374;
static constexpr int WIFI_LITE_SCAN_H = 405 - 359;

static constexpr int WIFI_CLEAR_X      = 372;
static constexpr int WIFI_CLEAR_Y      = 271;
static constexpr int WIFI_CLEAR_W      = 476 - 372;
static constexpr int WIFI_CLEAR_H      = 319 - 271;
static constexpr int WIFI_LITE_CLEAR_X = 265;
static constexpr int WIFI_LITE_CLEAR_Y = 359;
static constexpr int WIFI_LITE_CLEAR_W = 366 - 265;
static constexpr int WIFI_LITE_CLEAR_H = 405 - 359;

static constexpr int WIFI_INFO_X      = 126;
static constexpr int WIFI_INFO_Y      = 209;
static constexpr int WIFI_INFO_W      = 364 - 126;
static constexpr int WIFI_INFO_H      = 323 - 209;
static constexpr int WIFI_LITE_INFO_X = 122;
static constexpr int WIFI_LITE_INFO_Y = 45;
static constexpr int WIFI_LITE_INFO_W = 467 - 122;
static constexpr int WIFI_LITE_INFO_H = 360 - 45;

static constexpr int WIFI_ROW_H          = 20;
static constexpr int WIFI_LIST_PADDING_Y = 7;
static constexpr int WIFI_SCROLL_W       = 3;
static constexpr int WIFI_RSSI_W         = 52;
static constexpr int WIFI_SECURE_W       = 12;

struct WifiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

static WifiRect wifiScanRect()
{
    if (GetHAL().isLiteModel()) {
        return {WIFI_LITE_SCAN_X, WIFI_LITE_SCAN_Y, WIFI_LITE_SCAN_W, WIFI_LITE_SCAN_H};
    }
    return {WIFI_SCAN_X, WIFI_SCAN_Y, WIFI_SCAN_W, WIFI_SCAN_H};
}

static WifiRect wifiClearRect()
{
    if (GetHAL().isLiteModel()) {
        return {WIFI_LITE_CLEAR_X, WIFI_LITE_CLEAR_Y, WIFI_LITE_CLEAR_W, WIFI_LITE_CLEAR_H};
    }
    return {WIFI_CLEAR_X, WIFI_CLEAR_Y, WIFI_CLEAR_W, WIFI_CLEAR_H};
}

static WifiRect wifiInfoRect()
{
    if (GetHAL().isLiteModel()) {
        return {WIFI_LITE_INFO_X, WIFI_LITE_INFO_Y, WIFI_LITE_INFO_W, WIFI_LITE_INFO_H};
    }
    return {WIFI_INFO_X, WIFI_INFO_Y, WIFI_INFO_W, WIFI_INFO_H};
}

static constexpr BuzzerTone WIFI_SCAN_START_TONES[] = {
    {.start_freq_hz = 1800, .end_freq_hz = 1800, .duration_ms = 7, .gap_ms = 18},
    {.start_freq_hz = 2200, .end_freq_hz = 2200, .duration_ms = 7, .gap_ms = 18},
    {.start_freq_hz = 2600, .end_freq_hz = 2600, .duration_ms = 10, .gap_ms = 0},
};

static constexpr BuzzerTone WIFI_CLEAR_TONES[] = {
    {.start_freq_hz = 1050, .end_freq_hz = 1050, .duration_ms = 9, .gap_ms = 4},
    {.start_freq_hz = 650, .end_freq_hz = 650, .duration_ms = 16, .gap_ms = 0},
};

static constexpr BuzzerTone WIFI_SCAN_DONE_TONES[] = {
    {.start_freq_hz = 1250, .end_freq_hz = 1250, .duration_ms = 9, .gap_ms = 5},
    {.start_freq_hz = 1850, .end_freq_hz = 1850, .duration_ms = 9, .gap_ms = 5},
    {.start_freq_hz = 2450, .end_freq_hz = 2450, .duration_ms = 16, .gap_ms = 0},
};

static constexpr BuzzerTone WIFI_SCAN_FAIL_TONES[] = {
    {.start_freq_hz = 900, .end_freq_hz = 900, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 520, .end_freq_hz = 520, .duration_ms = 18, .gap_ms = 0},
};

static void drawSecureMark(int x, int row_y)
{
    static constexpr int MARK_SIZE = 5;
    GetHAL().display.fillRect(x + 2, row_y + (WIFI_ROW_H - MARK_SIZE) / 2, MARK_SIZE, MARK_SIZE, TFT_BLACK);
}

static std::string fitSsidText(const std::string& ssid, int max_width)
{
    std::string text = ssid.empty() ? "<hidden>" : ssid;
    if (GetHAL().display.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    while (text.size() > 1) {
        text.pop_back();
        const std::string clipped = text + ".";
        if (GetHAL().display.textWidth(clipped.c_str()) <= max_width) {
            return clipped;
        }
    }
    return ".";
}

void AppWifiScan::onCreate()
{
    setAppInfo().name = "AppWifiScan";
    open();
}

void AppWifiScan::onOpen()
{
    drawWifi();
}

void AppWifiScan::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const auto now_ms       = GetHAL().millis();
    const bool accept_input = now_ms - _last_input_ms >= WIFI_TOUCH_COOLDOWN_MS;

    if (!_auto_scan_started) {
        _auto_scan_started = true;
        startScan();
        return;
    }

    const WifiRect scan_rect  = wifiScanRect();
    const WifiRect clear_rect = wifiClearRect();
    if (accept_input && GetHAL().wasTouchClickedArea(scan_rect.x, scan_rect.y, scan_rect.w, scan_rect.h)) {
        startScan(true);
        _last_input_ms = now_ms;
        return;
    }

    if (accept_input && GetHAL().wasTouchClickedArea(clear_rect.x, clear_rect.y, clear_rect.w, clear_rect.h)) {
        GetHAL().playBuzzerPattern(WIFI_CLEAR_TONES, 2);
        clearResults();
        _last_input_ms = now_ms;
        return;
    }

    if (_scan_running) {
        const int scan_count = WiFi.scanComplete();
        if (scan_count >= 0) {
            finishScan(scan_count);
            return;
        }
        if (scan_count == WIFI_SCAN_FAILED) {
            _scan_running = false;
            GetHAL().playBuzzerPattern(WIFI_SCAN_FAIL_TONES, 2);
            drawStatusText("SCAN FAILED");
            return;
        }
    }

    if (!_has_drawn || GetHAL().isFullRedrawRequested()) {
        drawWifi();
    }
}

void AppWifiScan::startScan(bool play_sound)
{
    if (_scan_running) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.scanDelete();
    _scan_running = WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING;
    if (_scan_running) {
        if (play_sound) {
            GetHAL().playBuzzerPattern(WIFI_SCAN_START_TONES, 3);
        }
        drawStatusText("SCANNING...");
    } else {
        if (play_sound) {
            GetHAL().playBuzzerPattern(WIFI_SCAN_FAIL_TONES, 2);
        }
        drawStatusText("SCAN START FAIL");
    }
}

void AppWifiScan::finishScan(int count)
{
    _networks.clear();
    _networks.reserve(std::max(count, 0));

    for (int i = 0; i < count; ++i) {
        WifiNetwork network;
        network.ssid   = WiFi.SSID(i).c_str();
        network.rssi   = WiFi.RSSI(i);
        network.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
        _networks.push_back(network);
    }

    std::sort(_networks.begin(), _networks.end(),
              [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });

    WiFi.scanDelete();
    _scan_running = false;
    GetHAL().playBuzzerPattern(WIFI_SCAN_DONE_TONES, 3);
    drawWifi();
}

void AppWifiScan::clearResults()
{
    if (!_scan_running && _networks.empty()) {
        return;
    }

    if (_scan_running) {
        WiFi.scanDelete();
        _scan_running = false;
    }

    _networks.clear();
    GetHAL().requestRefresh();
    drawWifi();
}

std::int16_t AppWifiScan::visibleRows() const
{
    const WifiRect info = wifiInfoRect();
    return (info.h - WIFI_LIST_PADDING_Y * 2) / WIFI_ROW_H;
}

void AppWifiScan::drawStatusText(const char* text)
{
    const WifiRect info = wifiInfoRect();
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(info.x, info.y, info.w, info.h, TFT_WHITE);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);
    GetHAL().display.drawString(text, info.x + info.w / 2, info.y + info.h / 2);

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(info.x, info.y, info.w, info.h);
        GetHAL().display.waitDisplay();
    }

    _has_drawn = true;
}

void AppWifiScan::drawWifi()
{
    const WifiRect info = wifiInfoRect();
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(info.x, info.y, info.w, info.h, TFT_WHITE);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextDatum(top_left);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);

    if (_networks.empty()) {
        GetHAL().display.setTextDatum(middle_center);
        GetHAL().display.drawString(_scan_running ? "SCANNING..." : "NO DATA", info.x + info.w / 2,
                                    info.y + info.h / 2);
    } else {
        const int rows = visibleRows();

        for (int row = 0; row < rows; ++row) {
            const int index = row;
            if (index >= static_cast<int>(_networks.size())) {
                break;
            }

            const auto& network    = _networks[index];
            const int row_y        = info.y + WIFI_LIST_PADDING_Y + row * WIFI_ROW_H;
            const int row_center_y = row_y + WIFI_ROW_H / 2;
            const int rssi_right   = info.x + info.w - WIFI_SCROLL_W - 4;
            const int ssid_x       = info.x + WIFI_SECURE_W;
            const int ssid_w       = rssi_right - WIFI_RSSI_W - ssid_x - 4;
            const std::string ssid = fitSsidText(network.ssid, ssid_w);
            char rssi_text[16]     = {};
            snprintf(rssi_text, sizeof(rssi_text), "%lddBm", static_cast<long>(network.rssi));

            GetHAL().display.setTextDatum(top_left);
            if (network.secure) {
                drawSecureMark(info.x, row_y);
            }
            GetHAL().display.setTextDatum(middle_left);
            GetHAL().display.drawString(ssid.c_str(), ssid_x, row_center_y);
            GetHAL().display.setTextDatum(middle_right);
            GetHAL().display.drawString(rssi_text, rssi_right, row_center_y);
        }
    }

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(info.x, info.y, info.w, info.h);
        GetHAL().display.waitDisplay();
    }

    _has_drawn = true;
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <cstdio>
#include <hal.h>

#define APP_IMU_REFRESH_MS                30000
#define APP_IMU_SAMPLE_MS                 100
#define APP_IMU_TOUCH_REFRESH_COOLDOWN_MS 300

static constexpr int IMU_X = 75;
static constexpr int IMU_Y = 543;
static constexpr int IMU_W = 220 - 75;
static constexpr int IMU_H = 632 - 543;

static constexpr int IMU_ACCEL_X              = IMU_X;
static constexpr int IMU_GYRO_X               = IMU_X + 73;
static constexpr int IMU_ACCEL_W              = IMU_GYRO_X - IMU_ACCEL_X;
static constexpr int IMU_GYRO_W               = IMU_X + IMU_W - IMU_GYRO_X;
static constexpr int IMU_ROW_0_Y              = IMU_Y + 15;
static constexpr int IMU_ROW_GAP              = 29;
static constexpr BuzzerTone IMU_TOUCH_TONES[] = {
    {.start_freq_hz = 1200, .end_freq_hz = 1200, .duration_ms = 8, .gap_ms = 5},
    {.start_freq_hz = 1700, .end_freq_hz = 1700, .duration_ms = 10, .gap_ms = 0},
};

static void formatImuText(char* dest, std::size_t dest_size, const char* label, float value)
{
    char value_text[6] = {};
    snprintf(value_text, sizeof(value_text), "%.1f", value);
    snprintf(dest, dest_size, "%s %s", label, value_text);
}

static void drawImuText(int x, int y, int w, const char* text)
{
    GetHAL().display.fillRect(x, y - IMU_ROW_GAP / 2, w, IMU_ROW_GAP, TFT_WHITE);
    GetHAL().display.setClipRect(x, IMU_Y, w, IMU_H);
    GetHAL().display.drawString(text, x, y);
    GetHAL().display.clearClipRect();
}

static void drawImuRow(int row, const char* accel_text, const char* gyro_text)
{
    const int y = IMU_ROW_0_Y + row * IMU_ROW_GAP;
    drawImuText(IMU_ACCEL_X, y, IMU_ACCEL_W, accel_text);
    drawImuText(IMU_GYRO_X, y, IMU_GYRO_W, gyro_text);
}

void AppImu::onCreate()
{
    setAppInfo().name = "AppImu";
    open();
}

void AppImu::onOpen()
{
    updateImuBuffer(true);
    drawImu();
}

void AppImu::onRunning()
{
    const auto now_ms = GetHAL().millis();
    if (!_has_sample || now_ms - _last_sample_ms >= APP_IMU_SAMPLE_MS) {
        updateImuBuffer(false);
    }

    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    bool touch_changed       = false;
    const bool touch_pressed = GetHAL().isTouchPressed();
    std::int16_t touch_x     = -1;
    std::int16_t touch_y     = -1;
    if (touch_pressed) {
        const auto raw = M5.Touch.getTouchPointRaw();
        touch_x        = static_cast<std::int16_t>(raw.x);
        touch_y        = static_cast<std::int16_t>(raw.y);
    }
    const bool touch_in_imu_area =
        touch_pressed && touch_x >= IMU_X && touch_x <= IMU_X + IMU_W && touch_y >= IMU_Y && touch_y <= IMU_Y + IMU_H;
    if (touch_pressed != _last_touch_pressed || touch_x != _last_touch_x || touch_y != _last_touch_y) {
        touch_changed = touch_in_imu_area && now_ms - _last_touch_refresh_ms >= APP_IMU_TOUCH_REFRESH_COOLDOWN_MS;
        if (touch_changed && !_last_touch_pressed) {
            GetHAL().playBuzzerPattern(IMU_TOUCH_TONES, sizeof(IMU_TOUCH_TONES) / sizeof(IMU_TOUCH_TONES[0]));
        }
        _last_touch_pressed = touch_pressed;
        _last_touch_x       = touch_x;
        _last_touch_y       = touch_y;
    }

    if (!_has_drawn || GetHAL().isFullRedrawRequested() || touch_changed ||
        now_ms - _last_update_ms >= APP_IMU_REFRESH_MS) {
        drawImu();
        if (touch_changed) {
            _last_touch_refresh_ms = now_ms;
        }
    }
}

void AppImu::prepareForDisplayRefresh()
{
    if (GetHAL().isUiModalActive()) {
        return;
    }

    drawImu();
}

void AppImu::updateImuBuffer(bool force)
{
    const auto now_ms = GetHAL().millis();
    if (!force && _has_sample && now_ms - _last_sample_ms < APP_IMU_SAMPLE_MS) {
        return;
    }

    if (GetHAL().imu.isEnabled()) {
        GetHAL().imu.update();
        const auto data = GetHAL().imu.getImuData();
        formatImuText(_accel_text[0], sizeof(_accel_text[0]), "AX", data.accel.x);
        formatImuText(_accel_text[1], sizeof(_accel_text[1]), "AY", data.accel.y);
        formatImuText(_accel_text[2], sizeof(_accel_text[2]), "AZ", data.accel.z);
        formatImuText(_gyro_text[0], sizeof(_gyro_text[0]), "GX", data.gyro.x);
        formatImuText(_gyro_text[1], sizeof(_gyro_text[1]), "GY", data.gyro.y);
        formatImuText(_gyro_text[2], sizeof(_gyro_text[2]), "GZ", data.gyro.z);
    } else {
        snprintf(_accel_text[0], sizeof(_accel_text[0]), "AX --.-");
        snprintf(_accel_text[1], sizeof(_accel_text[1]), "AY --.-");
        snprintf(_accel_text[2], sizeof(_accel_text[2]), "AZ --.-");
        snprintf(_gyro_text[0], sizeof(_gyro_text[0]), "GX --.-");
        snprintf(_gyro_text[1], sizeof(_gyro_text[1]), "GY --.-");
        snprintf(_gyro_text[2], sizeof(_gyro_text[2]), "GZ --.-");
    }

    _last_sample_ms = now_ms;
    _has_sample     = true;
}

void AppImu::drawImu()
{
    updateImuBuffer(false);

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(IMU_X, IMU_Y, IMU_W, IMU_H, TFT_WHITE);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextDatum(middle_left);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);
    GetHAL().display.setClipRect(IMU_X, IMU_Y, IMU_W, IMU_H);
    drawImuRow(0, _accel_text[0], _gyro_text[0]);
    drawImuRow(1, _accel_text[1], _gyro_text[1]);
    drawImuRow(2, _accel_text[2], _gyro_text[2]);
    GetHAL().display.clearClipRect();
    GetHAL().display.setTextSize(1.0f);

    if (!GetHAL().isRefreshRequested() && !GetHAL().isPreparingDisplayRefreshObservers()) {
        GetHAL().flushDisplay(IMU_X, IMU_Y, IMU_W, IMU_H);
        GetHAL().display.waitDisplay();
    }

    _last_update_ms = GetHAL().millis();
    _has_drawn      = true;
}

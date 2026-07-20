/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <assets.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal.h>

static constexpr int SHUTDOWN_X = 366;
static constexpr int SHUTDOWN_Y = 434;
static constexpr int SHUTDOWN_W = 473 - 366;
static constexpr int SHUTDOWN_H = 475 - 434;

static constexpr std::uint32_t SHUTDOWN_TOUCH_COOLDOWN_MS = 350;
static constexpr std::uint32_t SHUTDOWN_IMAGE_HOLD_MS     = 250;
static constexpr std::uint32_t SHUTDOWN_LOGO_HOLD_MS      = 250;

static constexpr BuzzerTone SHUTDOWN_PRESS_TONES[] = {
    {.start_freq_hz = 1040, .end_freq_hz = 1040, .duration_ms = 18, .gap_ms = 4},
    {.start_freq_hz = 760, .end_freq_hz = 560, .duration_ms = 24, .gap_ms = 0},
};

static constexpr BuzzerTone SHUTDOWN_SINK_TONES[] = {
    {.start_freq_hz = 1120, .end_freq_hz = 1120, .duration_ms = 18, .gap_ms = 6},
    {.start_freq_hz = 840, .end_freq_hz = 500, .duration_ms = 42, .gap_ms = 4},
    {.start_freq_hz = 400, .end_freq_hz = 200, .duration_ms = 72, .gap_ms = 0},
};

void AppShutdown::onCreate()
{
    setAppInfo().name = "AppShutdown";
    open();
}

void AppShutdown::onOpen()
{
    _has_drawn = true;
}

void AppShutdown::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const std::uint32_t now_ms = GetHAL().millis();
    if (now_ms - _last_input_ms < SHUTDOWN_TOUCH_COOLDOWN_MS) {
        return;
    }

    if (GetHAL().wasTouchClickedArea(SHUTDOWN_X, SHUTDOWN_Y, SHUTDOWN_W, SHUTDOWN_H)) {
        _last_input_ms = now_ms;

        GetHAL().stopBuzzer();
        GetHAL().playBuzzerPattern(SHUTDOWN_PRESS_TONES,
                                   sizeof(SHUTDOWN_PRESS_TONES) / sizeof(SHUTDOWN_PRESS_TONES[0]));

        drawShutdownPressed();
        enterShutdown();
        return;
    }

    if (!_has_drawn || GetHAL().isRefreshRequested()) {
        _has_drawn = true;
    }
}

void AppShutdown::drawShutdownPressed()
{
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.drawPng(img_shutdown_start, img_shutdown_end - img_shutdown_start, SHUTDOWN_X, SHUTDOWN_Y);

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(SHUTDOWN_X, SHUTDOWN_Y, SHUTDOWN_W, SHUTDOWN_H);
        GetHAL().display.waitDisplay();
    }

    _has_drawn = true;
}

void AppShutdown::drawShutdownCleanScreen()
{
    auto& display = GetHAL().display;

    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.fillScreen(TFT_BLACK);
    GetHAL().flushDisplay();
    display.waitDisplay();

    display.fillScreen(TFT_WHITE);
    GetHAL().flushDisplay();
    display.waitDisplay();
}

void AppShutdown::drawShutdownLogo()
{
    auto& display                  = GetHAL().display;
    const bool lite_model          = GetHAL().isLiteModel();
    const std::uint8_t* logo_start = lite_model ? img_logo_lite_start : img_logo_start;
    const std::uint8_t* logo_end   = lite_model ? img_logo_lite_end : img_logo_end;

    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.fillScreen(TFT_WHITE);
    display.drawPng(logo_start, logo_end - logo_start, 0, 0);
    GetHAL().flushDisplay();
    display.waitDisplay();
}

void AppShutdown::enterShutdown()
{
    GetHAL().stopBuzzer();
    GetHAL().unmountTfCard();

    vTaskDelay(pdMS_TO_TICKS(SHUTDOWN_IMAGE_HOLD_MS));

    std::uint32_t total_ms = 0;
    for (std::size_t i = 0; i < sizeof(SHUTDOWN_SINK_TONES) / sizeof(SHUTDOWN_SINK_TONES[0]); ++i) {
        total_ms += SHUTDOWN_SINK_TONES[i].duration_ms;
        if (i + 1 < sizeof(SHUTDOWN_SINK_TONES) / sizeof(SHUTDOWN_SINK_TONES[0])) {
            total_ms += SHUTDOWN_SINK_TONES[i].gap_ms;
        }
    }

    drawShutdownCleanScreen();
    drawShutdownLogo();
    vTaskDelay(pdMS_TO_TICKS(SHUTDOWN_LOGO_HOLD_MS));

    GetHAL().playBuzzerPattern(SHUTDOWN_SINK_TONES, sizeof(SHUTDOWN_SINK_TONES) / sizeof(SHUTDOWN_SINK_TONES[0]));
    vTaskDelay(pdMS_TO_TICKS(total_ms + 20));
    GetHAL().stopBuzzer();

    gpio_set_direction(GPIO_NUM_42, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_NUM_42, 0);
    GetHAL().display.setBrightness(0);

    vTaskDelay(pdMS_TO_TICKS(100));

    auto& pm1 = GetHAL().pm1;
    pm1.ldoSetPowerHold(false);
    pm1.shutdown();
}

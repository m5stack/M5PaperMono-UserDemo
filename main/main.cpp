/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <M5Unified.h>
#include <apps.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal.h>
#include <memory>
#include <mooncake.h>

static constexpr const char* APP_TAG          = "PaperMono";
static constexpr const char* FIRMWARE_VERSION = "V1.2";

static void drawStartupBackground()
{
    if (GetHAL().drawBackground(m5gfx::epd_mode_t::epd_fast, false)) {
        GetHAL().requestFullRedraw();
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(APP_TAG, "Firmware version: %s", FIRMWARE_VERSION);

    // Wait for M5IOE1 and board I2C peripherals to become stable after power-up.
    vTaskDelay(pdMS_TO_TICKS(500));

    if (!GetHAL().init()) {
        while (true) {
            GetHAL().delay(1000);
        }
    }

    // Build the complete startup frame in memory before the first panel update.
    drawStartupBackground();

    mooncake::GetMooncake().installApp(std::make_unique<AppBrightness>());
    mooncake::GetMooncake().installApp(std::make_unique<AppClock>());
    mooncake::GetMooncake().installApp(std::make_unique<AppBattery>());

    auto imu_app = std::make_unique<AppImu>();
    GetHAL().registerDisplayRefreshObserver(imu_app.get());
    mooncake::GetMooncake().installApp(std::move(imu_app));

    auto mic_app = std::make_unique<AppMicWave>();
    GetHAL().registerDisplayRefreshObserver(mic_app.get());
    mooncake::GetMooncake().installApp(std::move(mic_app));

    mooncake::GetMooncake().installApp(std::make_unique<AppRgb>());
    mooncake::GetMooncake().installApp(std::make_unique<AppBuzzer>());
    mooncake::GetMooncake().installApp(std::make_unique<AppSleepWake>());
    mooncake::GetMooncake().installApp(std::make_unique<AppShutdown>());
    mooncake::GetMooncake().installApp(std::make_unique<AppWifiScan>());
    if (GetHAL().hasNfcHardware()) {
        mooncake::GetMooncake().installApp(std::make_unique<AppNfcScan>());
        mooncake::GetMooncake().installApp(std::make_unique<AppLora>());
    }
    mooncake::GetMooncake().installApp(std::make_unique<AppTfCard>());

    // Drive every app through onCreate/onOpen so its initial data joins the
    // background in the first panel update.
    mooncake::GetMooncake().update();
    GetHAL().prepareDisplayRefreshObservers();
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fast);
    GetHAL().flushDisplay();
    GetHAL().display.waitDisplay();
    GetHAL().clearRefreshRequest();
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().endFrame();

    while (true) {
        GetHAL().beginFrame();
        M5.update();
        mooncake::GetMooncake().update();
        if (GetHAL().isRefreshRequested()) {
            if (GetHAL().isFullRedrawRequested()) {
                GetHAL().prepareDisplayRefreshObservers();
                GetHAL().display.setEpdMode(GetHAL().fullRedrawMode());
            } else {
                GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
            }
            GetHAL().flushDisplay();
            GetHAL().display.waitDisplay();
            GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
        }
        GetHAL().clearRefreshRequest();
        GetHAL().endFrame();
        GetHAL().feedTheDog();
    }
}

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
static constexpr const char* FIRMWARE_VERSION = "V1.0";

static void drawStartupBackground()
{
    if (GetHAL().drawBackground(m5gfx::epd_mode_t::epd_quality, false)) {
        GetHAL().requestRefresh();
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

    drawStartupBackground();

    while (true) {
        GetHAL().beginFrame();
        M5.update();
        mooncake::GetMooncake().update();
        if (GetHAL().isRefreshRequested()) {
            GetHAL().prepareDisplayRefreshObservers();
            GetHAL().flushDisplay();
            GetHAL().display.waitDisplay();
        }
        GetHAL().clearRefreshRequest();
        GetHAL().feedTheDog();
    }
}

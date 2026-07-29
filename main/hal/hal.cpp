/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

static std::unique_ptr<Hal> s_hal;
static constexpr const char* HAL_TAG = "HAL";
static bool s_gpio_isr_service_ready = false;

bool halInitGpioIsrService()
{
    if (s_gpio_isr_service_ready) {
        return true;
    }

    const esp_err_t err = gpio_install_isr_service(0);
    if (err == ESP_OK || err == ESP_ERR_INVALID_STATE) {
        s_gpio_isr_service_ready = true;
        return true;
    }

    ESP_LOGW(HAL_TAG, "GPIO ISR service install failed: %s", esp_err_to_name(err));
    return false;
}

bool halIsGpioIsrServiceReady()
{
    return s_gpio_isr_service_ready;
}

Hal& GetHAL()
{
    if (!s_hal) {
        s_hal = std::make_unique<Hal>();
    }
    return *s_hal;
}

/* -------------------------------------------------------------------------- */
/*                                    Init                                    */
/* -------------------------------------------------------------------------- */
bool Hal::init()
{
    halInitGpioIsrService();
    initArduino();

    auto cfg          = M5.config();
    cfg.clear_display = false;
    cfg.internal_mic  = false;
    cfg.internal_spk  = false;
    cfg.internal_imu  = false;
    ESP_LOGI(HAL_TAG, "M5.begin start");
    M5.begin(cfg);
    ESP_LOGI(HAL_TAG, "M5.begin done board=%d display_count=%d", static_cast<int>(M5.getBoard()),
             static_cast<int>(M5.getDisplayCount()));

    if (M5.getDisplayCount() == 0) {
        ESP_LOGE(HAL_TAG, "Display init failed: count=0 board=%d", static_cast<int>(M5.getBoard()));
        return false;
    }

    display.setRotation(0);
    display.setAutoDisplay(false);

    if (display.width() <= 0 || display.height() <= 0) {
        ESP_LOGE(HAL_TAG, "Display init failed: count=%d size=%dx%d", static_cast<int>(M5.getDisplayCount()),
                 display.width(), display.height());
        return false;
    }

    _rgb_ready = initRgb();
    if (!_rgb_ready) {
        ESP_LOGW(HAL_TAG, "M5PM1/M5IOE1 init failed; RGB/NFC/TF controls are disabled");
    } else {
        detectBoardVariant();
    }

    if (!initImu()) {
        ESP_LOGW(HAL_TAG, "IMU init failed");
    }

    _buzzer_ready = initBuzzer();
    if (!_buzzer_ready) {
        ESP_LOGW(HAL_TAG, "Buzzer init failed");
    }

    return true;
}

/* -------------------------------------------------------------------------- */
/*                                   System                                   */
/* -------------------------------------------------------------------------- */
void Hal::delay(std::uint32_t ms)
{
    m5gfx::delay(ms);
}

std::uint32_t Hal::millis()
{
    return m5gfx::millis();
}

std::int16_t Hal::getBatteryVoltageMv()
{
    return M5.Power.getBatteryVoltage();
}

void Hal::feedTheDog()
{
    vTaskDelay(pdMS_TO_TICKS(5));
}

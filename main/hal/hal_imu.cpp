/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <M5Unified.h>
#include <esp_log.h>

static constexpr const char* HAL_IMU_TAG              = "HAL_IMU";
static constexpr std::uint8_t IMU_INIT_RETRIES        = 3;
static constexpr std::uint32_t IMU_READY_TIMEOUT_MS   = 500;
static constexpr std::uint32_t IMU_RETRY_DELAY_MS     = 150;
static constexpr std::uint32_t IMU_SAMPLE_INTERVAL_MS = 10;

/* -------------------------------------------------------------------------- */
/*                                     IMU                                    */
/* -------------------------------------------------------------------------- */
bool Hal::initImu()
{
    constexpr auto required_sensors =
        static_cast<m5::IMU_Class::sensor_mask_t>(m5::IMU_Class::sensor_mask_accel | m5::IMU_Class::sensor_mask_gyro);

    for (std::uint8_t attempt = 1; attempt <= IMU_INIT_RETRIES; ++attempt) {
        ESP_LOGI(HAL_IMU_TAG, "IMU init attempt %u/%u", attempt, IMU_INIT_RETRIES);
        const bool detected = imu.begin(&M5.In_I2C, M5.getBoard());
        auto observed       = m5::IMU_Class::sensor_mask_none;
        const auto start_ms = millis();

        while (detected && millis() - start_ms < IMU_READY_TIMEOUT_MS) {
            observed = static_cast<m5::IMU_Class::sensor_mask_t>(observed | imu.update());
            if ((observed & required_sensors) == required_sensors) {
                ESP_LOGI(HAL_IMU_TAG, "IMU ready type=%d attempt=%u", static_cast<int>(imu.getType()), attempt);
                return true;
            }
            delay(IMU_SAMPLE_INTERVAL_MS);
        }

        ESP_LOGW(HAL_IMU_TAG, "IMU not ready type=%d detected=%d mask=0x%02X", static_cast<int>(imu.getType()),
                 detected, static_cast<unsigned>(observed));
        if (attempt < IMU_INIT_RETRIES) {
            delay(IMU_RETRY_DELAY_MS);
        }
    }
    return false;
}

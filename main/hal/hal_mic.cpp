/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <M5Unified.h>
#include <driver/gpio.h>
#include <esp_log.h>

static constexpr const char* HAL_TAG              = "HAL";
static constexpr std::uint8_t PDM_EN_PIN          = M5IOE1_PIN_12;
static constexpr std::uint32_t MIC_SAMPLE_RATE_HZ = 16000;
static constexpr i2s_port_t MIC_I2S_PORT          = I2S_NUM_0;
static constexpr gpio_num_t MIC_PDM_DATA_PIN      = GPIO_NUM_46;
static constexpr gpio_num_t MIC_PDM_CLK_PIN       = GPIO_NUM_45;

/* -------------------------------------------------------------------------- */
/*                                  Microphone                                */
/* -------------------------------------------------------------------------- */
bool Hal::setPdmPower(bool enable)
{
    if (!_rgb_ready) {
        return false;
    }

    m5ioe1_err_t err = M5IOE1_OK;
    ioe1.digitalWriteWithRes(PDM_EN_PIN, enable ? HIGH : LOW, &err);
    return err == M5IOE1_OK;
}

int Hal::readPdmEnableLevel()
{
    if (!_rgb_ready) {
        return -1;
    }

    m5ioe1_err_t err = M5IOE1_OK;
    const int level  = ioe1.digitalReadWithRes(PDM_EN_PIN, &err);
    return err == M5IOE1_OK ? level : -1;
}

static bool waitMicIdle(std::uint32_t timeout_ms)
{
    const auto start_ms = GetHAL().millis();
    while (M5.Mic.isRecording()) {
        if (GetHAL().millis() - start_ms > timeout_ms) {
            return false;
        }
        GetHAL().delay(1);
    }
    return true;
}

static void releaseMicPins()
{
    gpio_reset_pin(MIC_PDM_DATA_PIN);
    gpio_set_direction(MIC_PDM_DATA_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(MIC_PDM_DATA_PIN, GPIO_FLOATING);
    gpio_reset_pin(MIC_PDM_CLK_PIN);
}

bool Hal::initMic()
{
    if (_mic_ready) {
        return true;
    }

    M5.Mic.end();
    setPdmPower(false);
    delay(20);
    setPdmPower(true);
    delay(20);
    releaseMicPins();

    auto mic_config          = M5.Mic.config();
    mic_config.pin_mck       = I2S_PIN_NO_CHANGE;
    mic_config.pin_bck       = I2S_PIN_NO_CHANGE;
    mic_config.pin_ws        = MIC_PDM_CLK_PIN;
    mic_config.pin_data_in   = MIC_PDM_DATA_PIN;
    mic_config.i2s_port      = MIC_I2S_PORT;
    mic_config.input_channel = m5::input_channel_t::input_only_right;
    mic_config.sample_rate   = MIC_SAMPLE_RATE_HZ;
    mic_config.over_sampling = 1;
    mic_config.magnification = 2;
    mic_config.dma_buf_len   = 128;
    mic_config.dma_buf_count = 8;
    M5.Mic.config(mic_config);

    if (!M5.Mic.begin()) {
        ESP_LOGW(HAL_TAG, "Mic begin failed pdm_en=%d port=%d data=%d clk=%d", readPdmEnableLevel(), MIC_I2S_PORT,
                 MIC_PDM_DATA_PIN, MIC_PDM_CLK_PIN);
        return false;
    }

    _mic_ready = true;
    return true;
}

bool Hal::isMicReady() const
{
    return _mic_ready;
}

bool Hal::readMicSamples(std::int16_t* buffer, std::size_t count)
{
    if (!buffer || count == 0) {
        return false;
    }
    if (!initMic()) {
        return false;
    }
    if (!M5.Mic.record(buffer, count, MIC_SAMPLE_RATE_HZ, false)) {
        return false;
    }
    return waitMicIdle(160);
}

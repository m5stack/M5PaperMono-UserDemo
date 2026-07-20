/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Arduino.h>
#include <M5Unified.h>
#include <esp_log.h>

static constexpr const char* HAL_TAG                        = "HAL";
static constexpr std::uint8_t PMIC_ADDR                     = 0x6E;
static constexpr std::uint8_t IO_EXPANDER_ADDR              = 0x4F;
static constexpr std::uint32_t RGB_I2C_FREQ_HZ              = 100000;
static constexpr std::uint8_t RX8130_ADDR                   = 0x32;
static constexpr std::uint8_t RX8130_RAM_BASE               = 0x20;
static constexpr std::uint8_t RX8130_RAM_SIZE               = 4;
static constexpr std::uint32_t RX8130_I2C_FREQ_HZ           = 400000;
static constexpr std::uint8_t NFC_ADDR                      = 0x50;
static constexpr std::uint32_t NFC_I2C_FREQ_HZ              = 400000;
static constexpr std::uint8_t NFC_READ_IC_IDENTITY_CMD      = 0x7F;
static constexpr std::uint8_t NFC_VALID_IDENTITY_TYPE       = 0x05;
static constexpr std::uint16_t RGB_PWM_FREQ_HZ              = 5000;
static constexpr std::uint8_t RGB_GREEN_PIN                 = M5IOE1_PIN_8;
static constexpr std::uint8_t RGB_BLUE_PIN                  = M5IOE1_PIN_9;
static constexpr std::uint8_t RGB_GREEN_PWM_CH              = M5IOE1_PWM_CH2;
static constexpr std::uint8_t RGB_BLUE_PWM_CH               = M5IOE1_PWM_CH1;
static constexpr std::uint8_t NFC_EN_PIN                    = M5IOE1_PIN_4;
static constexpr std::uint8_t PDM_EN_PIN                    = M5IOE1_PIN_12;
static constexpr std::uint8_t BOARD_PERIPHERAL_INIT_RETRIES = 3;

/* -------------------------------------------------------------------------- */
/*                                   PMIC/IOE                                 */
/* -------------------------------------------------------------------------- */
static void clearPm1WakeIrqState(M5PM1& pm1)
{
    pm1.setI2cSleepTime(0);
    pm1.setI2cSleepTime(0);
    pm1.irqClearGpioAll();
    pm1.irqClearSysAll();
    pm1.gpioSetWakeEnable(M5PM1_GPIO_NUM_0, false);
    pm1.gpioSetWakeEnable(M5PM1_GPIO_NUM_4, false);
}

bool Hal::initRgb()
{
    M5PM1::setLogLevel(M5PM1_LOG_LEVEL_INFO);
    M5IOE1::setLogLevel(M5IOE1_LOG_LEVEL_INFO);

    bool power_ok = false;
    for (std::uint8_t attempt = 1; attempt <= BOARD_PERIPHERAL_INIT_RETRIES; ++attempt) {
        ESP_LOGI(HAL_TAG, "Board peripheral init attempt %u/%u", attempt, BOARD_PERIPHERAL_INIT_RETRIES);

        const auto pm1_err = pm1.begin(&M5.In_I2C, PMIC_ADDR, RGB_I2C_FREQ_HZ);
        if (pm1_err != M5PM1_OK) {
            ESP_LOGW(HAL_TAG, "M5PM1 init failed: %d", static_cast<int>(pm1_err));
            delay(150);
            continue;
        }

        const auto ioe1_err = ioe1.begin(&M5.In_I2C, IO_EXPANDER_ADDR, RGB_I2C_FREQ_HZ, M5IOE1_INT_MODE_DISABLED);
        if (ioe1_err != M5IOE1_OK) {
            ESP_LOGW(HAL_TAG, "M5IOE1 init failed: %d", static_cast<int>(ioe1_err));
            delay(150);
            continue;
        }

        power_ok = true;
        clearPm1WakeIrqState(pm1);
        break;
    }

    if (!power_ok) {
        return false;
    }

    ioe1.pinMode(RGB_GREEN_PIN, OUTPUT);
    ioe1.pinMode(RGB_BLUE_PIN, OUTPUT);
    ioe1.pinMode(NFC_EN_PIN, OUTPUT);
    ioe1.pinMode(PDM_EN_PIN, OUTPUT);
    ioe1.setDriveMode(RGB_GREEN_PIN, M5IOE1_DRIVE_PUSHPULL);
    ioe1.setDriveMode(RGB_BLUE_PIN, M5IOE1_DRIVE_PUSHPULL);
    ioe1.setDriveMode(NFC_EN_PIN, M5IOE1_DRIVE_PUSHPULL);
    ioe1.setDriveMode(PDM_EN_PIN, M5IOE1_DRIVE_PUSHPULL);
    ioe1.digitalWrite(NFC_EN_PIN, LOW);
    ioe1.digitalWrite(PDM_EN_PIN, HIGH);
    ioe1.setPwmFrequency(RGB_PWM_FREQ_HZ);

    initTfCardPins();

    return true;
}

/* -------------------------------------------------------------------------- */
/*                                     RTC                                    */
/* -------------------------------------------------------------------------- */
bool Hal::rx8130RamWrite(std::uint8_t index, std::uint8_t value)
{
    if (index >= RX8130_RAM_SIZE) {
        return false;
    }

    std::uint8_t buf[2] = {static_cast<std::uint8_t>(RX8130_RAM_BASE + index), value};
    if (!M5.In_I2C.start(RX8130_ADDR, false, RX8130_I2C_FREQ_HZ)) {
        return false;
    }
    M5.In_I2C.write(buf, sizeof(buf));
    M5.In_I2C.stop();
    return true;
}

bool Hal::rx8130RamRead(std::uint8_t index, std::uint8_t* value)
{
    if (index >= RX8130_RAM_SIZE || value == nullptr) {
        return false;
    }

    std::uint8_t reg = static_cast<std::uint8_t>(RX8130_RAM_BASE + index);
    if (!M5.In_I2C.start(RX8130_ADDR, false, RX8130_I2C_FREQ_HZ)) {
        return false;
    }
    M5.In_I2C.write(&reg, 1);
    M5.In_I2C.stop();

    if (!M5.In_I2C.start(RX8130_ADDR, true, RX8130_I2C_FREQ_HZ)) {
        return false;
    }
    M5.In_I2C.read(value, 1);
    M5.In_I2C.stop();
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                     NFC                                    */
/* -------------------------------------------------------------------------- */
bool Hal::probeNfcIdentity(std::uint8_t& identity, std::uint8_t& type, std::uint8_t& rev)
{
    identity = 0;
    type     = 0;
    rev      = 0;

    for (int attempt = 0; attempt < 5; ++attempt) {
        std::uint8_t value = 0;
        if (M5.In_I2C.readRegister(NFC_ADDR, NFC_READ_IC_IDENTITY_CMD, &value, 1, NFC_I2C_FREQ_HZ)) {
            const std::uint8_t detected_type = static_cast<std::uint8_t>((value >> 3) & 0x1F);
            const std::uint8_t detected_rev  = static_cast<std::uint8_t>(value & 0x07);
            identity                         = value;
            type                             = detected_type;
            rev                              = detected_rev;
            if (detected_type == NFC_VALID_IDENTITY_TYPE && detected_rev != 0) {
                return true;
            }
        }
        delay(20);
    }
    return false;
}

void Hal::detectBoardVariant()
{
    if (!_rgb_ready) {
        _board_variant = BoardVariant::Lite;
        return;
    }

    m5ioe1_err_t err = M5IOE1_OK;
    ioe1.digitalWriteWithRes(NFC_EN_PIN, HIGH, &err);
    if (err != M5IOE1_OK) {
        _board_variant = BoardVariant::Lite;
        return;
    }
    delay(50);

    std::uint8_t identity = 0;
    std::uint8_t type     = 0;
    std::uint8_t rev      = 0;
    const bool has_nfc    = probeNfcIdentity(identity, type, rev);
    ioe1.digitalWriteWithRes(NFC_EN_PIN, LOW, &err);
    _board_variant = has_nfc ? BoardVariant::Pro : BoardVariant::Lite;
    ESP_LOGI(HAL_TAG, "NFC identity probe: ok=%d identity=0x%02X type=0x%02X rev=0x%02X", has_nfc, identity, type, rev);
    ESP_LOGI(HAL_TAG, "Board variant detected: %s", has_nfc ? "Pro" : "Lite");
}

bool Hal::setNfcPower(bool enable)
{
    if (!_rgb_ready) {
        return false;
    }
    ioe1.digitalWrite(NFC_EN_PIN, enable ? HIGH : LOW);
    return true;
}

/* -------------------------------------------------------------------------- */
/*                                     RGB                                    */
/* -------------------------------------------------------------------------- */
bool Hal::isRgbReady() const
{
    return _rgb_ready;
}

BoardVariant Hal::boardVariant() const
{
    return _board_variant;
}

bool Hal::isLiteModel() const
{
    return _board_variant == BoardVariant::Lite;
}

bool Hal::hasNfcHardware() const
{
    return _board_variant == BoardVariant::Pro;
}

bool Hal::setRgbLed(bool red_on, std::uint8_t green_percent, std::uint8_t blue_percent)
{
    if (green_percent > 100) {
        green_percent = 100;
    }
    if (blue_percent > 100) {
        blue_percent = 100;
    }

    const bool red_ok   = pm1.setLedEnLevel(red_on) == M5PM1_OK;
    const bool green_ok = ioe1.setPwmDuty(RGB_GREEN_PWM_CH, green_percent, false, green_percent > 0) == M5IOE1_OK;
    const bool blue_ok  = ioe1.setPwmDuty(RGB_BLUE_PWM_CH, blue_percent, false, blue_percent > 0) == M5IOE1_OK;
    return red_ok && green_ok && blue_ok;
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <Arduino.h>
#include <driver/sdmmc_host.h>
#include <esp_err.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>

static constexpr const char* TF_MOUNT_POINT = "/sdcard";
static constexpr std::uint8_t TF_EN_PIN     = M5IOE1_PIN_14;
static constexpr std::uint8_t TF_DET_PIN    = M5IOE1_PIN_1;
static constexpr gpio_num_t TF_DATA3_PIN    = GPIO_NUM_8;
static constexpr gpio_num_t TF_DATA2_PIN    = GPIO_NUM_9;
static constexpr gpio_num_t TF_DATA1_PIN    = GPIO_NUM_10;
static constexpr gpio_num_t TF_DATA0_PIN    = GPIO_NUM_11;
static constexpr gpio_num_t TF_CMD_PIN      = GPIO_NUM_12;
static constexpr gpio_num_t TF_CLK_PIN      = GPIO_NUM_13;

/* -------------------------------------------------------------------------- */
/*                                  TF Card                                   */
/* -------------------------------------------------------------------------- */
static bool isTfCardPresentStatus(TfCardStatus status)
{
    return status == TfCardStatus::Inserted || status == TfCardStatus::Mounted || status == TfCardStatus::MountFailed;
}

bool Hal::initTfCardPins()
{
    ioe1.pinMode(TF_EN_PIN, OUTPUT);
    ioe1.pinMode(TF_DET_PIN, INPUT);
    ioe1.setDriveMode(TF_EN_PIN, M5IOE1_DRIVE_PUSHPULL);
    ioe1.setPullMode(TF_DET_PIN, M5IOE1_PULL_UP);
    m5ioe1_err_t err = M5IOE1_OK;
    ioe1.digitalWriteWithRes(TF_EN_PIN, LOW, &err);
    _tf_power_enabled = false;
    return true;
}

bool Hal::isTfCardInserted()
{
    if (!_rgb_ready) {
        return false;
    }

    bool inserted = false;
    if (readTfCardDetect(inserted)) {
        return inserted;
    }

    return _tf_card != nullptr || isTfCardPresentStatus(_tf_card_info.status);
}

const TfCardInfo& Hal::refreshTfCardPresence()
{
    bool inserted = false;
    if (!readTfCardDetect(inserted)) {
        return _tf_card_info;
    }

    if (!inserted) {
        unmountTfCard();
        _tf_card_info         = {};
        _tf_card_info.status  = TfCardStatus::NoCard;
        _tf_card_info.message = "NO FOUND";
        return _tf_card_info;
    }

    if (_tf_card_info.status == TfCardStatus::Mounted && _tf_card) {
        return _tf_card_info;
    }

    if (_tf_card_info.status != TfCardStatus::MountFailed) {
        _tf_card_info         = {};
        _tf_card_info.status  = TfCardStatus::Inserted;
        _tf_card_info.message = "CARD FOUND";
    }
    return _tf_card_info;
}

const TfCardInfo& Hal::updateTfCard()
{
    bool inserted = false;
    if (!readTfCardDetect(inserted)) {
        return _tf_card_info;
    }

    if (!inserted) {
        unmountTfCard();
        _tf_card_info         = {};
        _tf_card_info.status  = TfCardStatus::NoCard;
        _tf_card_info.message = "NO FOUND";
        return _tf_card_info;
    }

    if (_tf_card_info.status == TfCardStatus::Mounted && _tf_card) {
        return _tf_card_info;
    }

    mountTfCard();
    return _tf_card_info;
}

const TfCardInfo& Hal::getTfCardInfo() const
{
    return _tf_card_info;
}

const char* Hal::tfMountPoint() const
{
    return TF_MOUNT_POINT;
}

bool Hal::mountTfCard()
{
    if (_tf_card) {
        return true;
    }

    _tf_card_info = {};
    bool inserted = false;
    if (!readTfCardDetect(inserted)) {
        _tf_card_info.status  = TfCardStatus::MountFailed;
        _tf_card_info.message = "DET READ FAIL";
        return false;
    }
    if (!inserted) {
        _tf_card_info.status  = TfCardStatus::NoCard;
        _tf_card_info.message = "NO FOUND";
        return false;
    }

    m5ioe1_err_t err = M5IOE1_OK;
    ioe1.digitalWriteWithRes(TF_EN_PIN, HIGH, &err);
    _tf_power_enabled = err == M5IOE1_OK;
    delay(300);

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
    mount_config.format_if_mount_failed           = false;
    mount_config.max_files                        = 8;
    mount_config.allocation_unit_size             = 16 * 1024;

    sdmmc_host_t host               = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width               = 4;
    slot_config.clk                 = TF_CLK_PIN;
    slot_config.cmd                 = TF_CMD_PIN;
    slot_config.d0                  = TF_DATA0_PIN;
    slot_config.d1                  = TF_DATA1_PIN;
    slot_config.d2                  = TF_DATA2_PIN;
    slot_config.d3                  = TF_DATA3_PIN;

    const esp_err_t ret = esp_vfs_fat_sdmmc_mount(TF_MOUNT_POINT, &host, &slot_config, &mount_config, &_tf_card);
    if (ret != ESP_OK) {
        _tf_card              = nullptr;
        _tf_card_info.status  = TfCardStatus::MountFailed;
        _tf_card_info.message = esp_err_to_name(ret);
        return false;
    }

    _tf_card_info.status     = TfCardStatus::Mounted;
    _tf_card_info.message    = "OK";
    _tf_card_info.card_bytes = static_cast<std::uint64_t>(_tf_card->csd.capacity) * _tf_card->csd.sector_size;
    updateTfCapacity();
    return true;
}

void Hal::updateTfCapacity()
{
    if (!_tf_card) {
        return;
    }

    _tf_card_info.status     = TfCardStatus::Mounted;
    _tf_card_info.card_bytes = static_cast<std::uint64_t>(_tf_card->csd.capacity) * _tf_card->csd.sector_size;

    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes  = 0;
    if (esp_vfs_fat_info(TF_MOUNT_POINT, &total_bytes, &free_bytes) == ESP_OK) {
        _tf_card_info.total_bytes = total_bytes;
        _tf_card_info.free_bytes  = free_bytes;
    }
}

void Hal::unmountTfCard()
{
    if (_tf_card) {
        esp_vfs_fat_sdcard_unmount(TF_MOUNT_POINT, _tf_card);
        _tf_card = nullptr;
    }
    if (_rgb_ready && _tf_power_enabled) {
        m5ioe1_err_t err = M5IOE1_OK;
        ioe1.digitalWriteWithRes(TF_EN_PIN, LOW, &err);
        if (err == M5IOE1_OK) {
            _tf_power_enabled = false;
        }
    }
}

bool Hal::readTfCardDetect(bool& inserted)
{
    inserted = false;
    if (!_rgb_ready) {
        return false;
    }

    m5ioe1_err_t err = M5IOE1_OK;
    const int level  = ioe1.digitalReadWithRes(TF_DET_PIN, &err);
    if (err != M5IOE1_OK) {
        return false;
    }

    inserted = level == LOW;
    return true;
}

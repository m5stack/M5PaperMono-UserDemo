/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <RadioLib.h>
#include <array>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <memory>

extern bool halIsGpioIsrServiceReady();

static constexpr const char* HAL_TAG                = "HAL";
static constexpr spi_host_device_t LORA_SPI_HOST    = SPI3_HOST;
static constexpr gpio_num_t LORA_SPI_SCK_PIN        = GPIO_NUM_39;
static constexpr gpio_num_t LORA_SPI_MISO_PIN       = GPIO_NUM_40;
static constexpr gpio_num_t LORA_SPI_MOSI_PIN       = GPIO_NUM_38;
static constexpr gpio_num_t LORA_NSS_PIN            = GPIO_NUM_41;
static constexpr gpio_num_t LORA_DIO1_PIN           = GPIO_NUM_5;
static constexpr gpio_num_t LORA_BUSY_PIN           = GPIO_NUM_21;
static constexpr m5pm1_gpio_num_t LORA_EN_PM1_PIN   = M5PM1_GPIO_NUM_2;
static constexpr std::uint8_t LORA_RST_IOE1_PIN     = M5IOE1_PIN_10;
static constexpr std::uint8_t LORA_ANT_SW_IOE1_PIN  = M5IOE1_PIN_2;
static constexpr std::uint32_t LORA_SPI_FREQ_HZ     = 8000000;
static constexpr float LORA_FREQ_MHZ                = 868.0f;
static constexpr float LORA_BW_KHZ                  = 62.5f;
static constexpr std::uint8_t LORA_SPREADING_FACTOR = 12;
static constexpr std::uint8_t LORA_CODING_RATE      = 8;
static constexpr std::uint8_t LORA_SYNC_WORD        = 0x34;
static constexpr std::int8_t LORA_TX_POWER_DBM      = 22;
static constexpr std::uint16_t LORA_PREAMBLE_LEN    = 10;
static constexpr float LORA_TCXO_VOLTAGE            = 3.0f;
static constexpr bool LORA_USE_REGULATOR_LDO        = true;
static constexpr float LORA_CURRENT_LIMIT_MA        = 140.0f;
static constexpr std::size_t LORA_MAX_PACKET_BYTES  = 255;

static volatile bool s_lora_rx_flag = false;
static volatile bool s_lora_tx_flag = false;

/* -------------------------------------------------------------------------- */
/*                                    LoRa                                    */
/* -------------------------------------------------------------------------- */
static void loraSetRxFlag()
{
    s_lora_rx_flag = true;
}

static void loraSetTxFlag()
{
    s_lora_tx_flag = true;
}

class PaperMonoRadioLibHal : public RadioLibHal {
public:
    PaperMonoRadioLibHal(gpio_num_t sck, gpio_num_t miso, gpio_num_t mosi)
        : RadioLibHal(GPIO_MODE_INPUT, GPIO_MODE_OUTPUT, 0, 1, GPIO_INTR_POSEDGE, GPIO_INTR_NEGEDGE),
          _sck(sck),
          _miso(miso),
          _mosi(mosi)
    {
    }

    ~PaperMonoRadioLibHal()
    {
        spiEnd();
    }

    void pinMode(std::uint32_t pin, std::uint32_t mode) override
    {
        if (pin == RADIOLIB_NC) {
            return;
        }

        gpio_config_t config = {};
        config.pin_bit_mask  = 1ULL << pin;
        config.mode          = static_cast<gpio_mode_t>(mode);
        config.pull_up_en    = GPIO_PULLUP_DISABLE;
        config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
        config.intr_type     = GPIO_INTR_DISABLE;
        gpio_config(&config);
    }

    void digitalWrite(std::uint32_t pin, std::uint32_t value) override
    {
        if (pin == RADIOLIB_NC) {
            return;
        }
        gpio_set_level(static_cast<gpio_num_t>(pin), value);
    }

    std::uint32_t digitalRead(std::uint32_t pin) override
    {
        if (pin == RADIOLIB_NC) {
            return 0;
        }
        return static_cast<std::uint32_t>(gpio_get_level(static_cast<gpio_num_t>(pin)));
    }

    void attachInterrupt(std::uint32_t interrupt_num, void (*interrupt_cb)(void), std::uint32_t mode) override
    {
        if (interrupt_num == RADIOLIB_NC || interrupt_cb == nullptr) {
            return;
        }

        if (!halIsGpioIsrServiceReady()) {
            ESP_LOGW(HAL_TAG, "LoRa GPIO ISR handler skipped: ISR service is not ready");
            return;
        }

        gpio_config_t config = {};
        config.pin_bit_mask  = 1ULL << interrupt_num;
        config.mode          = GPIO_MODE_INPUT;
        config.pull_up_en    = GPIO_PULLUP_DISABLE;
        config.pull_down_en  = GPIO_PULLDOWN_DISABLE;
        config.intr_type     = static_cast<gpio_int_type_t>(mode);
        gpio_config(&config);

        gpio_isr_handler_remove(static_cast<gpio_num_t>(interrupt_num));
        esp_err_t err = gpio_isr_handler_add(static_cast<gpio_num_t>(interrupt_num),
                                             reinterpret_cast<gpio_isr_t>(interrupt_cb), nullptr);
        if (err != ESP_OK) {
            ESP_LOGW(HAL_TAG, "LoRa GPIO ISR add failed pin=%lu: %s", interrupt_num, esp_err_to_name(err));
        }
    }

    void detachInterrupt(std::uint32_t interrupt_num) override
    {
        if (interrupt_num == RADIOLIB_NC) {
            return;
        }
        gpio_isr_handler_remove(static_cast<gpio_num_t>(interrupt_num));
        gpio_intr_disable(static_cast<gpio_num_t>(interrupt_num));
    }

    void delay(RadioLibTime_t ms) override
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }

    void delayMicroseconds(RadioLibTime_t us) override
    {
        const std::int64_t start_us = esp_timer_get_time();
        while (esp_timer_get_time() - start_us < static_cast<std::int64_t>(us)) {
            asm volatile("nop");
        }
    }

    RadioLibTime_t millis() override
    {
        return static_cast<RadioLibTime_t>(esp_timer_get_time() / 1000);
    }

    RadioLibTime_t micros() override
    {
        return static_cast<RadioLibTime_t>(esp_timer_get_time());
    }

    long pulseIn(std::uint32_t pin, std::uint32_t state, RadioLibTime_t timeout) override
    {
        if (pin == RADIOLIB_NC) {
            return 0;
        }

        pinMode(pin, GPIO_MODE_INPUT);
        const RadioLibTime_t start_us = micros();
        while (digitalRead(pin) != state) {
            if (micros() - start_us >= timeout) {
                return 0;
            }
        }
        const RadioLibTime_t pulse_start_us = micros();
        while (digitalRead(pin) == state) {
            if (micros() - pulse_start_us >= timeout) {
                return 0;
            }
        }
        return static_cast<long>(micros() - pulse_start_us);
    }

    void spiBegin() override
    {
        if (_spi_ready) {
            return;
        }

        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num      = _mosi;
        bus_config.miso_io_num      = _miso;
        bus_config.sclk_io_num      = _sck;
        bus_config.quadwp_io_num    = -1;
        bus_config.quadhd_io_num    = -1;
        bus_config.max_transfer_sz  = 4096;
        bus_config.flags            = SPICOMMON_BUSFLAG_MASTER;
        bus_config.intr_flags       = 0;

        esp_err_t err = spi_bus_initialize(LORA_SPI_HOST, &bus_config, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(HAL_TAG, "LoRa SPI bus init failed: %s", esp_err_to_name(err));
            return;
        }
        _bus_ready = true;

        spi_device_interface_config_t device_config = {};
        device_config.command_bits                  = 0;
        device_config.address_bits                  = 0;
        device_config.dummy_bits                    = 0;
        device_config.mode                          = 0;
        device_config.duty_cycle_pos                = 128;
        device_config.clock_speed_hz                = LORA_SPI_FREQ_HZ;
        device_config.spics_io_num                  = -1;
        device_config.queue_size                    = 1;
        device_config.flags                         = SPI_DEVICE_NO_DUMMY;

        err = spi_bus_add_device(LORA_SPI_HOST, &device_config, &_spi);
        if (err != ESP_OK) {
            ESP_LOGE(HAL_TAG, "LoRa SPI add device failed: %s", esp_err_to_name(err));
            return;
        }
        _spi_ready = true;
    }

    void spiBeginTransaction() override
    {
        spiBegin();
    }

    void spiTransfer(std::uint8_t* out, std::size_t len, std::uint8_t* in) override
    {
        if (!_spi_ready || len == 0) {
            return;
        }

        spi_transaction_t transaction = {};
        transaction.length            = len * 8;
        transaction.tx_buffer         = out;
        transaction.rx_buffer         = in;
        const esp_err_t err           = spi_device_transmit(_spi, &transaction);
        if (err != ESP_OK) {
            ESP_LOGW(HAL_TAG, "LoRa SPI transfer failed: %s", esp_err_to_name(err));
        }
    }

    void spiEndTransaction() override
    {
    }

    void spiEnd() override
    {
        if (_spi_ready) {
            spi_bus_remove_device(_spi);
            _spi       = nullptr;
            _spi_ready = false;
        }
        if (_bus_ready) {
            spi_bus_free(LORA_SPI_HOST);
            _bus_ready = false;
        }
    }

private:
    gpio_num_t _sck          = GPIO_NUM_NC;
    gpio_num_t _miso         = GPIO_NUM_NC;
    gpio_num_t _mosi         = GPIO_NUM_NC;
    spi_device_handle_t _spi = nullptr;
    bool _bus_ready          = false;
    bool _spi_ready          = false;
};

struct LoraDriverState {
    std::unique_ptr<PaperMonoRadioLibHal> hal;
    std::unique_ptr<Module> module;
    std::unique_ptr<SX1262> radio;
    std::string tx_text;
    bool ready              = false;
    bool transmitting       = false;
    std::int16_t last_state = RADIOLIB_ERR_NONE;
};

static LoraDriverState s_lora;

bool Hal::initLora()
{
    if (s_lora.ready) {
        return true;
    }
    if (!_rgb_ready) {
        ESP_LOGW(HAL_TAG, "LoRa init skipped: board peripherals are not ready");
        return false;
    }

    pm1.gpioSetFunc(LORA_EN_PM1_PIN, M5PM1_GPIO_FUNC_GPIO);
    if (pm1.gpioSet(LORA_EN_PM1_PIN, M5PM1_GPIO_MODE_OUTPUT, 1, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_PUSHPULL) !=
        M5PM1_OK) {
        ESP_LOGW(HAL_TAG, "LoRa power enable failed");
    }

    ioe1.pinMode(LORA_ANT_SW_IOE1_PIN, OUTPUT);
    ioe1.pinMode(LORA_RST_IOE1_PIN, OUTPUT);
    ioe1.setDriveMode(LORA_ANT_SW_IOE1_PIN, M5IOE1_DRIVE_PUSHPULL);
    ioe1.setDriveMode(LORA_RST_IOE1_PIN, M5IOE1_DRIVE_PUSHPULL);
    m5ioe1_err_t ioe_err = M5IOE1_OK;
    ioe1.digitalWriteWithRes(LORA_ANT_SW_IOE1_PIN, HIGH, &ioe_err);
    ioe1.digitalWriteWithRes(LORA_RST_IOE1_PIN, LOW, &ioe_err);
    delay(100);
    ioe1.digitalWriteWithRes(LORA_RST_IOE1_PIN, HIGH, &ioe_err);
    delay(20);
    if (ioe_err != M5IOE1_OK) {
        ESP_LOGW(HAL_TAG, "LoRa reset line setup failed: %d", static_cast<int>(ioe_err));
    }

    s_lora.ready        = false;
    s_lora.transmitting = false;
    s_lora.last_state   = RADIOLIB_ERR_UNKNOWN;
    s_lora.radio.reset();
    s_lora.module.reset();
    s_lora.hal.reset();

    s_lora.hal    = std::make_unique<PaperMonoRadioLibHal>(LORA_SPI_SCK_PIN, LORA_SPI_MISO_PIN, LORA_SPI_MOSI_PIN);
    s_lora.module = std::make_unique<Module>(s_lora.hal.get(), LORA_NSS_PIN, LORA_DIO1_PIN, RADIOLIB_NC, LORA_BUSY_PIN);
    s_lora.radio  = std::make_unique<SX1262>(s_lora.module.get());

    ESP_LOGI(HAL_TAG, "LoRa init NSS=%d BUSY=%d DIO1=%d SCK=%d MISO=%d MOSI=%d", LORA_NSS_PIN, LORA_BUSY_PIN,
             LORA_DIO1_PIN, LORA_SPI_SCK_PIN, LORA_SPI_MISO_PIN, LORA_SPI_MOSI_PIN);
    s_lora.last_state =
        s_lora.radio->begin(LORA_FREQ_MHZ, LORA_BW_KHZ, LORA_SPREADING_FACTOR, LORA_CODING_RATE, LORA_SYNC_WORD,
                            LORA_TX_POWER_DBM, LORA_PREAMBLE_LEN, LORA_TCXO_VOLTAGE, LORA_USE_REGULATOR_LDO);
    if (s_lora.last_state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(HAL_TAG, "LoRa init failed state=%d", s_lora.last_state);
        s_lora.radio.reset();
        s_lora.module.reset();
        s_lora.hal.reset();
        return false;
    }

    s_lora.radio->setDio2AsRfSwitch(true);
    s_lora.radio->setCurrentLimit(LORA_CURRENT_LIMIT_MA);
    s_lora.radio->setPacketReceivedAction(loraSetRxFlag);
    s_lora.last_state   = s_lora.radio->startReceive();
    s_lora.ready        = s_lora.last_state == RADIOLIB_ERR_NONE;
    s_lora.transmitting = false;
    s_lora_rx_flag      = false;
    s_lora_tx_flag      = false;
    ESP_LOGI(HAL_TAG, "LoRa init OK rx_state=%d", s_lora.last_state);
    return s_lora.ready;
}

bool Hal::isLoraReady() const
{
    return s_lora.ready;
}

bool Hal::startLoraTransmit(const std::string& text)
{
    if (text.empty() || text.size() > LORA_MAX_PACKET_BYTES) {
        ESP_LOGW(HAL_TAG, "LoRa TX rejected: len=%u max=%u", static_cast<unsigned>(text.size()),
                 static_cast<unsigned>(LORA_MAX_PACKET_BYTES));
        return false;
    }
    if (!initLora() || s_lora.transmitting || !s_lora.radio) {
        ESP_LOGW(HAL_TAG, "LoRa TX rejected: ready=%d transmitting=%d radio=%d", s_lora.ready, s_lora.transmitting,
                 s_lora.radio ? 1 : 0);
        return false;
    }

    s_lora.tx_text      = text;
    s_lora_tx_flag      = false;
    s_lora.transmitting = true;
    s_lora.radio->standby();
    s_lora.radio->setPacketSentAction(loraSetTxFlag);
    s_lora.last_state = s_lora.radio->startTransmit(reinterpret_cast<const std::uint8_t*>(s_lora.tx_text.data()),
                                                    s_lora.tx_text.size());
    if (s_lora.last_state != RADIOLIB_ERR_NONE) {
        ESP_LOGW(HAL_TAG, "LoRa TX start failed: state=%d len=%u", s_lora.last_state,
                 static_cast<unsigned>(s_lora.tx_text.size()));
        s_lora.transmitting = false;
        s_lora.radio->setPacketReceivedAction(loraSetRxFlag);
        s_lora.radio->startReceive();
        return false;
    }
    ESP_LOGI(HAL_TAG, "LoRa TX started: len=%u", static_cast<unsigned>(s_lora.tx_text.size()));
    return true;
}

bool Hal::pollLora(LoraEvent& event)
{
    event = {};
    if (!s_lora.ready || !s_lora.radio) {
        return false;
    }

    bool has_event = false;
    if (s_lora.transmitting && s_lora_tx_flag) {
        s_lora_tx_flag      = false;
        s_lora.last_state   = s_lora.radio->finishTransmit();
        s_lora.transmitting = false;
        event.transmit_done = true;
        event.state         = s_lora.last_state;
        event.error         = s_lora.last_state != RADIOLIB_ERR_NONE;
        has_event           = true;
        if (event.error) {
            ESP_LOGW(HAL_TAG, "LoRa TX finish failed: state=%d", event.state);
        } else {
            ESP_LOGI(HAL_TAG, "LoRa TX finish OK");
        }

        s_lora.radio->setPacketReceivedAction(loraSetRxFlag);
        const std::int16_t rx_state = s_lora.radio->startReceive();
        if (rx_state != RADIOLIB_ERR_NONE) {
            ESP_LOGW(HAL_TAG, "LoRa restart receive failed: %d", rx_state);
            event.error = true;
            event.state = rx_state;
        }
    }

    if (!s_lora.transmitting && s_lora_rx_flag) {
        s_lora_rx_flag                                             = false;
        std::array<std::uint8_t, LORA_MAX_PACKET_BYTES + 1> rx_buf = {};
        s_lora.last_state = s_lora.radio->readData(rx_buf.data(), LORA_MAX_PACKET_BYTES);
        event.received    = s_lora.last_state == RADIOLIB_ERR_NONE;
        event.error       = s_lora.last_state != RADIOLIB_ERR_NONE;
        event.state       = s_lora.last_state;
        if (event.received) {
            event.text = reinterpret_cast<const char*>(rx_buf.data());
            event.rssi = s_lora.radio->getRSSI();
            event.snr  = s_lora.radio->getSNR();
        }
        s_lora.radio->startReceive();
        has_event = true;
    }

    return has_event;
}

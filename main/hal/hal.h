/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <M5IOE1.h>
#include <M5PM1.h>
#include <M5Unified.hpp>
#include <cstddef>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <sdmmc_cmd.h>
#include <string>
#include <vector>

enum class TfCardStatus : std::uint8_t {
    NoCard,
    Inserted,
    Mounted,
    MountFailed,
};

enum class BoardVariant : std::uint8_t {
    Lite,
    Pro,
};

class DisplayRefreshObserver {
public:
    virtual ~DisplayRefreshObserver()       = default;
    virtual void prepareForDisplayRefresh() = 0;
};

struct TfCardInfo {
    TfCardStatus status       = TfCardStatus::NoCard;
    std::uint64_t card_bytes  = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t free_bytes  = 0;
    std::string message;
};

static constexpr std::size_t BUZZER_PATTERN_MAX_STEPS = 10;

struct BuzzerTone {
    std::uint16_t start_freq_hz = 0;
    std::uint16_t end_freq_hz   = 0;
    std::uint16_t duration_ms   = 0;
    std::uint16_t gap_ms        = 0;
};

struct LoraEvent {
    bool received      = false;
    bool transmit_done = false;
    bool error         = false;
    std::int16_t state = 0;
    float rssi         = 0.0f;
    float snr          = 0.0f;
    std::string text;
};

class Hal {
public:
    bool init();

    M5GFX& display     = M5.Display;
    m5::RTC_Class& rtc = M5.Rtc;
    m5::IMU_Class& imu = M5.Imu;
    M5PM1 pm1;
    M5IOE1 ioe1;

    /* --------------------------------- System --------------------------------- */
    void delay(std::uint32_t ms);
    std::uint32_t millis();
    void feedTheDog();
    std::int16_t getBatteryVoltageMv();

    /* ---------------------------------- Board --------------------------------- */
    BoardVariant boardVariant() const;
    bool isLiteModel() const;
    bool hasNfcHardware() const;
    bool isRgbReady() const;
    bool setRgbLed(bool red_on, std::uint8_t green_percent, std::uint8_t blue_percent);

    /* -------------------------------- Microphone ------------------------------- */
    bool initMic();
    bool isMicReady() const;
    bool readMicSamples(std::int16_t* buffer, std::size_t count);

    /* ----------------------------------- RTC ---------------------------------- */
    bool rx8130RamWrite(std::uint8_t index, std::uint8_t value);
    bool rx8130RamRead(std::uint8_t index, std::uint8_t* value);

    /* --------------------------------- Buzzer --------------------------------- */
    bool isBuzzerMuted() const;
    void setBuzzerMuted(bool muted);
    void stopBuzzer();
    void playBuzzer(std::uint16_t freq_hz, std::uint32_t duration_ms);
    void playBuzzerSweep(std::uint16_t start_freq_hz, std::uint16_t end_freq_hz, std::uint32_t duration_ms);
    void playBuzzerPattern(const BuzzerTone* tones, std::size_t count);

    /* ---------------------------------- LoRa ---------------------------------- */
    bool initLora();
    bool isLoraReady() const;
    bool startLoraTransmit(const std::string& text);
    bool pollLora(LoraEvent& event);

    /* ----------------------------------- NFC ---------------------------------- */
    bool setNfcPower(bool enable);

    /* --------------------------------- TF Card -------------------------------- */
    bool isTfCardInserted();
    const TfCardInfo& refreshTfCardPresence();
    const TfCardInfo& updateTfCard();
    const TfCardInfo& getTfCardInfo() const;
    const char* tfMountPoint() const;
    void unmountTfCard();

    /* --------------------------------- Display -------------------------------- */
    bool drawBackground(m5gfx::epd_mode_t mode = m5gfx::epd_mode_t::epd_quality, bool flush = true);
    bool captureDisplaySnapshot();
    bool restoreDisplaySnapshot();
    void clearDisplaySnapshot();
    void flushDisplayFastOnce();
    void flushDisplay();
    void flushDisplay(int32_t x, int32_t y, int32_t w, int32_t h);

    /* ---------------------------------- Touch --------------------------------- */
    bool isTouchPressed();
    const m5::Touch_Class::touch_detail_t& getTouchDetail();
    bool wasTouchClickedArea(int x, int y, int w, int h);

    /* ----------------------------------- UI ----------------------------------- */
    void requestRefresh();
    void requestFullRedraw(m5gfx::epd_mode_t mode = m5gfx::epd_mode_t::epd_quality);
    void requestRefreshNextFrame(m5gfx::epd_mode_t mode = m5gfx::epd_mode_t::epd_quality);
    bool registerDisplayRefreshObserver(DisplayRefreshObserver* observer);
    void prepareDisplayRefreshObservers(DisplayRefreshObserver* exclude = nullptr);
    bool isPreparingDisplayRefreshObservers() const;
    void releaseModalNextFrame(m5gfx::epd_mode_t mode = m5gfx::epd_mode_t::epd_quality);
    void beginFrame();
    bool isRefreshRequested() const;
    bool isFullRedrawRequested() const;
    m5gfx::epd_mode_t fullRedrawMode() const;
    void clearRefreshRequest();
    void setUiModalActive(bool active);
    bool isUiModalActive() const;

private:
    enum class BuzzerCommandType : std::uint8_t {
        Stop,
        Mute,
        Tone,
        Sweep,
        Pattern,
    };

    struct BuzzerCommand {
        BuzzerCommandType type                     = BuzzerCommandType::Stop;
        bool muted                                 = false;
        std::uint16_t start_freq_hz                = 0;
        std::uint16_t end_freq_hz                  = 0;
        std::uint32_t duration_ms                  = 0;
        std::uint8_t tone_count                    = 0;
        BuzzerTone tones[BUZZER_PATTERN_MAX_STEPS] = {};
    };

    /* ---------------------------------- Board --------------------------------- */
    bool initRgb();
    void detectBoardVariant();
    bool probeNfcIdentity(std::uint8_t& identity, std::uint8_t& type, std::uint8_t& rev);

    /* ----------------------------------- IMU ---------------------------------- */
    bool initImu();

    /* -------------------------------- Microphone ------------------------------- */
    bool setPdmPower(bool enable);
    int readPdmEnableLevel();

    /* --------------------------------- Buzzer --------------------------------- */
    bool initBuzzer();
    bool sendBuzzerCommand(const BuzzerCommand& command);
    static void buzzerTaskEntry(void* arg);
    void buzzerTask();
    void setBuzzerOutput(std::uint16_t freq_hz);

    /* --------------------------------- TF Card -------------------------------- */
    bool initTfCardPins();
    bool mountTfCard();
    bool readTfCardDetect(bool& inserted);
    void updateTfCapacity();

    bool _refresh_request                        = false;
    bool _full_redraw_request                    = false;
    bool _refresh_next_frame                     = false;
    bool _ui_modal_active                        = false;
    bool _release_modal_next_frame               = false;
    m5gfx::epd_mode_t _full_redraw_mode          = m5gfx::epd_mode_t::epd_quality;
    m5gfx::epd_mode_t _release_modal_redraw_mode = m5gfx::epd_mode_t::epd_quality;
    m5gfx::epd_mode_t _next_frame_redraw_mode    = m5gfx::epd_mode_t::epd_quality;
    BoardVariant _board_variant                  = BoardVariant::Lite;
    bool _rgb_ready                              = false;
    bool _buzzer_ready                           = false;
    bool _buzzer_muted                           = false;
    bool _tf_power_enabled                       = false;
    bool _mic_ready                              = false;
    std::uint8_t _local_refresh_count            = 0;
    std::uint8_t _fast_refresh_count             = 0;
    std::vector<m5gfx::grayscale_t> _display_snapshot;
    std::int16_t _display_snapshot_w = 0;
    std::int16_t _display_snapshot_h = 0;
    bool _display_snapshot_valid     = false;
    QueueHandle_t _buzzer_queue      = nullptr;
    TaskHandle_t _buzzer_task        = nullptr;
    TfCardInfo _tf_card_info;
    sdmmc_card_t* _tf_card                                                           = nullptr;
    static constexpr std::size_t DISPLAY_REFRESH_OBSERVER_MAX                        = 4;
    DisplayRefreshObserver* _display_refresh_observers[DISPLAY_REFRESH_OBSERVER_MAX] = {};
    std::size_t _display_refresh_observer_count                                      = 0;
    bool _preparing_display_refresh_observers                                        = false;
};

Hal& GetHAL();

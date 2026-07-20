/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <algorithm>
#include <driver/ledc.h>
#include <esp_err.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

static constexpr const char* HAL_TAG                     = "HAL";
static constexpr gpio_num_t BUZZER_PIN                   = GPIO_NUM_42;
static constexpr ledc_mode_t BUZZER_LEDC_MODE            = LEDC_LOW_SPEED_MODE;
static constexpr ledc_timer_t BUZZER_LEDC_TIMER          = LEDC_TIMER_3;
static constexpr ledc_channel_t BUZZER_LEDC_CHANNEL      = LEDC_CHANNEL_7;
static constexpr ledc_timer_bit_t BUZZER_LEDC_RESOLUTION = LEDC_TIMER_10_BIT;
static constexpr std::uint32_t BUZZER_DUTY_50_PERCENT    = 512;
static constexpr std::uint16_t BUZZER_DEFAULT_FREQ_HZ    = 2000;
static constexpr std::uint16_t BUZZER_MIN_FREQ_HZ        = 40;
static constexpr std::uint16_t BUZZER_MAX_FREQ_HZ        = 12000;
static constexpr std::uint32_t BUZZER_SWEEP_UPDATE_MS    = 2;
static constexpr UBaseType_t BUZZER_QUEUE_DEPTH          = 6;
static constexpr std::uint32_t BUZZER_TASK_STACK         = 3072;
static constexpr UBaseType_t BUZZER_TASK_PRIORITY        = 3;

/* -------------------------------------------------------------------------- */
/*                                   Buzzer                                   */
/* -------------------------------------------------------------------------- */
bool Hal::initBuzzer()
{
    ledc_timer_config_t timer_config = {};
    timer_config.speed_mode          = BUZZER_LEDC_MODE;
    timer_config.duty_resolution     = BUZZER_LEDC_RESOLUTION;
    timer_config.timer_num           = BUZZER_LEDC_TIMER;
    timer_config.freq_hz             = BUZZER_DEFAULT_FREQ_HZ;
    timer_config.clk_cfg             = LEDC_AUTO_CLK;
    esp_err_t err                    = ledc_timer_config(&timer_config);
    if (err != ESP_OK) {
        ESP_LOGW(HAL_TAG, "Buzzer timer config failed: %s", esp_err_to_name(err));
        return false;
    }

    ledc_channel_config_t channel_config = {};
    channel_config.gpio_num              = BUZZER_PIN;
    channel_config.speed_mode            = BUZZER_LEDC_MODE;
    channel_config.channel               = BUZZER_LEDC_CHANNEL;
    channel_config.intr_type             = LEDC_INTR_DISABLE;
    channel_config.timer_sel             = BUZZER_LEDC_TIMER;
    channel_config.duty                  = 0;
    channel_config.hpoint                = 0;
    channel_config.flags.output_invert   = 0;
    err                                  = ledc_channel_config(&channel_config);
    if (err != ESP_OK) {
        ESP_LOGW(HAL_TAG, "Buzzer channel config failed: %s", esp_err_to_name(err));
        return false;
    }

    _buzzer_queue = xQueueCreate(BUZZER_QUEUE_DEPTH, sizeof(BuzzerCommand));
    if (!_buzzer_queue) {
        ESP_LOGW(HAL_TAG, "Buzzer queue create failed");
        ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
        return false;
    }

    const BaseType_t task_ok =
        xTaskCreate(buzzerTaskEntry, "buzzer", BUZZER_TASK_STACK, this, BUZZER_TASK_PRIORITY, &_buzzer_task);
    if (task_ok != pdPASS) {
        ESP_LOGW(HAL_TAG, "Buzzer task create failed");
        vQueueDelete(_buzzer_queue);
        _buzzer_queue = nullptr;
        ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
        return false;
    }

    ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
    return true;
}

bool Hal::isBuzzerMuted() const
{
    return _buzzer_muted;
}

void Hal::setBuzzerMuted(bool muted)
{
    if (_buzzer_muted == muted) {
        return;
    }
    _buzzer_muted         = muted;
    BuzzerCommand command = {};
    command.type          = BuzzerCommandType::Mute;
    command.muted         = muted;
    sendBuzzerCommand(command);
}

void Hal::stopBuzzer()
{
    BuzzerCommand command = {};
    command.type          = BuzzerCommandType::Stop;
    sendBuzzerCommand(command);
}

void Hal::playBuzzer(std::uint16_t freq_hz, std::uint32_t duration_ms)
{
    if (!_buzzer_ready || _buzzer_muted || freq_hz == 0 || duration_ms == 0) {
        return;
    }

    if (freq_hz < BUZZER_MIN_FREQ_HZ) {
        freq_hz = BUZZER_MIN_FREQ_HZ;
    } else if (freq_hz > BUZZER_MAX_FREQ_HZ) {
        freq_hz = BUZZER_MAX_FREQ_HZ;
    }

    BuzzerCommand command = {};
    command.type          = BuzzerCommandType::Tone;
    command.start_freq_hz = freq_hz;
    command.end_freq_hz   = freq_hz;
    command.duration_ms   = duration_ms;
    sendBuzzerCommand(command);
}

void Hal::playBuzzerSweep(std::uint16_t start_freq_hz, std::uint16_t end_freq_hz, std::uint32_t duration_ms)
{
    if (!_buzzer_ready || _buzzer_muted || start_freq_hz == 0 || end_freq_hz == 0 || duration_ms == 0) {
        return;
    }

    if (start_freq_hz < BUZZER_MIN_FREQ_HZ) {
        start_freq_hz = BUZZER_MIN_FREQ_HZ;
    } else if (start_freq_hz > BUZZER_MAX_FREQ_HZ) {
        start_freq_hz = BUZZER_MAX_FREQ_HZ;
    }
    if (end_freq_hz < BUZZER_MIN_FREQ_HZ) {
        end_freq_hz = BUZZER_MIN_FREQ_HZ;
    } else if (end_freq_hz > BUZZER_MAX_FREQ_HZ) {
        end_freq_hz = BUZZER_MAX_FREQ_HZ;
    }

    BuzzerCommand command = {};
    command.type          = BuzzerCommandType::Sweep;
    command.start_freq_hz = start_freq_hz;
    command.end_freq_hz   = end_freq_hz;
    command.duration_ms   = duration_ms;
    sendBuzzerCommand(command);
}

void Hal::playBuzzerPattern(const BuzzerTone* tones, std::size_t count)
{
    if (!_buzzer_ready || _buzzer_muted || !tones || count == 0) {
        return;
    }

    BuzzerCommand command = {};
    command.type          = BuzzerCommandType::Pattern;
    for (std::size_t i = 0; i < count && command.tone_count < BUZZER_PATTERN_MAX_STEPS; ++i) {
        BuzzerTone tone = tones[i];
        if (tone.start_freq_hz == 0 || tone.duration_ms == 0) {
            continue;
        }
        if (tone.start_freq_hz < BUZZER_MIN_FREQ_HZ) {
            tone.start_freq_hz = BUZZER_MIN_FREQ_HZ;
        } else if (tone.start_freq_hz > BUZZER_MAX_FREQ_HZ) {
            tone.start_freq_hz = BUZZER_MAX_FREQ_HZ;
        }
        if (tone.end_freq_hz == 0) {
            tone.end_freq_hz = tone.start_freq_hz;
        } else if (tone.end_freq_hz < BUZZER_MIN_FREQ_HZ) {
            tone.end_freq_hz = BUZZER_MIN_FREQ_HZ;
        } else if (tone.end_freq_hz > BUZZER_MAX_FREQ_HZ) {
            tone.end_freq_hz = BUZZER_MAX_FREQ_HZ;
        }
        command.tones[command.tone_count++] = tone;
    }

    if (command.tone_count > 0) {
        sendBuzzerCommand(command);
    }
}

void Hal::setBuzzerOutput(std::uint16_t freq_hz)
{
    if (!_buzzer_ready || freq_hz == 0) {
        return;
    }

    ledc_set_freq(BUZZER_LEDC_MODE, BUZZER_LEDC_TIMER, freq_hz);
    ledc_set_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, BUZZER_DUTY_50_PERCENT);
    ledc_update_duty(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL);
}

bool Hal::sendBuzzerCommand(const BuzzerCommand& command)
{
    if (!_buzzer_queue) {
        return false;
    }

    if (xQueueSend(_buzzer_queue, &command, 0) == pdPASS) {
        return true;
    }

    xQueueReset(_buzzer_queue);
    return xQueueSend(_buzzer_queue, &command, 0) == pdPASS;
}

void Hal::buzzerTaskEntry(void* arg)
{
    static_cast<Hal*>(arg)->buzzerTask();
}

void Hal::buzzerTask()
{
    bool active                                  = false;
    bool sweep                                   = false;
    bool pattern_active                          = false;
    bool gap_active                              = false;
    bool muted                                   = false;
    std::uint16_t start_freq_hz                  = 0;
    std::uint16_t end_freq_hz                    = 0;
    TickType_t start_tick                        = 0;
    TickType_t duration_ticks                    = 0;
    TickType_t last_update_tick                  = 0;
    BuzzerTone pattern[BUZZER_PATTERN_MAX_STEPS] = {};
    std::uint8_t pattern_count                   = 0;
    std::uint8_t pattern_index                   = 0;
    const TickType_t update_ticks                = std::max<TickType_t>(1, pdMS_TO_TICKS(BUZZER_SWEEP_UPDATE_MS));

    auto startTone = [&](std::uint16_t start_freq, std::uint16_t end_freq, std::uint32_t duration_ms, bool sweep_tone) {
        start_freq_hz    = start_freq;
        end_freq_hz      = end_freq > 0 ? end_freq : start_freq;
        sweep            = sweep_tone && start_freq_hz != end_freq_hz;
        duration_ticks   = std::max<TickType_t>(1, pdMS_TO_TICKS(duration_ms));
        start_tick       = xTaskGetTickCount();
        last_update_tick = start_tick;
        gap_active       = false;
        active           = true;
        setBuzzerOutput(start_freq_hz);
    };

    auto stopPlayback = [&]() {
        ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
        active         = false;
        gap_active     = false;
        pattern_active = false;
    };

    auto startPatternStep = [&]() {
        if (!pattern_active || pattern_index >= pattern_count || muted) {
            stopPlayback();
            return;
        }

        const BuzzerTone& tone = pattern[pattern_index];
        startTone(tone.start_freq_hz, tone.end_freq_hz, tone.duration_ms, tone.start_freq_hz != tone.end_freq_hz);
    };

    while (true) {
        TickType_t wait_ticks = portMAX_DELAY;
        if (active || gap_active) {
            const TickType_t now_tick      = xTaskGetTickCount();
            const TickType_t elapsed_ticks = now_tick - start_tick;
            if (elapsed_ticks >= duration_ticks) {
                if (gap_active) {
                    gap_active = false;
                    startPatternStep();
                } else if (pattern_active) {
                    ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
                    const std::uint16_t gap_ms = pattern[pattern_index].gap_ms;
                    ++pattern_index;
                    if (pattern_index >= pattern_count) {
                        stopPlayback();
                    } else if (gap_ms > 0) {
                        active         = false;
                        gap_active     = true;
                        start_tick     = now_tick;
                        duration_ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(gap_ms));
                    } else {
                        startPatternStep();
                    }
                } else {
                    stopPlayback();
                }
                continue;
            }
            wait_ticks = std::min(update_ticks, duration_ticks - elapsed_ticks);
        }

        BuzzerCommand command = {};
        if (xQueueReceive(_buzzer_queue, &command, wait_ticks) == pdPASS) {
            switch (command.type) {
                case BuzzerCommandType::Stop:
                    stopPlayback();
                    break;
                case BuzzerCommandType::Mute:
                    muted = command.muted;
                    if (muted) {
                        stopPlayback();
                    }
                    break;
                case BuzzerCommandType::Tone:
                case BuzzerCommandType::Sweep:
                    if (!muted && command.start_freq_hz > 0 && command.duration_ms > 0) {
                        pattern_active = false;
                        startTone(command.start_freq_hz, command.end_freq_hz, command.duration_ms,
                                  command.type == BuzzerCommandType::Sweep);
                    }
                    break;
                case BuzzerCommandType::Pattern:
                    if (!muted && command.tone_count > 0) {
                        pattern_count = command.tone_count;
                        pattern_index = 0;
                        for (std::uint8_t i = 0; i < pattern_count; ++i) {
                            pattern[i] = command.tones[i];
                        }
                        pattern_active = true;
                        startPatternStep();
                    }
                    break;
            }
            continue;
        }

        if (!active || gap_active) {
            continue;
        }

        const TickType_t now_tick      = xTaskGetTickCount();
        const TickType_t elapsed_ticks = now_tick - start_tick;
        if (elapsed_ticks >= duration_ticks) {
            if (pattern_active) {
                ledc_stop(BUZZER_LEDC_MODE, BUZZER_LEDC_CHANNEL, 0);
                const std::uint16_t gap_ms = pattern[pattern_index].gap_ms;
                ++pattern_index;
                if (pattern_index >= pattern_count) {
                    stopPlayback();
                } else if (gap_ms > 0) {
                    active         = false;
                    gap_active     = true;
                    start_tick     = now_tick;
                    duration_ticks = std::max<TickType_t>(1, pdMS_TO_TICKS(gap_ms));
                } else {
                    startPatternStep();
                }
            } else {
                stopPlayback();
            }
            continue;
        }

        if (sweep && now_tick - last_update_tick >= update_ticks) {
            const std::int32_t freq_delta =
                static_cast<std::int32_t>(end_freq_hz) - static_cast<std::int32_t>(start_freq_hz);
            const std::uint16_t freq_hz = static_cast<std::uint16_t>(
                static_cast<std::int32_t>(start_freq_hz) +
                freq_delta * static_cast<std::int32_t>(elapsed_ticks) / static_cast<std::int32_t>(duration_ticks));
            setBuzzerOutput(freq_hz);
            last_update_tick = now_tick;
        }
    }
}

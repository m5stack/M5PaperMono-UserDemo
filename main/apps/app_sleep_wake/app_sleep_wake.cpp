/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <assets.h>
#include <cstddef>
#include <cstdint>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_sleep.h>
#include <esp_rom_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <hal.h>
#include <bmi270.h>

/* -------------------------------------------------------------------------- */
/*                                  Constants                                 */
/* -------------------------------------------------------------------------- */

static constexpr const char* APP_SLEEP_WAKE_TAG = "AppSleepWake";

static constexpr int IMU_WAKE_X   = 21;
static constexpr int IMU_WAKE_Y   = 434;
static constexpr int IMU_WAKE_W   = 128 - 21;
static constexpr int IMU_WAKE_H   = 522 - 434;
static constexpr int TOUCH_WAKE_X = 136;
static constexpr int TOUCH_WAKE_Y = 434;
static constexpr int TOUCH_WAKE_W = 243 - 136;
static constexpr int TOUCH_WAKE_H = 522 - 434;
static constexpr int RTC_WAKE_X   = 251;
static constexpr int RTC_WAKE_Y   = 434;
static constexpr int RTC_WAKE_W   = 358 - 251;
static constexpr int RTC_WAKE_H   = 522 - 434;

static constexpr gpio_num_t USER_BUTTON1_GPIO = GPIO_NUM_2;
static constexpr gpio_num_t USER_BUTTON2_GPIO = GPIO_NUM_3;
static constexpr gpio_num_t LORA_IRQ_GPIO     = GPIO_NUM_5;
static constexpr gpio_num_t RFID_IRQ_GPIO     = GPIO_NUM_6;
static constexpr gpio_num_t PM1_IRQ_GPIO      = GPIO_NUM_1;
static constexpr gpio_num_t BUZZER_GPIO       = GPIO_NUM_42;

static constexpr std::uint32_t WAKE_TOUCH_COOLDOWN_MS        = 350;
static constexpr std::uint32_t WAKE_IMAGE_HOLD_MS            = 250;
static constexpr std::uint32_t RTC_WAKE_TIMER_MS             = 10000;
static constexpr gpio_num_t TOUCH_WAKE_GPIO                  = GPIO_NUM_4;
static constexpr std::uint32_t TOUCH_RELEASE_TIMEOUT_MS      = 3000;
static constexpr std::uint32_t TOUCH_RELEASE_POLL_MS         = 20;
static constexpr std::uint8_t PM1_IDLE_SLEEP_SECONDS         = 1;
static constexpr std::uint32_t RTC_WAKE_LINE_IDLE_TIMEOUT_MS = 3000;
static constexpr std::uint32_t RTC_WAKE_LINE_POLL_MS         = 50;
static constexpr std::uint32_t BMI270_I2C_FREQ_HZ            = 100000;
static constexpr std::uint8_t BMI270_ADDR_PRIMARY            = 0x68;
static constexpr std::uint8_t BMI270_ADDR_SECONDARY          = 0x69;
static constexpr std::uint16_t BMI270_ANY_MOTION_DURATION    = 20;
static constexpr std::uint16_t BMI270_ANY_MOTION_THRESHOLD   = 640;
static constexpr std::uint16_t BMI270_ANY_MOTION_STATUS_MASK = 0x0040;
static constexpr std::uint32_t IMU_WAKE_LINE_IDLE_TIMEOUT_MS = 3000;
static constexpr std::uint32_t IMU_WAKE_LINE_POLL_MS         = 50;

static constexpr m5pm1_gpio_num_t PM1_GPIO_RTC_INT   = M5PM1_GPIO_NUM_0;
static constexpr m5pm1_gpio_num_t PM1_GPIO_IMU_INT   = M5PM1_GPIO_NUM_4;
static constexpr m5pm1_gpio_num_t PM1_GPIO_PY_IRQ    = M5PM1_GPIO_NUM_1;
static constexpr m5pm1_gpio_num_t PM1_GPIO_LORA_EN   = M5PM1_GPIO_NUM_2;
static constexpr m5pm1_gpio_num_t PM1_GPIO_BACKLIGHT = M5PM1_GPIO_NUM_3;

static constexpr std::uint8_t IOE1_EINK_EN_PIN      = M5IOE1_PIN_3;
static constexpr std::uint8_t IOE1_NFC_EN_PIN       = M5IOE1_PIN_4;
static constexpr std::uint8_t IOE1_EINK_RST_PIN     = M5IOE1_PIN_5;
static constexpr std::uint8_t IOE1_TOUCH_RST_PIN    = M5IOE1_PIN_6;
static constexpr std::uint8_t IOE1_RGB_GREEN_PIN    = M5IOE1_PIN_8;
static constexpr std::uint8_t IOE1_RGB_BLUE_PIN     = M5IOE1_PIN_9;
static constexpr std::uint8_t IOE1_LORA_RST_PIN     = M5IOE1_PIN_10;
static constexpr std::uint8_t IOE1_PDM_EN_PIN       = M5IOE1_PIN_12;
static constexpr std::uint8_t IOE1_TOUCH_EN_PIN     = M5IOE1_PIN_13;
static constexpr std::uint8_t IOE1_TF_EN_PIN        = M5IOE1_PIN_14;
static constexpr std::uint8_t IOE1_RGB_GREEN_PWM_CH = M5IOE1_PWM_CH2;
static constexpr std::uint8_t IOE1_RGB_BLUE_PWM_CH  = M5IOE1_PWM_CH1;

static constexpr BuzzerTone IMU_WAKE_PRESS_TONES[] = {
    {.start_freq_hz = 720, .end_freq_hz = 920, .duration_ms = 18, .gap_ms = 4},
    {.start_freq_hz = 1040, .end_freq_hz = 1040, .duration_ms = 18, .gap_ms = 0},
};

static constexpr BuzzerTone TOUCH_WAKE_PRESS_TONES[] = {
    {.start_freq_hz = 1180, .end_freq_hz = 980, .duration_ms = 14, .gap_ms = 3},
    {.start_freq_hz = 1320, .end_freq_hz = 1320, .duration_ms = 12, .gap_ms = 0},
};

static constexpr BuzzerTone RTC_WAKE_PRESS_TONES[] = {
    {.start_freq_hz = 880, .end_freq_hz = 880, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 880, .end_freq_hz = 880, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 1180, .end_freq_hz = 1180, .duration_ms = 14, .gap_ms = 0},
};

static constexpr BuzzerTone IMU_SLEEP_SINK_TONES[] = {
    {.start_freq_hz = 980, .end_freq_hz = 620, .duration_ms = 36, .gap_ms = 4},
    {.start_freq_hz = 520, .end_freq_hz = 360, .duration_ms = 42, .gap_ms = 0},
};

static constexpr BuzzerTone TOUCH_SLEEP_SINK_TONES[] = {
    {.start_freq_hz = 1260, .end_freq_hz = 760, .duration_ms = 34, .gap_ms = 4},
    {.start_freq_hz = 640, .end_freq_hz = 320, .duration_ms = 46, .gap_ms = 0},
};

static constexpr BuzzerTone RTC_SLEEP_SINK_TONES[] = {
    {.start_freq_hz = 1120, .end_freq_hz = 1120, .duration_ms = 10, .gap_ms = 7},
    {.start_freq_hz = 820, .end_freq_hz = 540, .duration_ms = 34, .gap_ms = 4},
    {.start_freq_hz = 480, .end_freq_hz = 300, .duration_ms = 42, .gap_ms = 0},
};

/* -------------------------------------------------------------------------- */
/*                               BMI270 Helpers                               */
/* -------------------------------------------------------------------------- */

struct Bmi270BusContext {
    std::uint8_t addr = BMI270_ADDR_PRIMARY;
};

static int8_t bmi270I2cRead(std::uint8_t reg_addr, std::uint8_t* reg_data, std::uint32_t len, void* intf_ptr)
{
    if (reg_data == nullptr || intf_ptr == nullptr || len == 0) {
        return BMI2_E_COM_FAIL;
    }

    const auto* bus = static_cast<const Bmi270BusContext*>(intf_ptr);
    return M5.In_I2C.readRegister(bus->addr, reg_addr, reg_data, len, BMI270_I2C_FREQ_HZ) ? BMI2_INTF_RET_SUCCESS
                                                                                          : BMI2_E_COM_FAIL;
}

static int8_t bmi270I2cWrite(std::uint8_t reg_addr, const std::uint8_t* reg_data, std::uint32_t len, void* intf_ptr)
{
    if (reg_data == nullptr || intf_ptr == nullptr || len == 0) {
        return BMI2_E_COM_FAIL;
    }

    const auto* bus = static_cast<const Bmi270BusContext*>(intf_ptr);
    return M5.In_I2C.writeRegister(bus->addr, reg_addr, reg_data, len, BMI270_I2C_FREQ_HZ) ? BMI2_INTF_RET_SUCCESS
                                                                                           : BMI2_E_COM_FAIL;
}

static void bmi270DelayUs(std::uint32_t period, void* intf_ptr)
{
    (void)intf_ptr;
    esp_rom_delay_us(period);
}

static void initBmi270Device(struct bmi2_dev* dev, Bmi270BusContext* bus)
{
    *dev                 = {};
    dev->chip_id         = bus->addr;
    dev->read            = bmi270I2cRead;
    dev->write           = bmi270I2cWrite;
    dev->delay_us        = bmi270DelayUs;
    dev->intf            = BMI2_I2C_INTF;
    dev->intf_ptr        = bus;
    dev->read_write_len  = 30;
    dev->config_file_ptr = nullptr;
}

static bool readBmi270IntStatus(std::uint8_t bmi_addr, std::uint16_t* status)
{
    std::uint8_t data[2] = {};
    if (!M5.In_I2C.readRegister(bmi_addr, BMI2_INT_STATUS_0_ADDR, data, sizeof(data), BMI270_I2C_FREQ_HZ)) {
        return false;
    }
    *status = static_cast<std::uint16_t>(data[0] | (static_cast<std::uint16_t>(data[1]) << 8));
    return true;
}

static bool checkBmi270Result(const char* step, int8_t result)
{
    if (result != BMI2_OK) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "%s failed: %d", step, static_cast<int>(result));
        return false;
    }
    return true;
}

static bool configureBmi270AnyMotion(std::uint8_t* configured_addr)
{
    Bmi270BusContext bus;
    struct bmi2_dev dev = {};

    int8_t result = BMI2_E_DEV_NOT_FOUND;
    for (const std::uint8_t candidate : {BMI270_ADDR_PRIMARY, BMI270_ADDR_SECONDARY}) {
        bus.addr = candidate;
        initBmi270Device(&dev, &bus);
        result = bmi270_init(&dev);
        if (result == BMI2_OK) {
            break;
        }
        ESP_LOGW(APP_SLEEP_WAKE_TAG, "BMI270 init addr=0x%02X failed: %d", candidate, static_cast<int>(result));
    }

    if (result != BMI2_OK) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "BMI270 not found");
        return false;
    }

    ESP_LOGI(APP_SLEEP_WAKE_TAG, "Configure BMI270 any-motion wake addr=0x%02X", bus.addr);

    struct bmi2_sens_config base_config[2] = {};
    base_config[0].type                    = BMI2_ACCEL;
    base_config[1].type                    = BMI2_GYRO;
    result                                 = bmi270_get_sensor_config(base_config, 2, &dev);
    if (!checkBmi270Result("BMI270 get accel/gyro config", result)) return false;

    base_config[0].cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    base_config[0].cfg.acc.bwp         = BMI2_ACC_OSR2_AVG2;
    base_config[0].cfg.acc.odr         = BMI2_ACC_ODR_100HZ;
    base_config[0].cfg.acc.range       = BMI2_ACC_RANGE_2G;
    base_config[1].cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    base_config[1].cfg.gyr.noise_perf  = BMI2_PERF_OPT_MODE;
    base_config[1].cfg.gyr.bwp         = BMI2_GYR_OSR2_MODE;
    base_config[1].cfg.gyr.odr         = BMI2_GYR_ODR_100HZ;
    base_config[1].cfg.gyr.range       = BMI2_GYR_RANGE_2000;
    base_config[1].cfg.gyr.ois_range   = BMI2_GYR_OIS_2000;
    result                             = bmi270_set_sensor_config(base_config, 2, &dev);
    if (!checkBmi270Result("BMI270 set accel/gyro config", result)) return false;

    std::uint8_t base_sensor_list[] = {BMI2_ACCEL, BMI2_GYRO};
    result                          = bmi270_sensor_enable(base_sensor_list, 2, &dev);
    if (!checkBmi270Result("BMI270 enable accel/gyro", result)) return false;

    struct bmi2_int_pin_config int_pin_config = {};
    int_pin_config.pin_type                   = BMI2_INT1;
    int_pin_config.int_latch                  = BMI2_INT_NON_LATCH;
    int_pin_config.pin_cfg[0].lvl             = BMI2_INT_ACTIVE_LOW;
    int_pin_config.pin_cfg[0].od              = BMI2_INT_PUSH_PULL;
    int_pin_config.pin_cfg[0].output_en       = BMI2_INT_OUTPUT_ENABLE;
    int_pin_config.pin_cfg[0].input_en        = BMI2_INT_INPUT_DISABLE;
    result                                    = bmi2_set_int_pin_config(&int_pin_config, &dev);
    if (!checkBmi270Result("BMI270 set INT1 config", result)) return false;

    struct bmi2_sens_config any_motion_config = {};
    any_motion_config.type                    = BMI2_ANY_MOTION;
    result                                    = bmi270_get_sensor_config(&any_motion_config, 1, &dev);
    if (!checkBmi270Result("BMI270 get any-motion config", result)) return false;

    any_motion_config.cfg.any_motion.threshold = BMI270_ANY_MOTION_THRESHOLD;
    any_motion_config.cfg.any_motion.duration  = BMI270_ANY_MOTION_DURATION;
    any_motion_config.cfg.any_motion.select_x  = BMI2_ENABLE;
    any_motion_config.cfg.any_motion.select_y  = BMI2_ENABLE;
    any_motion_config.cfg.any_motion.select_z  = BMI2_ENABLE;
    result                                     = bmi270_set_sensor_config(&any_motion_config, 1, &dev);
    if (!checkBmi270Result("BMI270 set any-motion config", result)) return false;

    std::uint8_t sensor_list[] = {BMI2_ACCEL, BMI2_ANY_MOTION};
    result                     = bmi270_sensor_enable(sensor_list, 2, &dev);
    if (!checkBmi270Result("BMI270 enable accel/any-motion", result)) return false;

    result = bmi2_map_feat_int(BMI2_ANY_MOTION, BMI2_INT1, &dev);
    if (!checkBmi270Result("BMI270 map any-motion INT1", result)) return false;

    std::uint16_t int_status = 0;
    result                   = bmi2_get_int_status(&int_status, &dev);
    if (!checkBmi270Result("BMI270 get int status", result)) return false;

    ESP_LOGI(APP_SLEEP_WAKE_TAG, "BMI270 any-motion wake configured threshold=%u duration=%u int_status=0x%04X",
             BMI270_ANY_MOTION_THRESHOLD, BMI270_ANY_MOTION_DURATION, int_status);
    *configured_addr = bus.addr;
    return true;
}

/* -------------------------------------------------------------------------- */
/*                              Wake Helpers                                  */
/* -------------------------------------------------------------------------- */

static bool checkPm1Result(const char* step, m5pm1_err_t err)
{
    if (err != M5PM1_OK) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "%s failed: %d", step, static_cast<int>(err));
        return false;
    }
    return true;
}

static bool waitTouchWakeLineIdle()
{
    const std::uint32_t start_ms = GetHAL().millis();
    while (GetHAL().millis() - start_ms < TOUCH_RELEASE_TIMEOUT_MS) {
        M5.update();
        if (!GetHAL().isTouchPressed() && gpio_get_level(TOUCH_WAKE_GPIO) != 0) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(TOUCH_RELEASE_POLL_MS));
    }

    ESP_LOGW(APP_SLEEP_WAKE_TAG, "Touch wake line still active before sleep");
    return false;
}

static void isolateRtcGpioIfValid(gpio_num_t gpio)
{
    if (rtc_gpio_is_valid_gpio(gpio)) {
        rtc_gpio_isolate(gpio);
    }
}

static void playWakeTone(const BuzzerTone* tones, std::size_t count)
{
    GetHAL().stopBuzzer();
    GetHAL().playBuzzerPattern(tones, count);
}

static void playWakeToneAndWait(const BuzzerTone* tones, std::size_t count)
{
    GetHAL().playBuzzerPattern(tones, count);

    std::uint32_t total_ms = 0;
    for (std::size_t i = 0; i < count; ++i) {
        total_ms += tones[i].duration_ms;
        if (i + 1 < count) {
            total_ms += tones[i].gap_ms;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(total_ms + 20));
    GetHAL().stopBuzzer();
}

static void clearRtcWakeTimer(m5::RTC_Class& rtc)
{
    rtc.setTimerIRQ(0);
    rtc.disableIRQ();
    rtc.clearIRQ();
}

static bool waitRtcWakeLineIdle(M5PM1& pm1, m5::RTC_Class& rtc)
{
    const std::uint32_t start_ms = GetHAL().millis();
    while (GetHAL().millis() - start_ms < RTC_WAKE_LINE_IDLE_TIMEOUT_MS) {
        rtc.clearIRQ();
        pm1.irqClearGpioAll();
        pm1.irqClearSysAll();

        std::uint8_t g0_level = 0;
        if (!checkPm1Result("PM1 read RTC G0", pm1.gpioGetInput(PM1_GPIO_RTC_INT, &g0_level))) {
            return false;
        }

        const bool rtc_irq = rtc.getIRQstatus();
        if (g0_level != 0 && !rtc_irq) {
            ESP_LOGI(APP_SLEEP_WAKE_TAG, "RTC wake line idle G0=%u rtc_irq=%u", g0_level, rtc_irq ? 1 : 0);
            return true;
        }

        ESP_LOGW(APP_SLEEP_WAKE_TAG, "Waiting RTC wake line idle G0=%u rtc_irq=%u", g0_level, rtc_irq ? 1 : 0);
        vTaskDelay(pdMS_TO_TICKS(RTC_WAKE_LINE_POLL_MS));
    }

    ESP_LOGE(APP_SLEEP_WAKE_TAG, "RTC wake line still active, shutdown canceled");
    return false;
}

static bool waitImuWakeLineIdle(M5PM1& pm1, std::uint8_t bmi_addr)
{
    const std::uint32_t start_ms = GetHAL().millis();
    while (GetHAL().millis() - start_ms < IMU_WAKE_LINE_IDLE_TIMEOUT_MS) {
        std::uint8_t g4_level = 0;
        if (!checkPm1Result("PM1 read IMU G4", pm1.gpioGetInput(PM1_GPIO_IMU_INT, &g4_level))) {
            return false;
        }

        std::uint16_t int_status = 0;
        const bool int_ok        = readBmi270IntStatus(bmi_addr, &int_status);
        if (g4_level != 0 && int_ok && (int_status & BMI270_ANY_MOTION_STATUS_MASK) == 0) {
            ESP_LOGI(APP_SLEEP_WAKE_TAG, "IMU wake line idle G4=%u int_status=0x%04X", g4_level, int_status);
            return true;
        }

        ESP_LOGW(APP_SLEEP_WAKE_TAG, "Waiting IMU wake line idle G4=%u int_status=%s0x%04X", g4_level,
                 int_ok ? "" : "read_failed:", int_status);
        vTaskDelay(pdMS_TO_TICKS(IMU_WAKE_LINE_POLL_MS));
    }

    ESP_LOGE(APP_SLEEP_WAKE_TAG, "IMU wake line still active, shutdown canceled");
    return false;
}

/* -------------------------------------------------------------------------- */
/*                                 Lifecycle                                  */
/* -------------------------------------------------------------------------- */

void AppSleepWake::onCreate()
{
    setAppInfo().name = "AppSleepWake";
    open();
}

void AppSleepWake::onOpen()
{
    _has_drawn = true;
}

void AppSleepWake::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const std::uint32_t now_ms = GetHAL().millis();
    if (now_ms - _last_input_ms < WAKE_TOUCH_COOLDOWN_MS) {
        return;
    }

    if (GetHAL().wasTouchClickedArea(IMU_WAKE_X, IMU_WAKE_Y, IMU_WAKE_W, IMU_WAKE_H)) {
        _last_input_ms = now_ms;
        playWakeTone(IMU_WAKE_PRESS_TONES, sizeof(IMU_WAKE_PRESS_TONES) / sizeof(IMU_WAKE_PRESS_TONES[0]));
        drawPressedImage(img_SLEEP_IMU_WAKE_ON_start, img_SLEEP_IMU_WAKE_ON_end, IMU_WAKE_X, IMU_WAKE_Y, IMU_WAKE_W,
                         IMU_WAKE_H);
        enterImuWakeShutdown();
        return;
    }

    if (GetHAL().wasTouchClickedArea(TOUCH_WAKE_X, TOUCH_WAKE_Y, TOUCH_WAKE_W, TOUCH_WAKE_H)) {
        _last_input_ms = now_ms;
        playWakeTone(TOUCH_WAKE_PRESS_TONES, sizeof(TOUCH_WAKE_PRESS_TONES) / sizeof(TOUCH_WAKE_PRESS_TONES[0]));
        drawPressedImage(img_SLEEP_TOUCH_WAKE_ON_start, img_SLEEP_TOUCH_WAKE_ON_end, TOUCH_WAKE_X, TOUCH_WAKE_Y,
                         TOUCH_WAKE_W, TOUCH_WAKE_H);
        enterTouchWakeDeepSleep();
        return;
    }

    if (GetHAL().wasTouchClickedArea(RTC_WAKE_X, RTC_WAKE_Y, RTC_WAKE_W, RTC_WAKE_H)) {
        _last_input_ms = now_ms;
        playWakeTone(RTC_WAKE_PRESS_TONES, sizeof(RTC_WAKE_PRESS_TONES) / sizeof(RTC_WAKE_PRESS_TONES[0]));
        drawPressedImage(img_SLEEP_10Sec_WAKE_ON_start, img_SLEEP_10Sec_WAKE_ON_end, RTC_WAKE_X, RTC_WAKE_Y, RTC_WAKE_W,
                         RTC_WAKE_H);
        enterRtc10sWakeShutdown();
        return;
    }

    if (!_has_drawn || GetHAL().isFullRedrawRequested()) {
        _has_drawn = true;
    }
}

void AppSleepWake::drawPressedImage(const std::uint8_t* start, const std::uint8_t* end, int x, int y, int w, int h)
{
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fast);
    GetHAL().display.drawPng(start, end - start, x, y);

    GetHAL().flushDisplay(x, y, w, h);
    GetHAL().display.waitDisplay();
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);

    _has_drawn = true;
}

/* -------------------------------------------------------------------------- */
/*                              Shutdown Paths                                */
/* -------------------------------------------------------------------------- */

void AppSleepWake::enterImuWakeShutdown()
{
    GetHAL().stopBuzzer();

    auto& pm1           = GetHAL().pm1;
    bool config_success = true;
    config_success &= checkPm1Result("PM1 disable RTC wake G0", pm1.gpioSetWakeEnable(PM1_GPIO_RTC_INT, false));
    config_success &= checkPm1Result("PM1 disable IMU wake G4", pm1.gpioSetWakeEnable(PM1_GPIO_IMU_INT, false));
    config_success &= checkPm1Result("PM1 clear GPIO IRQ", pm1.irqClearGpioAll());
    config_success &= checkPm1Result("PM1 clear SYS IRQ", pm1.irqClearSysAll());
    config_success &= checkPm1Result("PM1 mask RTC GPIO0", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO0, M5PM1_IRQ_MASK_ENABLE));
    config_success &=
        checkPm1Result("PM1 unmask IMU GPIO4", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_DISABLE));
    config_success &= checkPm1Result("PM1 mask GPIO2", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO2, M5PM1_IRQ_MASK_ENABLE));
    config_success &= checkPm1Result("PM1 mask GPIO3", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO3, M5PM1_IRQ_MASK_ENABLE));
    config_success &= checkPm1Result("PM1 G4 GPIO input", pm1.gpioSet(PM1_GPIO_IMU_INT, M5PM1_GPIO_MODE_INPUT, 0,
                                                                      M5PM1_GPIO_PULL_UP, M5PM1_GPIO_DRIVE_PUSHPULL));
    config_success &=
        checkPm1Result("PM1 G4 wake edge", pm1.gpioSetWakeEdge(PM1_GPIO_IMU_INT, M5PM1_GPIO_WAKE_FALLING));
    if (!config_success) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "PM1 IMU wake pre-config failed");
        return;
    }

    std::uint8_t wake_src = 0;
    std::uint8_t irq_gpio = 0;
    std::uint8_t irq_btn  = 0;
    std::uint8_t irq_sys  = 0;
    pm1.getWakeSource(&wake_src, M5PM1_CLEAN_ALL);
    pm1.irqGetGpioStatus(&irq_gpio, M5PM1_CLEAN_ALL);
    pm1.irqGetBtnStatus(&irq_btn, M5PM1_CLEAN_ALL);
    pm1.irqGetSysStatus(&irq_sys, M5PM1_CLEAN_ALL);
    ESP_LOGI(APP_SLEEP_WAKE_TAG, "PM1 test reset wake=0x%02X gpio_irq=0x%02X btn_irq=0x%02X sys_irq=0x%02X", wake_src,
             irq_gpio, irq_btn, irq_sys);

    std::uint8_t bmi_addr = 0;
    if (!configureBmi270AnyMotion(&bmi_addr)) {
        return;
    }

    if (!waitImuWakeLineIdle(pm1, bmi_addr)) {
        return;
    }

    config_success &= checkPm1Result("PM1 setLdoEnable", pm1.setLdoEnable(true));
    config_success &= checkPm1Result("PM1 ldoSetPowerHold", pm1.ldoSetPowerHold(true));
    config_success &= checkPm1Result("PM1 setLedEnLevel", pm1.setLedEnLevel(true));
    config_success &= checkPm1Result("PM1 clear GPIO IRQ before wake", pm1.irqClearGpioAll());
    config_success &= checkPm1Result("PM1 clear SYS IRQ before wake", pm1.irqClearSysAll());

    wake_src = 0;
    irq_gpio = 0;
    irq_btn  = 0;
    irq_sys  = 0;
    config_success &= checkPm1Result("PM1 clean wake source", pm1.getWakeSource(&wake_src, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 clean GPIO IRQ", pm1.irqGetGpioStatus(&irq_gpio, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 clean button IRQ", pm1.irqGetBtnStatus(&irq_btn, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 clean system IRQ", pm1.irqGetSysStatus(&irq_sys, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 enable IMU G4 wake", pm1.gpioSetWakeEnable(PM1_GPIO_IMU_INT, true));

    if (!config_success) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "PM1 IMU wake setup failed, shutdown canceled");
        return;
    }

    if (!waitImuWakeLineIdle(pm1, bmi_addr)) {
        pm1.gpioSetWakeEnable(PM1_GPIO_IMU_INT, false);
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "IMU wake line became active before shutdown, canceled");
        return;
    }

    ESP_LOGI(APP_SLEEP_WAKE_TAG, "PM1 shutdown, wake by BMI270 INT1 -> PM1 G4 falling edge");
    vTaskDelay(pdMS_TO_TICKS(WAKE_IMAGE_HOLD_MS));
    playWakeToneAndWait(IMU_SLEEP_SINK_TONES, sizeof(IMU_SLEEP_SINK_TONES) / sizeof(IMU_SLEEP_SINK_TONES[0]));
    GetHAL().powerSaveDisplay();
    vTaskDelay(pdMS_TO_TICKS(100));
    checkPm1Result("PM1 shutdown", pm1.shutdown());
}

void AppSleepWake::enterTouchWakeDeepSleep()
{
    GetHAL().stopBuzzer();
    GetHAL().unmountTfCard();

    const gpio_config_t touch_gpio_config = {
        .pin_bit_mask = 1ULL << TOUCH_WAKE_GPIO,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&touch_gpio_config);
    if (err != ESP_OK) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "Touch wake GPIO config failed: %s", esp_err_to_name(err));
        return;
    }

    if (!waitTouchWakeLineIdle()) {
        return;
    }

    auto& pm1           = GetHAL().pm1;
    bool config_success = true;
    config_success &= checkPm1Result("PM1 disable idle sleep before deep sleep", pm1.setI2cSleepTime(0));
    config_success &= checkPm1Result("PM1 disable RTC wake G0", pm1.gpioSetWakeEnable(PM1_GPIO_RTC_INT, false));
    config_success &= checkPm1Result("PM1 disable IMU wake G4", pm1.gpioSetWakeEnable(PM1_GPIO_IMU_INT, false));
    config_success &= checkPm1Result("PM1 clear GPIO IRQ", pm1.irqClearGpioAll());
    config_success &= checkPm1Result("PM1 clear SYS IRQ", pm1.irqClearSysAll());
    config_success &= checkPm1Result("PM1 red LED off", pm1.setLedEnLevel(false));
    config_success &= checkPm1Result("PM1 boost off", pm1.setBoostEnable(false));
    config_success &= checkPm1Result(
        "PM1 G0 low-power input",
        pm1.gpioSet(PM1_GPIO_RTC_INT, M5PM1_GPIO_MODE_INPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_OPENDRAIN));
    config_success &= checkPm1Result(
        "PM1 G1 low-power input",
        pm1.gpioSet(PM1_GPIO_PY_IRQ, M5PM1_GPIO_MODE_INPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_OPENDRAIN));
    config_success &= checkPm1Result(
        "PM1 G2 low-power input",
        pm1.gpioSet(PM1_GPIO_LORA_EN, M5PM1_GPIO_MODE_INPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_OPENDRAIN));
    config_success &= checkPm1Result(
        "PM1 G4 low-power input",
        pm1.gpioSet(PM1_GPIO_IMU_INT, M5PM1_GPIO_MODE_INPUT, 0, M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_OPENDRAIN));
    if (!config_success) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "PM1 touch deep sleep setup failed");
        pm1.setI2cSleepTime(0);
        return;
    }

    auto& ioe1                       = GetHAL().ioe1;
    m5ioe1_err_t ioe_err             = M5IOE1_OK;
    const std::uint8_t output_pins[] = {IOE1_EINK_EN_PIN,   IOE1_NFC_EN_PIN,  IOE1_EINK_RST_PIN, IOE1_TOUCH_RST_PIN,
                                        IOE1_LORA_RST_PIN,  IOE1_PDM_EN_PIN,  IOE1_TOUCH_EN_PIN, IOE1_TF_EN_PIN,
                                        IOE1_RGB_GREEN_PIN, IOE1_RGB_BLUE_PIN};
    for (const auto pin : output_pins) {
        ioe1.pinMode(pin, OUTPUT);
        ioe1.setDriveMode(pin, M5IOE1_DRIVE_PUSHPULL);
    }
    ioe1.setPwmDuty(IOE1_RGB_GREEN_PWM_CH, 0, false, false);
    ioe1.setPwmDuty(IOE1_RGB_BLUE_PWM_CH, 0, false, false);
    ioe1.digitalWriteWithRes(IOE1_NFC_EN_PIN, LOW, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_TF_EN_PIN, LOW, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_PDM_EN_PIN, LOW, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_LORA_RST_PIN, LOW, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_RGB_GREEN_PIN, LOW, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_RGB_BLUE_PIN, LOW, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_TOUCH_EN_PIN, HIGH, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_TOUCH_RST_PIN, HIGH, &ioe_err);
    if (ioe_err != M5IOE1_OK) {
        ESP_LOGW(APP_SLEEP_WAKE_TAG, "IOE1 low-power GPIO setup had error: %d", static_cast<int>(ioe_err));
    }

    ioe1.setI2cConfig(PM1_IDLE_SLEEP_SECONDS, M5IOE1_I2C_SPEED_100K, M5IOE1_WAKE_EDGE_RISING, M5IOE1_PULL_ENABLED);
    checkPm1Result("PM1 enable idle sleep", pm1.setI2cSleepTime(PM1_IDLE_SLEEP_SECONDS));

    err = esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (err != ESP_OK) {
        ESP_LOGW(APP_SLEEP_WAKE_TAG, "Disable previous wake sources failed: %s", esp_err_to_name(err));
    }

    rtc_gpio_deinit(TOUCH_WAKE_GPIO);
    rtc_gpio_init(TOUCH_WAKE_GPIO);
    rtc_gpio_set_direction(TOUCH_WAKE_GPIO, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_dis(TOUCH_WAKE_GPIO);
    rtc_gpio_pullup_en(TOUCH_WAKE_GPIO);
    err = esp_sleep_enable_ext0_wakeup(TOUCH_WAKE_GPIO, 0);
    if (err != ESP_OK) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "Touch EXT0 wake enable failed: %s", esp_err_to_name(err));
        pm1.sendWakeSignal();
        vTaskDelay(pdMS_TO_TICKS(10));
        pm1.setI2cSleepTime(0);
        return;
    }

    isolateRtcGpioIfValid(USER_BUTTON1_GPIO);
    isolateRtcGpioIfValid(USER_BUTTON2_GPIO);
    isolateRtcGpioIfValid(LORA_IRQ_GPIO);
    isolateRtcGpioIfValid(RFID_IRQ_GPIO);
    isolateRtcGpioIfValid(PM1_IRQ_GPIO);

    playWakeToneAndWait(TOUCH_SLEEP_SINK_TONES, sizeof(TOUCH_SLEEP_SINK_TONES) / sizeof(TOUCH_SLEEP_SINK_TONES[0]));
    gpio_set_direction(BUZZER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(BUZZER_GPIO, 0);
    GetHAL().display.setBrightness(0);
    ioe1.digitalWriteWithRes(IOE1_EINK_EN_PIN, HIGH, &ioe_err);
    ioe1.digitalWriteWithRes(IOE1_EINK_RST_PIN, HIGH, &ioe_err);
    checkPm1Result("PM1 backlight GPIO func", pm1.gpioSetFunc(PM1_GPIO_BACKLIGHT, M5PM1_GPIO_FUNC_GPIO));
    checkPm1Result("PM1 G3 backlight off", pm1.gpioSet(PM1_GPIO_BACKLIGHT, M5PM1_GPIO_MODE_INPUT, 0,
                                                       M5PM1_GPIO_PULL_NONE, M5PM1_GPIO_DRIVE_OPENDRAIN));

    ESP_LOGI(APP_SLEEP_WAKE_TAG, "ESP32 deep sleep, wake by touch INT GPIO%d low", TOUCH_WAKE_GPIO);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_deep_sleep_start();
}

void AppSleepWake::enterRtc10sWakeShutdown()
{
    GetHAL().stopBuzzer();

    auto& rtc = GetHAL().rtc;
    if (!rtc.isEnabled()) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "RTC is not enabled, shutdown canceled");
        return;
    }

    clearRtcWakeTimer(rtc);

    auto& pm1           = GetHAL().pm1;
    bool config_success = true;
    config_success &= checkPm1Result("PM1 disable RTC wake G0", pm1.gpioSetWakeEnable(PM1_GPIO_RTC_INT, false));
    config_success &= checkPm1Result("PM1 disable IMU wake G4", pm1.gpioSetWakeEnable(PM1_GPIO_IMU_INT, false));
    config_success &= checkPm1Result("PM1 clear GPIO IRQ", pm1.irqClearGpioAll());
    config_success &= checkPm1Result("PM1 clear SYS IRQ", pm1.irqClearSysAll());
    config_success &=
        checkPm1Result("PM1 unmask RTC GPIO0", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO0, M5PM1_IRQ_MASK_DISABLE));
    config_success &= checkPm1Result("PM1 mask IMU GPIO4", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO4, M5PM1_IRQ_MASK_ENABLE));
    config_success &= checkPm1Result("PM1 mask GPIO2", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO2, M5PM1_IRQ_MASK_ENABLE));
    config_success &= checkPm1Result("PM1 mask GPIO3", pm1.irqSetGpioMask(M5PM1_IRQ_GPIO3, M5PM1_IRQ_MASK_ENABLE));
    config_success &= checkPm1Result("PM1 G0 GPIO input", pm1.gpioSet(PM1_GPIO_RTC_INT, M5PM1_GPIO_MODE_INPUT, 0,
                                                                      M5PM1_GPIO_PULL_UP, M5PM1_GPIO_DRIVE_PUSHPULL));
    config_success &=
        checkPm1Result("PM1 G0 wake edge", pm1.gpioSetWakeEdge(PM1_GPIO_RTC_INT, M5PM1_GPIO_WAKE_FALLING));
    config_success &= checkPm1Result("PM1 ldoSetPowerHold off", pm1.ldoSetPowerHold(false));

    if (!config_success) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "PM1 RTC wake pre-config failed");
        return;
    }

    std::uint8_t wake_src = 0;
    std::uint8_t irq_gpio = 0;
    std::uint8_t irq_btn  = 0;
    std::uint8_t irq_sys  = 0;
    pm1.getWakeSource(&wake_src, M5PM1_CLEAN_ALL);
    pm1.irqGetGpioStatus(&irq_gpio, M5PM1_CLEAN_ALL);
    pm1.irqGetBtnStatus(&irq_btn, M5PM1_CLEAN_ALL);
    pm1.irqGetSysStatus(&irq_sys, M5PM1_CLEAN_ALL);
    rtc.clearIRQ();

    if (!waitRtcWakeLineIdle(pm1, rtc)) {
        return;
    }

    const std::uint32_t actual_ms = rtc.setTimerIRQ(RTC_WAKE_TIMER_MS);
    if (actual_ms == 0) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "RTC setTimerIRQ failed, shutdown canceled");
        return;
    }
    ESP_LOGI(APP_SLEEP_WAKE_TAG, "RTC wake timer set: requested=%ums actual=%ums", RTC_WAKE_TIMER_MS, actual_ms);

    config_success &= checkPm1Result("PM1 clean wake source", pm1.getWakeSource(&wake_src, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 clean GPIO IRQ", pm1.irqGetGpioStatus(&irq_gpio, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 clean button IRQ", pm1.irqGetBtnStatus(&irq_btn, M5PM1_CLEAN_ALL));
    config_success &= checkPm1Result("PM1 clean system IRQ", pm1.irqGetSysStatus(&irq_sys, M5PM1_CLEAN_ALL));

    if (!waitRtcWakeLineIdle(pm1, rtc)) {
        return;
    }

    config_success &= checkPm1Result("PM1 enable RTC G0 wake", pm1.gpioSetWakeEnable(PM1_GPIO_RTC_INT, true));

    if (!config_success) {
        ESP_LOGE(APP_SLEEP_WAKE_TAG, "PM1 RTC wake setup failed, shutdown canceled");
        return;
    }

    if (!waitRtcWakeLineIdle(pm1, rtc)) {
        pm1.gpioSetWakeEnable(PM1_GPIO_RTC_INT, false);
        return;
    }

    ESP_LOGI(APP_SLEEP_WAKE_TAG, "PM1 shutdown, wake by RX8130 timer -> PM1 G0 falling edge");
    vTaskDelay(pdMS_TO_TICKS(WAKE_IMAGE_HOLD_MS));
    playWakeToneAndWait(RTC_SLEEP_SINK_TONES, sizeof(RTC_SLEEP_SINK_TONES) / sizeof(RTC_SLEEP_SINK_TONES[0]));
    GetHAL().powerSaveDisplay();
    vTaskDelay(pdMS_TO_TICKS(100));
    checkPm1Result("PM1 shutdown", pm1.shutdown());
}

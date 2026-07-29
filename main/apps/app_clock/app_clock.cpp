/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <hal.h>
#include <esp_sntp.h>
#include <sys/time.h>
#include <time.h>

/* -------------------------------------------------------------------------- */
/*                                  Constants                                 */
/* -------------------------------------------------------------------------- */

static constexpr std::uint32_t CLOCK_REFRESH_MS           = 60000;
static constexpr std::uint32_t CLOCK_MINUTE_CHECK_MS      = 1000;
static constexpr std::uint32_t TIMEZONE_TOUCH_COOLDOWN_MS = 300;
static constexpr std::uint8_t TIMEZONE_RAM_INDEX          = 2;
static constexpr std::uint8_t TIMEZONE_RAM_TAG            = 0xD0;
static constexpr std::uint8_t TIMEZONE_RAM_TAG_MASK       = 0xF0;
static constexpr std::uint8_t TIMEZONE_RAM_VALUE_MASK     = 0x0F;

static constexpr int TIME_X = 270;
static constexpr int TIME_Y = 548;
static constexpr int TIME_W = 388 - 261;
static constexpr int TIME_H = 616 - 550;

static constexpr int TIMEZONE_X = 407;
static constexpr int TIMEZONE_Y = 583;
static constexpr int TIMEZONE_W = 469 - 407;
static constexpr int TIMEZONE_H = 613 - 583;

static constexpr int DATE_X = 256;
static constexpr int DATE_Y = 618;
static constexpr int DATE_W = 371 - 256;
static constexpr int DATE_H = 660 - 618;

static constexpr int NTP_BUTTON_X = 373;
static constexpr int NTP_BUTTON_Y = 614;
static constexpr int NTP_BUTTON_W = 475 - 373;
static constexpr int NTP_BUTTON_H = 661 - 614;

static constexpr int NTP_DIALOG_X                      = 20;
static constexpr int NTP_DIALOG_Y                      = 172;
static constexpr int NTP_DIALOG_W                      = 440;
static constexpr int NTP_DIALOG_H                      = 520;
static constexpr int NTP_FIELD_X                       = NTP_DIALOG_X + 22;
static constexpr int NTP_FIELD_W                       = NTP_DIALOG_W - 44;
static constexpr int NTP_SSID_Y                        = NTP_DIALOG_Y + 78;
static constexpr int NTP_PASS_Y                        = NTP_DIALOG_Y + 142;
static constexpr int NTP_FIELD_H                       = 36;
static constexpr int NTP_TIMEZONE_Y                    = NTP_DIALOG_Y + 198;
static constexpr int NTP_TIMEZONE_VALUE_W              = 132;
static constexpr int NTP_TIMEZONE_VALUE_X              = NTP_DIALOG_X + NTP_DIALOG_W - 22 - NTP_TIMEZONE_VALUE_W;
static constexpr int NTP_AUTO_Y                        = NTP_DIALOG_Y + 246;
static constexpr int NTP_AUTO_H                        = 34;
static constexpr int NTP_AUTO_TOGGLE_W                 = 70;
static constexpr int NTP_AUTO_TOGGLE_X                 = NTP_DIALOG_X + NTP_DIALOG_W - 22 - NTP_AUTO_TOGGLE_W;
static constexpr int NTP_BUTTON_ROW_Y                  = NTP_DIALOG_Y + 294;
static constexpr int NTP_ACTION_W                      = 150;
static constexpr int NTP_ACTION_H                      = 36;
static constexpr int NTP_SCAN_X                        = NTP_FIELD_X;
static constexpr int NTP_SYNC_X                        = NTP_DIALOG_X + NTP_DIALOG_W - 22 - NTP_ACTION_W;
static constexpr int NTP_CLOSE_X                       = NTP_DIALOG_X + NTP_DIALOG_W - 48;
static constexpr int NTP_CLOSE_Y                       = NTP_DIALOG_Y + 8;
static constexpr int NTP_CLOSE_W                       = 32;
static constexpr int NTP_CLOSE_H                       = 32;
static constexpr int NTP_CLOSE_TOUCH_PAD               = 14;
static constexpr int NTP_LIST_X                        = NTP_FIELD_X;
static constexpr int NTP_STATUS_Y                      = NTP_BUTTON_ROW_Y + NTP_ACTION_H + 22;
static constexpr int NTP_LIST_Y                        = NTP_STATUS_Y + 24;
static constexpr int NTP_LIST_W                        = NTP_FIELD_W;
static constexpr int NTP_LIST_ROW_H                    = 30;
static constexpr int NTP_LIST_ROWS                     = 4;
static constexpr int NTP_SCROLL_W                      = 12;
static constexpr std::uint32_t NTP_TOUCH_COOLDOWN_MS   = 250;
static constexpr std::uint32_t NTP_CONNECT_TIMEOUT_MS  = 15000;
static constexpr std::uint32_t NTP_SYNC_TIMEOUT_MS     = 10000;
static constexpr std::uint32_t NTP_SUCCESS_CLOSE_MS    = 3000;
static constexpr std::uint32_t NTP_AUTO_BOOT_DELAY_MS  = 3000;
static constexpr std::uint32_t NTP_AUTO_INTERVAL_MS    = 60 * 60 * 1000;
static constexpr std::uint32_t NTP_VALID_UNIX_TIME     = 1700000000;
static constexpr long RTC_BASE_OFFSET_SEC              = 8 * 60 * 60;
static constexpr time_t NTP_RTC_WRITE_COMPENSATION_SEC = 1;
static constexpr bool NTP_AUTO_SYNC_TEMP_DISABLED      = true;

/* -------------------------------------------------------------------------- */
/*                                    Types                                   */
/* -------------------------------------------------------------------------- */

enum class DstRule : std::uint8_t {
    None,
    Europe,
    UnitedStates,
};

struct TimezoneOption {
    const char* text;
    int standard_offset_minutes;
    DstRule dst_rule;
};

struct ClockDate {
    int year;
    int month;
    int day;
};

struct ClockDateTime {
    ClockDate date;
    int minutes;
};

/* -------------------------------------------------------------------------- */
/*                                  Timezones                                 */
/* -------------------------------------------------------------------------- */

static constexpr std::array<TimezoneOption, 16> TIMEZONES = {{
    {"CN", 8 * 60, DstRule::None},
    {"HK", 8 * 60, DstRule::None},
    {"TW", 8 * 60, DstRule::None},
    {"SG", 8 * 60, DstRule::None},
    {"JP", 9 * 60, DstRule::None},
    {"KR", 9 * 60, DstRule::None},
    {"TH", 7 * 60, DstRule::None},
    {"IN", 5 * 60 + 30, DstRule::None},
    {"UAE", 4 * 60, DstRule::None},
    {"UK", 0, DstRule::Europe},
    {"CET", 1 * 60, DstRule::Europe},
    {"EET", 2 * 60, DstRule::Europe},
    {"USE", -5 * 60, DstRule::UnitedStates},
    {"USC", -6 * 60, DstRule::UnitedStates},
    {"USM", -7 * 60, DstRule::UnitedStates},
    {"USW", -8 * 60, DstRule::UnitedStates},
}};

static constexpr BuzzerTone TIMEZONE_SWITCH_TONES[] = {
    {.start_freq_hz = 1200, .end_freq_hz = 1200, .duration_ms = 6, .gap_ms = 24},
    {.start_freq_hz = 1200, .end_freq_hz = 1200, .duration_ms = 6, .gap_ms = 8},
    {.start_freq_hz = 900, .end_freq_hz = 900, .duration_ms = 16, .gap_ms = 0},
};

static constexpr BuzzerTone NTP_DIALOG_OPEN_TONES[] = {
    {.start_freq_hz = 980, .end_freq_hz = 980, .duration_ms = 8, .gap_ms = 5},
    {.start_freq_hz = 1470, .end_freq_hz = 1470, .duration_ms = 14, .gap_ms = 0},
};

static constexpr BuzzerTone NTP_SELECT_TONES[] = {
    {.start_freq_hz = 1550, .end_freq_hz = 1550, .duration_ms = 9, .gap_ms = 0},
};

static constexpr BuzzerTone NTP_CLOSE_TONES[] = {
    {.start_freq_hz = 1350, .end_freq_hz = 1350, .duration_ms = 6, .gap_ms = 4},
    {.start_freq_hz = 820, .end_freq_hz = 820, .duration_ms = 12, .gap_ms = 0},
};

static constexpr BuzzerTone NTP_SYNC_START_TONES[] = {
    {.start_freq_hz = 1100, .end_freq_hz = 1100, .duration_ms = 7, .gap_ms = 10},
    {.start_freq_hz = 1500, .end_freq_hz = 1500, .duration_ms = 7, .gap_ms = 10},
    {.start_freq_hz = 1900, .end_freq_hz = 1900, .duration_ms = 10, .gap_ms = 0},
};

static constexpr BuzzerTone NTP_SYNC_DONE_TONES[] = {
    {.start_freq_hz = 880, .end_freq_hz = 880, .duration_ms = 9, .gap_ms = 5},
    {.start_freq_hz = 1320, .end_freq_hz = 1320, .duration_ms = 9, .gap_ms = 5},
    {.start_freq_hz = 1760, .end_freq_hz = 1760, .duration_ms = 18, .gap_ms = 0},
};

static constexpr BuzzerTone NTP_FAIL_TONES[] = {
    {.start_freq_hz = 720, .end_freq_hz = 720, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 430, .end_freq_hz = 430, .duration_ms = 18, .gap_ms = 0},
};

/* -------------------------------------------------------------------------- */
/*                               Time Helpers                                 */
/* -------------------------------------------------------------------------- */

static std::uint8_t loadTimezoneIndex()
{
    std::uint8_t value = 0;
    if (GetHAL().rx8130RamRead(TIMEZONE_RAM_INDEX, &value) && (value & TIMEZONE_RAM_TAG_MASK) == TIMEZONE_RAM_TAG) {
        const std::uint8_t index = value & TIMEZONE_RAM_VALUE_MASK;
        if (index < TIMEZONES.size()) {
            return index;
        }
    }
    return 0;
}

static void saveTimezoneIndex(std::uint8_t index)
{
    GetHAL().rx8130RamWrite(TIMEZONE_RAM_INDEX, static_cast<std::uint8_t>(TIMEZONE_RAM_TAG | index));
}

static std::string formatUtcOffset(int offset_minutes)
{
    const char sign = offset_minutes >= 0 ? '+' : '-';
    int minutes     = std::abs(offset_minutes);
    const int hours = minutes / 60;
    minutes %= 60;

    char text[16] = {};
    if (minutes == 0) {
        std::snprintf(text, sizeof(text), "UTC%c%d", sign, hours);
    } else {
        std::snprintf(text, sizeof(text), "UTC%c%d:%02d", sign, hours, minutes);
    }
    return text;
}

static std::string formatTimezoneOption(const TimezoneOption& timezone)
{
    return std::string(timezone.text) + " " + formatUtcOffset(timezone.standard_offset_minutes);
}

static std::int64_t floorDiv(std::int64_t value, std::int64_t divisor)
{
    std::int64_t result          = value / divisor;
    const std::int64_t remainder = value % divisor;
    if (remainder != 0 && ((remainder < 0) != (divisor < 0))) {
        --result;
    }
    return result;
}

static std::int64_t floorMod(std::int64_t value, std::int64_t divisor)
{
    return value - floorDiv(value, divisor) * divisor;
}

static std::int64_t daysFromCivil(int year, int month, int day)
{
    year -= month <= 2;
    const int era              = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe         = static_cast<unsigned>(year - era * 400);
    const unsigned month_index = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
    const unsigned doy         = (153 * month_index + 2) / 5 + static_cast<unsigned>(day) - 1;
    const unsigned doe         = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return static_cast<std::int64_t>(era) * 146097 + static_cast<int>(doe) - 719468;
}

static ClockDate civilFromDays(std::int64_t days)
{
    days += 719468;
    const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe     = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe     = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int year               = static_cast<int>(yoe) + static_cast<int>(era) * 400;
    const unsigned doy     = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp      = (5 * doy + 2) / 153;
    const unsigned day     = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month   = mp + (mp < 10 ? 3 : -9);
    year += month <= 2;
    return {year, static_cast<int>(month), static_cast<int>(day)};
}

static int dayOfWeek(int year, int month, int day)
{
    return static_cast<int>(floorMod(daysFromCivil(year, month, day) + 4, 7));
}

static int daysInMonth(int year, int month)
{
    static constexpr int DAYS_PER_MONTH[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0))) {
        return 29;
    }
    return DAYS_PER_MONTH[month - 1];
}

static int nthWeekdayOfMonth(int year, int month, int weekday, int nth)
{
    const int first_weekday = dayOfWeek(year, month, 1);
    return 1 + floorMod(weekday - first_weekday, 7) + (nth - 1) * 7;
}

static int lastWeekdayOfMonth(int year, int month, int weekday)
{
    const int last_day     = daysInMonth(year, month);
    const int last_weekday = dayOfWeek(year, month, last_day);
    return last_day - floorMod(last_weekday - weekday, 7);
}

static ClockDateTime dateTimeFromTotalMinutes(std::int64_t total_minutes)
{
    const std::int64_t days = floorDiv(total_minutes, 24 * 60);
    const int minutes       = static_cast<int>(floorMod(total_minutes, 24 * 60));
    return {civilFromDays(days), minutes};
}

static bool isEuropeDst(std::int64_t utc_total_minutes)
{
    const auto utc  = dateTimeFromTotalMinutes(utc_total_minutes);
    const int month = utc.date.month;
    if (month < 3 || month > 10) {
        return false;
    }
    if (month > 3 && month < 10) {
        return true;
    }

    const int transition_day = lastWeekdayOfMonth(utc.date.year, month, 0);
    if (month == 3) {
        return utc.date.day > transition_day || (utc.date.day == transition_day && utc.minutes >= 60);
    }
    return utc.date.day < transition_day || (utc.date.day == transition_day && utc.minutes < 60);
}

static bool isUnitedStatesDst(std::int64_t utc_total_minutes, int standard_offset_minutes)
{
    const auto standard_local = dateTimeFromTotalMinutes(utc_total_minutes + standard_offset_minutes);
    const int month           = standard_local.date.month;
    if (month < 3 || month > 11) {
        return false;
    }
    if (month > 3 && month < 11) {
        return true;
    }

    if (month == 3) {
        const int start_day = nthWeekdayOfMonth(standard_local.date.year, 3, 0, 2);
        return standard_local.date.day > start_day ||
               (standard_local.date.day == start_day && standard_local.minutes >= 120);
    }

    const int end_day = nthWeekdayOfMonth(standard_local.date.year, 11, 0, 1);
    return standard_local.date.day < end_day || (standard_local.date.day == end_day && standard_local.minutes < 60);
}

static bool isDstActive(const TimezoneOption& timezone, std::int64_t utc_total_minutes)
{
    switch (timezone.dst_rule) {
        case DstRule::Europe:
            return isEuropeDst(utc_total_minutes);
        case DstRule::UnitedStates:
            return isUnitedStatesDst(utc_total_minutes, timezone.standard_offset_minutes);
        default:
            return false;
    }
}

static ClockDateTime convertRtcToTimezone(const m5::rtc_datetime_t& date_time, const TimezoneOption& timezone)
{
    const auto rtc_days   = daysFromCivil(static_cast<int>(date_time.date.year), static_cast<int>(date_time.date.month),
                                          static_cast<int>(date_time.date.date));
    const int rtc_minutes = static_cast<int>(date_time.time.hours) * 60 + static_cast<int>(date_time.time.minutes);
    const std::int64_t utc_total_minutes = rtc_days * 24 * 60 + rtc_minutes - TIMEZONES[0].standard_offset_minutes;
    int target_offset_minutes            = timezone.standard_offset_minutes;
    if (isDstActive(timezone, utc_total_minutes)) {
        target_offset_minutes += 60;
    }
    return dateTimeFromTotalMinutes(utc_total_minutes + target_offset_minutes);
}

static std::int64_t makeDisplayMinuteKey(const ClockDateTime& date_time)
{
    return (((static_cast<std::int64_t>(date_time.date.year) * 13 + date_time.date.month) * 32 + date_time.date.day) *
            24 * 60) +
           date_time.minutes;
}

/* -------------------------------------------------------------------------- */
/*                                UI Helpers                                  */
/* -------------------------------------------------------------------------- */

static bool pointInRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

static std::string fitNtpText(std::string text, int max_width)
{
    static constexpr const char* kEllipsis = "...";
    if (GetHAL().display.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    while (text.size() > 1) {
        text.erase(text.begin());
        const std::string clipped = kEllipsis + text;
        if (GetHAL().display.textWidth(clipped.c_str()) <= max_width) {
            return clipped;
        }
    }
    return kEllipsis;
}

static std::string passwordMask(std::size_t len)
{
    return std::string(len, '*');
}

static void drawNtpToggle(bool enabled, int x, int y, int w, int h)
{
    auto& display = GetHAL().display;
    display.fillRoundRect(x, y, w, h, h / 2, enabled ? TFT_BLACK : TFT_WHITE);
    display.drawRoundRect(x, y, w, h, h / 2, TFT_BLACK);
    const int knob_size = h - 8;
    const int knob_x    = enabled ? x + w - knob_size - 4 : x + 4;
    display.fillRoundRect(knob_x, y + 4, knob_size, knob_size, knob_size / 2, enabled ? TFT_WHITE : TFT_BLACK);
    display.setTextDatum(middle_center);
    display.setTextColor(enabled ? TFT_WHITE : TFT_BLACK, enabled ? TFT_BLACK : TFT_WHITE);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextSize(0.75f);
    display.drawString(enabled ? "ON" : "OFF", enabled ? x + 20 : x + w - 22, y + h / 2 + 1);
    display.setTextSize(1.0f);
}

static void drawNtpButton(const char* label, int x, int y, int w, int h, bool active = false)
{
    auto& display = GetHAL().display;
    if (active) {
        display.fillRoundRect(x, y, w, h, 5, TFT_BLACK);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        display.fillRoundRect(x, y, w, h, 5, TFT_WHITE);
        display.drawRoundRect(x, y, w, h, 5, TFT_BLACK);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextSize(1.0f);
    while (display.textWidth(label) > w - 12 && display.getTextSizeX() > 0.72f) {
        display.setTextSize(display.getTextSizeX() - 0.08f);
    }
    display.drawString(label, x + w / 2, y + h / 2 + 1);
    display.setTextSize(1.0f);
}

static void drawDateWithTimeFont(const char* text)
{
    float text_size = 0.5f;
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setFont(&fonts::Font7);
    GetHAL().display.setTextSize(text_size);

    while (text_size > 0.2f && (GetHAL().display.textWidth(text) > DATE_W || GetHAL().display.fontHeight() > DATE_H)) {
        text_size -= 0.05f;
        GetHAL().display.setTextSize(text_size);
    }

    GetHAL().display.drawString(text, DATE_X + DATE_W / 2, DATE_Y + DATE_H / 2);
    GetHAL().display.setTextSize(1.0f);
}

static void drawTimezone(const char* text)
{
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);
    GetHAL().display.setTextSize(1.0f);
    while (GetHAL().display.textWidth(text) > TIMEZONE_W || GetHAL().display.fontHeight() > TIMEZONE_H) {
        const float text_size = GetHAL().display.getTextSizeX() - 0.1f;
        if (text_size < 0.6f) {
            break;
        }
        GetHAL().display.setTextSize(text_size);
    }
    GetHAL().display.drawString(text, TIMEZONE_X + TIMEZONE_W / 2, TIMEZONE_Y + TIMEZONE_H / 2);
    GetHAL().display.setTextSize(1.0f);
}

/* -------------------------------------------------------------------------- */
/*                              Settings Cache                                */
/* -------------------------------------------------------------------------- */

void AppClock::loadNtpSettings()
{
    Preferences prefs;
    if (!prefs.begin("clock_ntp", false)) {
        return;
    }
    _ntp_ssid         = prefs.getString("ssid", "").c_str();
    _ntp_password     = prefs.getString("pass", "").c_str();
    _ntp_auto_enabled = prefs.getBool("auto", false);
    prefs.end();
}

void AppClock::saveNtpSettings()
{
    Preferences prefs;
    if (!prefs.begin("clock_ntp", false)) {
        return;
    }
    prefs.putString("ssid", _ntp_ssid.c_str());
    prefs.putString("pass", _ntp_password.c_str());
    prefs.putBool("auto", _ntp_auto_enabled);
    prefs.end();
}

void AppClock::scheduleNextAutoNtp(std::uint32_t delay_ms)
{
    _next_auto_ntp_ms = GetHAL().millis() + delay_ms;
}

/* -------------------------------------------------------------------------- */
/*                                 Lifecycle                                  */
/* -------------------------------------------------------------------------- */

void AppClock::onCreate()
{
    setAppInfo().name = "AppClock";
    loadNtpSettings();
    _timezone_index   = loadTimezoneIndex();
    _next_auto_ntp_ms = 0;
    open();
}

void AppClock::onOpen()
{
    drawTime();
}

void AppClock::onRunning()
{
    if (_ntp_dialog_open) {
        handleNtpDialog();
        return;
    }

    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const auto now_ms = GetHAL().millis();
    if (_ntp_connecting || _ntp_syncing) {
        pollNtpSync();
    } else if (!NTP_AUTO_SYNC_TEMP_DISABLED && _ntp_auto_enabled && !_ntp_ssid.empty() && _next_auto_ntp_ms != 0 &&
               static_cast<std::int32_t>(now_ms - _next_auto_ntp_ms) >= 0) {
        startNtpSync(true);
    }

    const bool timezone_touched = GetHAL().wasTouchClickedArea(TIMEZONE_X, TIMEZONE_Y, TIMEZONE_W, TIMEZONE_H);
    const bool ntp_touched      = GetHAL().wasTouchClickedArea(NTP_BUTTON_X, NTP_BUTTON_Y, NTP_BUTTON_W, NTP_BUTTON_H);

    if (ntp_touched && now_ms - _last_ntp_touch_ms >= NTP_TOUCH_COOLDOWN_MS) {
        _last_ntp_touch_ms = now_ms;
        openNtpDialog();
        return;
    }

    if (timezone_touched && now_ms - _last_timezone_touch_ms >= TIMEZONE_TOUCH_COOLDOWN_MS) {
        switchTimezone();
        GetHAL().playBuzzerPattern(TIMEZONE_SWITCH_TONES, 3);
        _last_timezone_touch_ms = now_ms;
        GetHAL().requestRefresh();
        drawTime();
        return;
    }

    bool minute_changed = false;
    if (now_ms - _last_minute_check_ms >= CLOCK_MINUTE_CHECK_MS) {
        _last_minute_check_ms         = now_ms;
        const auto date_time          = GetHAL().rtc.getDateTime();
        const auto timezone_date_time = convertRtcToTimezone(date_time, TIMEZONES[_timezone_index]);
        minute_changed                = makeDisplayMinuteKey(timezone_date_time) != _last_display_minute_key;
    }
    if (!_has_drawn || GetHAL().isFullRedrawRequested() || minute_changed ||
        now_ms - _last_update_ms >= CLOCK_REFRESH_MS) {
        drawTime();
    }
}

/* -------------------------------------------------------------------------- */
/*                                NTP Dialog                                  */
/* -------------------------------------------------------------------------- */

void AppClock::openNtpDialog()
{
    GetHAL().captureDisplaySnapshot();
    _ntp_dialog_open      = true;
    _ntp_scan_running     = false;
    _ntp_success_close_ms = 0;
    _ntp_touching         = false;
    _ntp_list_touching    = false;
    _ntp_list_dragged     = false;
    if (!_ntp_connecting && !_ntp_syncing) {
        _ntp_status = _ntp_ssid.empty() ? "SELECT OR TYPE SSID" : "Wi-Fi CONFIG READY";
    }
    WiFi.scanDelete();
    GetHAL().setUiModalActive(true);
    GetHAL().playBuzzerPattern(NTP_DIALOG_OPEN_TONES, 2);
    drawNtpDialog(true);
}

void AppClock::closeNtpDialog()
{
    WiFi.scanDelete();
    _keyboard.close();
    _ntp_dialog_open      = false;
    _ntp_scan_running     = false;
    _ntp_connecting       = false;
    _ntp_syncing          = false;
    _ntp_auto_syncing     = false;
    _ntp_success_close_ms = 0;
    _ntp_touching         = false;
    _ntp_list_touching    = false;
    _ntp_list_dragged     = false;
    if (!GetHAL().restoreDisplaySnapshot()) {
        GetHAL().drawBackground(m5gfx::epd_mode_t::epd_fast, false);
    }
    GetHAL().clearDisplaySnapshot();
    _has_drawn = false;
    GetHAL().releaseModalNextFrame(m5gfx::epd_mode_t::epd_fast);
}

void AppClock::drawNtpDialog(bool full_refresh)
{
    auto& display = GetHAL().display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    display.fillRoundRect(NTP_DIALOG_X, NTP_DIALOG_Y, NTP_DIALOG_W, NTP_DIALOG_H, 6, TFT_WHITE);
    display.drawRoundRect(NTP_DIALOG_X, NTP_DIALOG_Y, NTP_DIALOG_W, NTP_DIALOG_H, 6, TFT_BLACK);
    display.fillRect(NTP_DIALOG_X, NTP_DIALOG_Y, NTP_DIALOG_W, 48, TFT_BLACK);

    display.setTextDatum(middle_left);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextSize(1.0f);
    display.setFont(&fonts::FreeSansBold12pt7b);
    display.drawString("NTP & Wi-Fi CONFIG", NTP_DIALOG_X + 18, NTP_DIALOG_Y + 24);
    drawNtpButton("x", NTP_CLOSE_X, NTP_CLOSE_Y, NTP_CLOSE_W, NTP_CLOSE_H, true);

    drawNtpInputFields();

    display.setTextDatum(middle_left);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.drawString("TIME ZONE", NTP_FIELD_X, NTP_TIMEZONE_Y + NTP_AUTO_H / 2);
    drawNtpButton(formatTimezoneOption(TIMEZONES[_timezone_index]).c_str(), NTP_TIMEZONE_VALUE_X, NTP_TIMEZONE_Y,
                  NTP_TIMEZONE_VALUE_W, NTP_AUTO_H);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_left);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextSize(1.0f);
    display.drawString("AUTO TIME SYNC", NTP_FIELD_X, NTP_AUTO_Y + NTP_AUTO_H / 2);
    drawNtpToggle(_ntp_auto_enabled, NTP_AUTO_TOGGLE_X, NTP_AUTO_Y, NTP_AUTO_TOGGLE_W, NTP_AUTO_H);

    drawNtpButton(_ntp_scan_running ? "SCANNING..." : "SCAN Wi-Fi", NTP_SCAN_X, NTP_BUTTON_ROW_Y, NTP_ACTION_W,
                  NTP_ACTION_H, _ntp_scan_running);
    drawNtpButton(_ntp_connecting || _ntp_syncing ? "SYNCING TIME" : "SYNC TIME", NTP_SYNC_X, NTP_BUTTON_ROW_Y,
                  NTP_ACTION_W, NTP_ACTION_H, _ntp_connecting || _ntp_syncing);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.drawString(_ntp_status.c_str(), NTP_DIALOG_X + NTP_DIALOG_W / 2, NTP_STATUS_Y);

    display.drawRoundRect(NTP_LIST_X, NTP_LIST_Y, NTP_LIST_W, NTP_LIST_ROWS * NTP_LIST_ROW_H + 8, 5, TFT_BLACK);
    display.setTextDatum(middle_left);
    if (_ntp_networks.empty()) {
        display.drawString(_ntp_scan_running ? "Scanning networks..." : "No scan results", NTP_LIST_X + 10,
                           NTP_LIST_Y + 24);
    } else {
        const int max_scroll = std::max(0, static_cast<int>(_ntp_networks.size()) - NTP_LIST_ROWS);
        _ntp_list_scroll     = static_cast<std::int16_t>(std::clamp(static_cast<int>(_ntp_list_scroll), 0, max_scroll));
        for (int i = 0; i < std::min<int>(NTP_LIST_ROWS, _ntp_networks.size()); ++i) {
            const int index = _ntp_list_scroll + i;
            if (index >= static_cast<int>(_ntp_networks.size())) {
                break;
            }
            const int row_y     = NTP_LIST_Y + 4 + i * NTP_LIST_ROW_H;
            const auto& network = _ntp_networks[index];
            const int rssi_right =
                NTP_LIST_X + NTP_LIST_W - (_ntp_networks.size() > NTP_LIST_ROWS ? NTP_SCROLL_W + 10 : 10);
            display.drawString(
                fitNtpText(network.ssid.empty() ? "<hidden>" : network.ssid, rssi_right - NTP_LIST_X - 112).c_str(),
                NTP_LIST_X + 10, row_y + NTP_LIST_ROW_H / 2);
            char rssi_text[16] = {};
            std::snprintf(rssi_text, sizeof(rssi_text), "%lddBm", static_cast<long>(network.rssi));
            display.setTextDatum(middle_right);
            display.drawString(rssi_text, rssi_right, row_y + NTP_LIST_ROW_H / 2);
            display.setTextDatum(middle_left);
        }
        if (_ntp_networks.size() > NTP_LIST_ROWS) {
            const int track_x = NTP_LIST_X + NTP_LIST_W - NTP_SCROLL_W - 2;
            const int track_y = NTP_LIST_Y + 4;
            const int track_h = NTP_LIST_ROWS * NTP_LIST_ROW_H;
            const int thumb_h = std::max(16, track_h * NTP_LIST_ROWS / static_cast<int>(_ntp_networks.size()));
            const int thumb_y = track_y + (track_h - thumb_h) * _ntp_list_scroll / max_scroll;
            display.drawFastVLine(track_x, track_y, track_h, TFT_BLACK);
            display.fillRect(track_x, thumb_y, NTP_SCROLL_W, thumb_h, TFT_BLACK);
        }
    }

    display.setEpdMode(full_refresh ? m5gfx::epd_mode_t::epd_fast : m5gfx::epd_mode_t::epd_fastest);
    GetHAL().flushDisplay(NTP_DIALOG_X, NTP_DIALOG_Y, NTP_DIALOG_W, NTP_DIALOG_H);
    display.waitDisplay();
    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
}

void AppClock::drawNtpInputFields()
{
    auto& display = GetHAL().display;
    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    display.fillRect(NTP_FIELD_X - 2, NTP_SSID_Y - 22, NTP_FIELD_W + 4, NTP_FIELD_H + 24, TFT_WHITE);
    display.fillRect(NTP_FIELD_X - 2, NTP_PASS_Y - 22, NTP_FIELD_W + 4, NTP_FIELD_H + 24, TFT_WHITE);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextDatum(bottom_left);
    display.setTextSize(0.85f);
    display.drawString("SSID", NTP_FIELD_X, NTP_SSID_Y - 2);
    display.drawString("PASSWORD", NTP_FIELD_X, NTP_PASS_Y - 2);
    display.setTextSize(1.0f);

    display.drawRoundRect(NTP_FIELD_X, NTP_SSID_Y, NTP_FIELD_W, NTP_FIELD_H, 5, TFT_BLACK);
    display.drawRoundRect(NTP_FIELD_X, NTP_PASS_Y, NTP_FIELD_W, NTP_FIELD_H, 5, TFT_BLACK);
    display.setTextDatum(middle_left);
    const std::string ssid_text = _ntp_ssid;
    const std::string pass_text = _ntp_password.empty() ? "" : passwordMask(_ntp_password.size());
    display.drawString(fitNtpText(ssid_text, NTP_FIELD_W - 18).c_str(), NTP_FIELD_X + 9, NTP_SSID_Y + NTP_FIELD_H / 2);
    display.drawString(fitNtpText(pass_text, NTP_FIELD_W - 18).c_str(), NTP_FIELD_X + 9, NTP_PASS_Y + NTP_FIELD_H / 2);
}

void AppClock::handleNtpDialog()
{
    const auto now_ms = GetHAL().millis();
    if (_ntp_success_close_ms != 0 && static_cast<std::int32_t>(now_ms - _ntp_success_close_ms) >= 0) {
        closeNtpDialog();
        return;
    }

    if (_keyboard.isOpen()) {
        auto closeKeyboardAndRedraw = [&]() {
            commitNtpKeyboard();
            if (!GetHAL().restoreDisplaySnapshot()) {
                GetHAL().drawBackground(m5gfx::epd_mode_t::epd_fast, false);
            }
            GetHAL().requestRefresh();
            drawTime();
            drawNtpDialog(true);
        };
        auto switchKeyboardField = [&](std::uint8_t field) {
            if (_ntp_edit_field == field) {
                return;
            }
            commitNtpKeyboard();
            drawNtpInputFields();
            GetHAL().flushDisplay(NTP_FIELD_X - 2, NTP_SSID_Y - 22, NTP_FIELD_W + 4,
                                  NTP_PASS_Y - NTP_SSID_Y + NTP_FIELD_H + 24);
            GetHAL().display.waitDisplay();
            openNtpKeyboard(field);
        };

        const KeyboardEvent event = _keyboard.update(false);
        if (event == KeyboardEvent::Closed) {
            closeKeyboardAndRedraw();
        } else if (event == KeyboardEvent::Changed) {
            if (_ntp_edit_field == 0) {
                _ntp_ssid = _keyboard.text();
            } else {
                _ntp_password = _keyboard.text();
            }
            drawNtpInputFields();
            const int refresh_y  = NTP_SSID_Y - 22;
            const int combined_y = std::min(refresh_y, _keyboard.refreshTop());
            GetHAL().flushDisplay(0, combined_y, GetHAL().display.width(), GetHAL().display.height() - combined_y);
            GetHAL().display.waitDisplay();
        } else if (GetHAL().isTouchPressed()) {
            const auto& touch = GetHAL().getTouchDetail();
            const auto& point = M5.Touch.getTouchPointRaw();
            if (touch.wasPressed() && !_keyboard.containsPoint(point.x, point.y)) {
                if (pointInRect(point.x, point.y, NTP_FIELD_X, NTP_SSID_Y, NTP_FIELD_W, NTP_FIELD_H)) {
                    switchKeyboardField(0);
                } else if (pointInRect(point.x, point.y, NTP_FIELD_X, NTP_PASS_Y, NTP_FIELD_W, NTP_FIELD_H)) {
                    switchKeyboardField(1);
                } else {
                    _keyboard.close();
                    closeKeyboardAndRedraw();
                }
            }
        }
        return;
    }

    if (_ntp_scan_running) {
        const int scan_count = WiFi.scanComplete();
        if (scan_count >= 0) {
            finishNtpScan(scan_count);
            return;
        }
        if (scan_count == WIFI_SCAN_FAILED) {
            _ntp_scan_running = false;
            _ntp_status       = "NETWORK SCAN FAILED";
            GetHAL().playBuzzerPattern(NTP_FAIL_TONES, 2);
            drawNtpDialog();
            return;
        }
    }

    const auto& touch    = GetHAL().getTouchDetail();
    const auto& point    = M5.Touch.getTouchPointRaw();
    const int list_h     = NTP_LIST_ROWS * NTP_LIST_ROW_H + 8;
    const int max_scroll = std::max(0, static_cast<int>(_ntp_networks.size()) - NTP_LIST_ROWS);

    if (touch.wasPressed()) {
        _ntp_success_close_ms = 0;
        _ntp_touching         = true;
        _ntp_touch_start_x    = static_cast<std::int16_t>(point.x);
        _ntp_touch_start_y    = static_cast<std::int16_t>(point.y);
        _ntp_last_touch_y     = static_cast<std::int16_t>(point.y);
        _ntp_list_touching    = false;
        _ntp_list_dragged     = false;
        return;
    }

    if (_ntp_connecting || _ntp_syncing) {
        pollNtpSync();
    }

    if (!touch.wasReleased() || !_ntp_touching) {
        return;
    }
    _ntp_touching = false;

    const int dx               = point.x - _ntp_touch_start_x;
    const int dy               = point.y - _ntp_touch_start_y;
    const bool started_in_list = _ntp_touch_start_x >= NTP_LIST_X && _ntp_touch_start_x <= NTP_LIST_X + NTP_LIST_W &&
                                 _ntp_touch_start_y >= NTP_LIST_Y && _ntp_touch_start_y < NTP_LIST_Y + list_h;
    const bool is_vertical_swipe = started_in_list && std::abs(dy) >= NTP_LIST_ROW_H && std::abs(dy) > std::abs(dx);
    if (max_scroll > 0 && is_vertical_swipe) {
        const int old_scroll = _ntp_list_scroll;
        if (dy < 0) {
            _ntp_list_scroll =
                static_cast<std::int16_t>(std::min(max_scroll, static_cast<int>(_ntp_list_scroll) + NTP_LIST_ROWS));
        } else {
            _ntp_list_scroll =
                static_cast<std::int16_t>(std::max(0, static_cast<int>(_ntp_list_scroll) - NTP_LIST_ROWS));
        }
        if (_ntp_list_scroll != old_scroll) {
            drawNtpDialog();
        }
        return;
    }

    if (started_in_list && std::abs(dx) < NTP_LIST_ROW_H && std::abs(dy) < NTP_LIST_ROW_H && !_ntp_networks.empty()) {
        const int row   = (_ntp_touch_start_y - NTP_LIST_Y - 4) / NTP_LIST_ROW_H;
        const int index = _ntp_list_scroll + row;
        if (row >= 0 && row < NTP_LIST_ROWS && index >= 0 && index < static_cast<int>(_ntp_networks.size())) {
            _ntp_ssid = _ntp_networks[index].ssid;
            saveNtpSettings();
            _ntp_status = "SSID SELECTED";
            drawNtpDialog();
            openNtpKeyboard(1);
            return;
        }
    }

    if (pointInRect(point.x, point.y, NTP_CLOSE_X - NTP_CLOSE_TOUCH_PAD, NTP_CLOSE_Y - NTP_CLOSE_TOUCH_PAD,
                    NTP_CLOSE_W + NTP_CLOSE_TOUCH_PAD * 2, NTP_CLOSE_H + NTP_CLOSE_TOUCH_PAD * 2)) {
        GetHAL().playBuzzerPattern(NTP_CLOSE_TONES, 2);
        closeNtpDialog();
        return;
    }
    if (pointInRect(point.x, point.y, NTP_FIELD_X, NTP_SSID_Y, NTP_FIELD_W, NTP_FIELD_H)) {
        openNtpKeyboard(0);
        return;
    }
    if (pointInRect(point.x, point.y, NTP_FIELD_X, NTP_PASS_Y, NTP_FIELD_W, NTP_FIELD_H)) {
        openNtpKeyboard(1);
        return;
    }
    if (pointInRect(point.x, point.y, NTP_FIELD_X, NTP_TIMEZONE_Y, NTP_FIELD_W, NTP_AUTO_H)) {
        const int current_offset = TIMEZONES[_timezone_index].standard_offset_minutes;
        do {
            _timezone_index = static_cast<std::uint8_t>((_timezone_index + 1) % TIMEZONES.size());
        } while (TIMEZONES[_timezone_index].standard_offset_minutes == current_offset);
        saveTimezoneIndex(_timezone_index);
        _ntp_status = formatTimezoneOption(TIMEZONES[_timezone_index]);
        GetHAL().playBuzzerPattern(TIMEZONE_SWITCH_TONES, 3);
        drawNtpDialog();
        return;
    }
    if (pointInRect(point.x, point.y, NTP_FIELD_X, NTP_AUTO_Y, NTP_FIELD_W, NTP_AUTO_H)) {
        _ntp_auto_enabled = !_ntp_auto_enabled;
        saveNtpSettings();
        if (_ntp_auto_enabled && !_ntp_ssid.empty()) {
            scheduleNextAutoNtp(NTP_AUTO_BOOT_DELAY_MS);
            _ntp_status = "AUTO TIME SYNC ON";
        } else {
            _next_auto_ntp_ms = 0;
            _ntp_status       = _ntp_auto_enabled ? "SET Wi-Fi FIRST" : "AUTO TIME SYNC OFF";
        }
        GetHAL().playBuzzerPattern(NTP_SELECT_TONES, 1);
        drawNtpDialog();
        return;
    }
    if (pointInRect(point.x, point.y, NTP_SCAN_X, NTP_BUTTON_ROW_Y, NTP_ACTION_W, NTP_ACTION_H)) {
        startNtpScan();
        return;
    }
    if (pointInRect(point.x, point.y, NTP_SYNC_X, NTP_BUTTON_ROW_Y, NTP_ACTION_W, NTP_ACTION_H)) {
        startNtpSync();
        return;
    }
}

/* -------------------------------------------------------------------------- */
/*                                NTP Network                                 */
/* -------------------------------------------------------------------------- */

void AppClock::startNtpScan()
{
    if (_ntp_scan_running) {
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.scanDelete();
    _ntp_networks.clear();
    _ntp_touching      = false;
    _ntp_list_scroll   = 0;
    _ntp_list_touching = false;
    _ntp_list_dragged  = false;
    _ntp_scan_running  = WiFi.scanNetworks(true, true) == WIFI_SCAN_RUNNING;
    if (_ntp_scan_running) {
        _ntp_status = "SCANNING NETWORKS...";
        GetHAL().playBuzzerPattern(NTP_SYNC_START_TONES, 3);
    } else {
        _ntp_status = "NETWORK SCAN FAILED";
        GetHAL().playBuzzerPattern(NTP_FAIL_TONES, 2);
    }
    drawNtpDialog();
}

void AppClock::finishNtpScan(int count)
{
    _ntp_networks.clear();
    _ntp_networks.reserve(std::max(count, 0));
    for (int i = 0; i < count; ++i) {
        const std::string ssid = WiFi.SSID(i).c_str();
        if (ssid.empty()) {
            continue;
        }
        const bool duplicate = std::any_of(_ntp_networks.begin(), _ntp_networks.end(),
                                           [&](const WifiChoice& item) { return item.ssid == ssid; });
        if (!duplicate) {
            _ntp_networks.push_back({ssid, WiFi.RSSI(i)});
        }
    }
    std::sort(_ntp_networks.begin(), _ntp_networks.end(),
              [](const WifiChoice& a, const WifiChoice& b) { return a.rssi > b.rssi; });
    _ntp_list_scroll   = 0;
    _ntp_touching      = false;
    _ntp_list_touching = false;
    _ntp_list_dragged  = false;
    WiFi.scanDelete();
    _ntp_scan_running = false;
    _ntp_status       = _ntp_networks.empty() ? "NO NETWORKS" : "SELECT SSID";
    GetHAL().playBuzzerPattern(_ntp_networks.empty() ? NTP_FAIL_TONES : NTP_SYNC_DONE_TONES,
                               _ntp_networks.empty() ? 2 : 3);
    drawNtpDialog();
}

void AppClock::startNtpSync(bool automatic)
{
    if (_ntp_ssid.empty()) {
        _ntp_status = "SSID REQUIRED";
        GetHAL().playBuzzerPattern(NTP_FAIL_TONES, 2);
        if (_ntp_dialog_open) {
            drawNtpDialog();
        }
        return;
    }

    saveNtpSettings();
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(false, false);
    WiFi.begin(_ntp_ssid.c_str(), _ntp_password.empty() ? nullptr : _ntp_password.c_str());
    _ntp_connect_start_ms = GetHAL().millis();
    _ntp_connecting       = true;
    _ntp_syncing          = false;
    _ntp_auto_syncing     = automatic;
    _ntp_success_close_ms = 0;
    _ntp_status           = automatic ? "AUTO CONNECTING..." : "CONNECTING TO Wi-Fi...";
    if (!automatic || _ntp_dialog_open) {
        GetHAL().playBuzzerPattern(NTP_SYNC_START_TONES, 3);
    }
    if (_ntp_dialog_open) {
        drawNtpDialog();
    }
}

void AppClock::pollNtpSync()
{
    const auto now_ms = GetHAL().millis();
    if (_ntp_connecting) {
        if (WiFi.status() == WL_CONNECTED) {
            _ntp_connecting       = false;
            _ntp_syncing          = true;
            _ntp_connect_start_ms = now_ms;
            _ntp_status           = _ntp_auto_syncing ? "AUTO TIME SYNCING..." : "SYNCING TIME...";
            sntp_set_sync_status(SNTP_SYNC_STATUS_RESET);
            configTime(0, 0, "pool.ntp.org", "time.nist.gov", "time.google.com");
            if (_ntp_dialog_open) {
                drawNtpDialog();
            }
            return;
        }
        if (now_ms - _ntp_connect_start_ms > NTP_CONNECT_TIMEOUT_MS) {
            const bool was_auto = _ntp_auto_syncing;
            _ntp_connecting     = false;
            _ntp_status         = _ntp_auto_syncing ? "AUTO CONNECT FAIL" : "CONNECT FAIL";
            if (_ntp_auto_enabled) {
                scheduleNextAutoNtp(NTP_AUTO_INTERVAL_MS);
            }
            _ntp_auto_syncing = false;
            if (_ntp_dialog_open || !was_auto) {
                GetHAL().playBuzzerPattern(NTP_FAIL_TONES, 2);
            }
            if (_ntp_dialog_open) {
                drawNtpDialog();
            }
        }
        return;
    }

    if (!_ntp_syncing) {
        return;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
        timeval tv = {};
        gettimeofday(&tv, nullptr);
        time_t utc_now = tv.tv_sec;
        if (tv.tv_usec >= 500000) {
            ++utc_now;
        }
        utc_now += NTP_RTC_WRITE_COMPENSATION_SEC;
        if (utc_now > NTP_VALID_UNIX_TIME) {
            const time_t rtc_time = utc_now + RTC_BASE_OFFSET_SEC;
            tm rtc_tm             = {};
            gmtime_r(&rtc_time, &rtc_tm);
            GetHAL().rtc.setDateTime(&rtc_tm);
            const bool was_auto = _ntp_auto_syncing;
            _ntp_syncing        = false;
            _ntp_status         = "TIME UPDATED";
            if (_ntp_auto_enabled) {
                scheduleNextAutoNtp(NTP_AUTO_INTERVAL_MS);
            }
            _ntp_auto_syncing = false;
            if (_ntp_dialog_open || !was_auto) {
                GetHAL().playBuzzerPattern(NTP_SYNC_DONE_TONES, 3);
            }
            if (_ntp_dialog_open) {
                _ntp_success_close_ms = now_ms + NTP_SUCCESS_CLOSE_MS;
                drawNtpDialog();
            }
            _last_update_ms = 0;
            _has_drawn      = false;
            return;
        }
    }

    if (now_ms - _ntp_connect_start_ms > NTP_SYNC_TIMEOUT_MS) {
        const bool was_auto = _ntp_auto_syncing;
        _ntp_syncing        = false;
        _ntp_status         = _ntp_auto_syncing ? "AUTO NTP TIMEOUT" : "NTP TIMEOUT";
        if (_ntp_auto_enabled) {
            scheduleNextAutoNtp(NTP_AUTO_INTERVAL_MS);
        }
        _ntp_auto_syncing = false;
        if (_ntp_dialog_open || !was_auto) {
            GetHAL().playBuzzerPattern(NTP_FAIL_TONES, 2);
        }
        if (_ntp_dialog_open) {
            drawNtpDialog();
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                                NTP Input                                   */
/* -------------------------------------------------------------------------- */

void AppClock::openNtpKeyboard(std::uint8_t field)
{
    _ntp_edit_field = field;
    GetHAL().playBuzzerPattern(NTP_SELECT_TONES, 1);
    if (field == 0) {
        _keyboard.open("SSID", _ntp_ssid, false, 32);
    } else {
        _keyboard.open("PASSWORD", _ntp_password, true, 64);
    }
}

void AppClock::commitNtpKeyboard()
{
    if (_ntp_edit_field == 0) {
        _ntp_ssid   = _keyboard.text();
        _ntp_status = _ntp_ssid.empty() ? "SSID REQUIRED" : "SSID READY";
        if (_ntp_auto_enabled && !_ntp_ssid.empty()) {
            scheduleNextAutoNtp(NTP_AUTO_BOOT_DELAY_MS);
        }
    } else {
        _ntp_password = _keyboard.text();
        _ntp_status   = "PASSWORD READY";
    }
    saveNtpSettings();
}

/* -------------------------------------------------------------------------- */
/*                                Time Display                                */
/* -------------------------------------------------------------------------- */

void AppClock::switchTimezone()
{
    _timezone_index = static_cast<std::uint8_t>((_timezone_index + 1) % TIMEZONES.size());
    saveTimezoneIndex(_timezone_index);
}

void AppClock::drawTime()
{
    const auto date_time          = GetHAL().rtc.getDateTime();
    const auto& timezone          = TIMEZONES[_timezone_index];
    const auto timezone_date_time = convertRtcToTimezone(date_time, timezone);

    char time_text[16] = {};
    char date_text[24] = {};
    snprintf(time_text, sizeof(time_text), "%02d:%02d", timezone_date_time.minutes / 60,
             timezone_date_time.minutes % 60);
    snprintf(date_text, sizeof(date_text), "%04d.%02d.%02d", timezone_date_time.date.year,
             timezone_date_time.date.month, timezone_date_time.date.day);

    const int refresh_x = TIME_X;
    const int refresh_y = TIME_Y;
    const int refresh_w = TIMEZONE_X + TIMEZONE_W - TIME_X;
    const int refresh_h = DATE_Y + DATE_H - TIME_Y;

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(TIME_X, TIME_Y, TIME_W, TIME_H, TFT_WHITE);
    GetHAL().display.fillRect(TIMEZONE_X, TIMEZONE_Y, TIMEZONE_W, TIMEZONE_H, TFT_WHITE);
    GetHAL().display.fillRect(DATE_X, DATE_Y, DATE_W, DATE_H, TFT_WHITE);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setFont(&fonts::Font7);
    GetHAL().display.drawString(time_text, TIME_X + TIME_W / 2, TIME_Y + TIME_H / 2);
    drawTimezone(timezone.text);
    drawDateWithTimeFont(date_text);
    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(refresh_x, refresh_y, refresh_w, refresh_h);
        GetHAL().display.waitDisplay();
    }

    _last_update_ms          = GetHAL().millis();
    _last_display_minute_key = makeDisplayMinuteKey(timezone_date_time);
    _has_drawn               = true;
}

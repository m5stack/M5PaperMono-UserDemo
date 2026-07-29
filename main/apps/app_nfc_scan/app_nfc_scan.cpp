/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <M5UnitUnified.hpp>
#include <M5UnitUnifiedNFC.h>
#include <algorithm>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <hal.h>
#include <string>
#include <utility>
#include <vector>

static constexpr std::uint32_t NFC_TOUCH_COOLDOWN_MS    = 300;
static constexpr std::uint32_t NFC_SCAN_TASK_STACK      = 16 * 1024;
static constexpr UBaseType_t NFC_SCAN_TASK_PRIORITY     = 4;
static constexpr std::uint32_t NFC_A_SCAN_WINDOW_MS     = 3500;
static constexpr std::uint32_t NFC_OTHER_SCAN_WINDOW_MS = 900;
static constexpr std::uint32_t NFC_DETECT_TIMEOUT_MS    = 180;
static constexpr std::uint32_t NFC_DETECT_GAP_MS        = 60;
static constexpr std::uint32_t NFC_MODE_STABLE_MS       = 200;

static constexpr int NFC_SCAN_X = 371;
static constexpr int NFC_SCAN_Y = 360;
static constexpr int NFC_SCAN_W = 476 - 371;
static constexpr int NFC_SCAN_H = 409 - 360;

static constexpr int NFC_INFO_X              = 122;
static constexpr int NFC_INFO_Y              = 361;
static constexpr int NFC_INFO_W              = 352 - 122;
static constexpr int NFC_INFO_H              = 410 - 361;
static constexpr int NFC_INFO_LINES_PER_PAGE = 2;
static constexpr int NFC_INFO_LINE_GAP       = 22;
static constexpr int NFC_INFO_TEXT_Y_OFFSET  = 1;

static constexpr BuzzerTone NFC_SCAN_START_TONES[] = {
    {.start_freq_hz = 720, .end_freq_hz = 720, .duration_ms = 12, .gap_ms = 8},
    {.start_freq_hz = 1080, .end_freq_hz = 1080, .duration_ms = 14, .gap_ms = 0},
};

static constexpr BuzzerTone NFC_CARD_FOUND_TONES[] = {
    {.start_freq_hz = 980, .end_freq_hz = 980, .duration_ms = 12, .gap_ms = 5},
    {.start_freq_hz = 1470, .end_freq_hz = 1470, .duration_ms = 24, .gap_ms = 0},
};

static constexpr BuzzerTone NFC_NO_CARD_TONES[] = {
    {.start_freq_hz = 760, .end_freq_hz = 760, .duration_ms = 12, .gap_ms = 5},
    {.start_freq_hz = 480, .end_freq_hz = 480, .duration_ms = 22, .gap_ms = 0},
};

namespace {

m5::unit::UnitUnified s_nfc_hub;
m5::unit::UnitNFC s_nfc_unit;
m5::nfc::NFCLayerA s_nfc_layer_a{s_nfc_unit};
m5::nfc::NFCLayerB s_nfc_layer_b{s_nfc_unit};
m5::nfc::NFCLayerF s_nfc_layer_f{s_nfc_unit};
m5::nfc::NFCLayerV s_nfc_layer_v{s_nfc_unit};
bool s_nfc_initialized               = false;
m5::nfc::NFC s_nfc_mode              = m5::nfc::NFC::None;
SemaphoreHandle_t s_nfc_result_mutex = nullptr;
TaskHandle_t s_nfc_task_handle       = nullptr;
bool s_nfc_task_running              = false;
bool s_nfc_task_done                 = false;
std::vector<AppNfcScan::NfcInfo> s_nfc_task_items;
std::string s_nfc_task_status;

bool ensureNfcHardware();
void powerDownNfcHardware();
bool scanA(std::vector<AppNfcScan::NfcInfo>& items);
bool scanB(std::vector<AppNfcScan::NfcInfo>& items);
bool scanF(std::vector<AppNfcScan::NfcInfo>& items);
bool scanV(std::vector<AppNfcScan::NfcInfo>& items);
bool scanModeWindow(std::vector<AppNfcScan::NfcInfo>& items, m5::nfc::NFC mode, std::uint32_t window_ms);
bool scanAllModes(std::vector<AppNfcScan::NfcInfo>& items);

bool ensureNfcResultMutex()
{
    if (s_nfc_result_mutex) {
        return true;
    }
    s_nfc_result_mutex = xSemaphoreCreateMutex();
    return s_nfc_result_mutex != nullptr;
}

void publishNfcScanResult(std::vector<AppNfcScan::NfcInfo>&& items, std::string status)
{
    if (!ensureNfcResultMutex()) {
        return;
    }

    xSemaphoreTake(s_nfc_result_mutex, portMAX_DELAY);
    s_nfc_task_items   = std::move(items);
    s_nfc_task_status  = std::move(status);
    s_nfc_task_done    = true;
    s_nfc_task_running = false;
    s_nfc_task_handle  = nullptr;
    xSemaphoreGive(s_nfc_result_mutex);
}

bool takeNfcScanResult(std::vector<AppNfcScan::NfcInfo>& items, std::string& status)
{
    if (!ensureNfcResultMutex()) {
        return false;
    }

    xSemaphoreTake(s_nfc_result_mutex, portMAX_DELAY);
    const bool done = s_nfc_task_done;
    if (done) {
        items  = std::move(s_nfc_task_items);
        status = std::move(s_nfc_task_status);
        s_nfc_task_items.clear();
        s_nfc_task_status.clear();
        s_nfc_task_done = false;
    }
    xSemaphoreGive(s_nfc_result_mutex);
    return done;
}

void nfcScanTask(void*)
{
    std::vector<AppNfcScan::NfcInfo> found_items;
    std::string status;

    if (!scanAllModes(found_items)) {
        status = "NO CARD";
    }

    powerDownNfcHardware();
    publishNfcScanResult(std::move(found_items), std::move(status));
    vTaskDelete(nullptr);
}

const char* modeName(m5::nfc::NFC mode)
{
    switch (mode) {
        case m5::nfc::NFC::A:
            return "NFC-A";
        case m5::nfc::NFC::B:
            return "NFC-B";
        case m5::nfc::NFC::F:
            return "NFC-F";
        case m5::nfc::NFC::V:
            return "NFC-V";
        default:
            return "NFC";
    }
}

std::string fitText(std::string text, int max_width)
{
    if (GetHAL().display.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    while (text.size() > 1) {
        text.pop_back();
        const std::string clipped = text + ".";
        if (GetHAL().display.textWidth(clipped.c_str()) <= max_width) {
            return clipped;
        }
    }
    return ".";
}

void addItem(std::vector<AppNfcScan::NfcInfo>& items, const char* label, const std::string& value)
{
    if (!value.empty()) {
        items.push_back({label, value});
    }
}

void addSizeItem(std::vector<AppNfcScan::NfcInfo>& items, std::uint16_t user_size, std::uint16_t total_size)
{
    if (user_size == 0 && total_size == 0) {
        return;
    }

    char text[32] = {};
    if (user_size > 0 && total_size > 0) {
        std::snprintf(text, sizeof(text), "%u/%uB", user_size, total_size);
    } else {
        std::snprintf(text, sizeof(text), "%uB", std::max(user_size, total_size));
    }
    items.push_back({"SIZE", text});
}

std::string nfcInfoText(const AppNfcScan::NfcInfo& item)
{
    return item.label.empty() ? item.value : item.label + " " + item.value;
}

bool ensureNfcHardware()
{
    if (!GetHAL().setNfcPower(true)) {
        return false;
    }
    GetHAL().delay(120);

    if (s_nfc_initialized) {
        s_nfc_unit.begin();
        if (s_nfc_mode != m5::nfc::NFC::None) {
            s_nfc_unit.configureNFCMode(s_nfc_mode);
        }
        s_nfc_hub.update();
        return true;
    }

    auto cfg      = s_nfc_unit.config();
    cfg.emulation = false;
    cfg.mode      = m5::nfc::NFC::A;
    cfg.using_irq = false;
    s_nfc_unit.config(cfg);

    if (!s_nfc_hub.add(s_nfc_unit, M5.In_I2C) || !s_nfc_hub.begin()) {
        return false;
    }

    s_nfc_initialized = true;
    s_nfc_mode        = m5::nfc::NFC::A;
    return true;
}

void powerDownNfcHardware()
{
    if (s_nfc_initialized) {
        s_nfc_unit.disableField();
    }
    GetHAL().setNfcPower(false);
}

bool switchNfcMode(m5::nfc::NFC mode)
{
    if (!ensureNfcHardware()) {
        return false;
    }
    if (s_nfc_mode == mode) {
        return true;
    }

    s_nfc_unit.disableField();
    GetHAL().delay(80);

    auto cfg      = s_nfc_unit.config();
    cfg.emulation = false;
    cfg.mode      = mode;
    s_nfc_unit.config(cfg);

    if (!s_nfc_unit.configureNFCMode(mode)) {
        return false;
    }

    GetHAL().delay(80);
    if (!s_nfc_unit.begin()) {
        return false;
    }

    s_nfc_hub.update();
    s_nfc_mode = mode;
    return true;
}

bool scanModeWindow(std::vector<AppNfcScan::NfcInfo>& items, m5::nfc::NFC mode, std::uint32_t window_ms)
{
    if (!switchNfcMode(mode)) {
        return false;
    }

    GetHAL().delay(NFC_MODE_STABLE_MS);
    const auto deadline = GetHAL().millis() + window_ms;
    do {
        s_nfc_hub.update();
        switch (mode) {
            case m5::nfc::NFC::A:
                if (scanA(items)) {
                    return true;
                }
                break;
            case m5::nfc::NFC::B:
                if (scanB(items)) {
                    return true;
                }
                break;
            case m5::nfc::NFC::F:
                if (scanF(items)) {
                    return true;
                }
                break;
            case m5::nfc::NFC::V:
                if (scanV(items)) {
                    return true;
                }
                break;
            default:
                return false;
        }
        GetHAL().delay(NFC_DETECT_GAP_MS);
    } while (GetHAL().millis() <= deadline);

    return false;
}

bool scanAllModes(std::vector<AppNfcScan::NfcInfo>& items)
{
    if (scanModeWindow(items, m5::nfc::NFC::A, NFC_A_SCAN_WINDOW_MS)) {
        return true;
    }
    if (scanModeWindow(items, m5::nfc::NFC::B, NFC_OTHER_SCAN_WINDOW_MS)) {
        return true;
    }
    if (scanModeWindow(items, m5::nfc::NFC::F, NFC_OTHER_SCAN_WINDOW_MS)) {
        return true;
    }
    return scanModeWindow(items, m5::nfc::NFC::V, NFC_OTHER_SCAN_WINDOW_MS);
}

bool scanA(std::vector<AppNfcScan::NfcInfo>& items)
{
    if (!switchNfcMode(m5::nfc::NFC::A)) {
        return false;
    }

    s_nfc_hub.update();
    std::vector<m5::nfc::a::PICC> piccs;
    if (!s_nfc_layer_a.detect(piccs, NFC_DETECT_TIMEOUT_MS) || piccs.empty()) {
        return false;
    }

    auto& picc            = piccs[0];
    const bool identified = s_nfc_layer_a.identify(picc);
    if (identified) {
        s_nfc_layer_a.reactivate(picc);
    }

    addItem(items, "UID", picc.uidAsString());
    addItem(items, "TYPE", picc.typeAsString());
    addSizeItem(items, picc.userAreaSize(), picc.totalSize());
    addItem(items, "MODE", modeName(m5::nfc::NFC::A));

    if (identified) {
        s_nfc_layer_a.deactivate();
    }
    return true;
}

bool scanB(std::vector<AppNfcScan::NfcInfo>& items)
{
    if (!switchNfcMode(m5::nfc::NFC::B)) {
        return false;
    }

    s_nfc_hub.update();
    std::vector<m5::nfc::b::PICC> piccs;
    if (!s_nfc_layer_b.detect(piccs, 0x00, 4, NFC_DETECT_TIMEOUT_MS) || piccs.empty()) {
        return false;
    }

    const auto& picc = piccs[0];
    addItem(items, "PUPI", picc.pupiAsString());
    addItem(items, "TYPE", picc.typeAsString());
    addItem(items, "MODE", modeName(m5::nfc::NFC::B));
    return true;
}

bool scanF(std::vector<AppNfcScan::NfcInfo>& items)
{
    if (!switchNfcMode(m5::nfc::NFC::F)) {
        return false;
    }

    s_nfc_hub.update();
    std::vector<m5::nfc::f::PICC> piccs;
    if (!s_nfc_layer_f.detect(piccs, m5::nfc::f::TimeSlot::Slot16, NFC_DETECT_TIMEOUT_MS) || piccs.empty()) {
        return false;
    }

    const auto& picc = piccs[0];
    addItem(items, "IDm", picc.idmAsString());
    addItem(items, "TYPE", picc.typeAsString());
    addItem(items, "PMm", picc.pmmAsString());
    addItem(items, "MODE", modeName(m5::nfc::NFC::F));
    return true;
}

bool scanV(std::vector<AppNfcScan::NfcInfo>& items)
{
    if (!switchNfcMode(m5::nfc::NFC::V)) {
        return false;
    }

    s_nfc_hub.update();
    std::vector<m5::nfc::v::PICC> piccs;
    if (!s_nfc_layer_v.detect(piccs, NFC_DETECT_TIMEOUT_MS) || piccs.empty()) {
        return false;
    }

    const auto& picc = piccs[0];
    addItem(items, "UID", picc.uidAsString());
    addItem(items, "TYPE", picc.typeAsString());
    addSizeItem(items, picc.userAreaSize(), picc.totalSize());
    addItem(items, "MODE", modeName(m5::nfc::NFC::V));
    return true;
}

}  // namespace

void AppNfcScan::onCreate()
{
    setAppInfo().name = "AppNfcScan";
    open();
}

void AppNfcScan::onOpen()
{
    setStatus("READY");
}

void AppNfcScan::onRunning()
{
    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    finishScanIfReady();

    const auto now_ms = GetHAL().millis();
    if (now_ms - _last_input_ms < NFC_TOUCH_COOLDOWN_MS) {
        if (!_has_drawn || GetHAL().isFullRedrawRequested()) {
            drawNfc();
        }
        return;
    }

    if (GetHAL().wasTouchClickedArea(NFC_SCAN_X, NFC_SCAN_Y, NFC_SCAN_W, NFC_SCAN_H)) {
        _last_input_ms = now_ms;
        startScan();
        return;
    }

    if (GetHAL().wasTouchClickedArea(NFC_INFO_X, NFC_INFO_Y, NFC_INFO_W, NFC_INFO_H)) {
        _last_input_ms = now_ms;
        showNextInfo();
        return;
    }

    if (!_has_drawn || GetHAL().isFullRedrawRequested()) {
        drawNfc();
    }
}

void AppNfcScan::startScan()
{
    if (!ensureNfcResultMutex()) {
        GetHAL().playBuzzerPattern(NFC_NO_CARD_TONES, 2);
        setStatus("TASK FAIL");
        return;
    }

    xSemaphoreTake(s_nfc_result_mutex, portMAX_DELAY);
    if (s_nfc_task_running) {
        xSemaphoreGive(s_nfc_result_mutex);
        return;
    }
    s_nfc_task_running = true;
    s_nfc_task_done    = false;
    s_nfc_task_items.clear();
    s_nfc_task_status.clear();
    xSemaphoreGive(s_nfc_result_mutex);

    _scan_running = true;
    GetHAL().playBuzzerPattern(NFC_SCAN_START_TONES, 2);
    setStatus("SCANNING...");

    if (!ensureNfcHardware()) {
        powerDownNfcHardware();
        xSemaphoreTake(s_nfc_result_mutex, portMAX_DELAY);
        s_nfc_task_running = false;
        s_nfc_task_handle  = nullptr;
        xSemaphoreGive(s_nfc_result_mutex);
        _scan_running = false;
        GetHAL().playBuzzerPattern(NFC_NO_CARD_TONES, 2);
        setStatus("INIT FAIL");
        return;
    }

    const BaseType_t ok = xTaskCreatePinnedToCore(nfcScanTask, "nfc_scan", NFC_SCAN_TASK_STACK, nullptr,
                                                  NFC_SCAN_TASK_PRIORITY, &s_nfc_task_handle, tskNO_AFFINITY);
    if (ok != pdPASS) {
        powerDownNfcHardware();
        xSemaphoreTake(s_nfc_result_mutex, portMAX_DELAY);
        s_nfc_task_running = false;
        s_nfc_task_handle  = nullptr;
        xSemaphoreGive(s_nfc_result_mutex);
        _scan_running = false;
        GetHAL().playBuzzerPattern(NFC_NO_CARD_TONES, 2);
        setStatus("TASK FAIL");
    }
}

void AppNfcScan::finishScanIfReady()
{
    if (!_scan_running) {
        return;
    }

    std::vector<NfcInfo> found_items;
    std::string status;
    if (!takeNfcScanResult(found_items, status)) {
        return;
    }

    _scan_running = false;
    if (!status.empty()) {
        GetHAL().playBuzzerPattern(NFC_NO_CARD_TONES, 2);
        setStatus(status.c_str());
        return;
    }

    _info_items = std::move(found_items);
    _info_index = 0;
    GetHAL().playBuzzerPattern(NFC_CARD_FOUND_TONES, 2);
    drawNfc();
}

void AppNfcScan::setStatus(const char* text)
{
    if (_info_items.size() == 1 && _info_index == 0 && _info_items[0].label.empty() && _info_items[0].value == text) {
        return;
    }
    _info_items.clear();
    _info_items.push_back({"", text});
    _info_index = 0;
    drawNfc();
}

void AppNfcScan::setNoCard()
{
    if (_info_items.size() == 1 && _info_index == 0 && _info_items[0].label.empty() &&
        _info_items[0].value == "NO CARD") {
        return;
    }
    _info_items.clear();
    _info_items.push_back({"", "NO CARD"});
    _info_index = 0;
    drawNfc();
}

void AppNfcScan::showNextInfo()
{
    if (_info_items.empty()) {
        setStatus("READY");
        return;
    }
    if (_info_items.size() <= static_cast<std::size_t>(NFC_INFO_LINES_PER_PAGE)) {
        return;
    }

    const int item_count = static_cast<int>(_info_items.size());
    const int page_start = (_info_index / NFC_INFO_LINES_PER_PAGE) * NFC_INFO_LINES_PER_PAGE;
    _info_index          = static_cast<std::uint16_t>(page_start + NFC_INFO_LINES_PER_PAGE);
    if (_info_index >= item_count) {
        _info_index = 0;
    }
    drawNfc();
}

void AppNfcScan::drawNfc()
{
    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(NFC_INFO_X, NFC_INFO_Y, NFC_INFO_W, NFC_INFO_H, TFT_WHITE);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setTextSize(1.0f);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);

    std::vector<std::string> lines;
    if (_info_items.empty()) {
        lines.push_back("READY");
    } else {
        const int clamped_index =
            std::clamp(static_cast<int>(_info_index), 0, static_cast<int>(_info_items.size()) - 1);
        _info_index = static_cast<std::uint16_t>((clamped_index / NFC_INFO_LINES_PER_PAGE) * NFC_INFO_LINES_PER_PAGE);
        const int item_count = static_cast<int>(_info_items.size());
        for (int i = 0; i < NFC_INFO_LINES_PER_PAGE && _info_index + i < item_count; ++i) {
            lines.push_back(fitText(nfcInfoText(_info_items[_info_index + i]), NFC_INFO_W - 10));
        }
    }

    const int center_x = NFC_INFO_X + NFC_INFO_W / 2;
    const int center_y = NFC_INFO_Y + NFC_INFO_H / 2 + NFC_INFO_TEXT_Y_OFFSET;
    if (lines.size() == 1) {
        GetHAL().display.drawString(lines[0].c_str(), center_x, center_y);
    } else {
        const int first_y = center_y - NFC_INFO_LINE_GAP / 2;
        for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
            GetHAL().display.drawString(lines[i].c_str(), center_x, first_y + i * NFC_INFO_LINE_GAP);
        }
    }

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(NFC_INFO_X, NFC_INFO_Y, NFC_INFO_W, NFC_INFO_H);
        GetHAL().display.waitDisplay();
    }
    _has_drawn = true;
}

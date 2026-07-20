/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "apps.h"

#include <M5Unified.h>
#include <algorithm>
#include <assets.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <hal.h>
#include <image_utils.h>
#include <string>
#include <sys/stat.h>
#include <strings.h>

/* -------------------------------------------------------------------------- */
/*                                  Constants                                 */
/* -------------------------------------------------------------------------- */

static constexpr std::uint32_t TF_TOUCH_COOLDOWN_MS     = 250;
static constexpr std::uint32_t TF_CARD_POLL_MS          = 300;
static constexpr std::uint32_t TF_BROWSER_CARD_CHECK_MS = 250;
static constexpr int TF_SWIPE_THRESHOLD_PX              = 48;

static constexpr int TF_SUMMARY_X = 51;
static constexpr int TF_SUMMARY_Y = 668;
static constexpr int TF_SUMMARY_W = 190 - 51;
static constexpr int TF_SUMMARY_H = 779 - 668;

static constexpr int BROWSER_MARGIN                 = 14;
static constexpr int BROWSER_HEADER_H               = 58;
static constexpr int BROWSER_FOOTER_H               = 72;
static constexpr int BROWSER_ROW_H                  = 58;
static constexpr int BROWSER_BUTTON_H               = 48;
static constexpr int BROWSER_BUTTON_W               = 132;
static constexpr int BROWSER_SCROLL_W               = 5;
static constexpr int BROWSER_ICON_SIZE              = 32;
static constexpr int BROWSER_ICON_TEXT_GAP          = 12;
static constexpr int BROWSER_NAME_SIZE_GAP          = 14;
static constexpr std::size_t BROWSER_NAME_MAX_CHARS = 22;
static constexpr int IMAGE_OVERLAY_HEADER_H         = 52;
static constexpr int IMAGE_OVERLAY_FOOTER_H         = 68;

static constexpr BuzzerTone TF_CARD_INSERT_TONES[] = {
    {.start_freq_hz = 740, .end_freq_hz = 740, .duration_ms = 9, .gap_ms = 5},
    {.start_freq_hz = 1110, .end_freq_hz = 1110, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 1480, .end_freq_hz = 1480, .duration_ms = 16, .gap_ms = 0},
};

static constexpr BuzzerTone TF_CARD_REMOVE_TONES[] = {
    {.start_freq_hz = 1320, .end_freq_hz = 1320, .duration_ms = 8, .gap_ms = 5},
    {.start_freq_hz = 760, .end_freq_hz = 760, .duration_ms = 18, .gap_ms = 0},
};

static constexpr BuzzerTone TF_CARD_FAIL_TONES[] = {
    {.start_freq_hz = 680, .end_freq_hz = 680, .duration_ms = 9, .gap_ms = 5},
    {.start_freq_hz = 430, .end_freq_hz = 430, .duration_ms = 20, .gap_ms = 0},
};

static constexpr BuzzerTone TF_OPEN_BROWSER_TONES[] = {
    {.start_freq_hz = 830, .end_freq_hz = 830, .duration_ms = 8, .gap_ms = 5},
    {.start_freq_hz = 1240, .end_freq_hz = 1240, .duration_ms = 13, .gap_ms = 0},
};

static constexpr BuzzerTone TF_FOLDER_OPEN_TONES[] = {
    {.start_freq_hz = 760, .end_freq_hz = 760, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 1140, .end_freq_hz = 1140, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 1520, .end_freq_hz = 1520, .duration_ms = 12, .gap_ms = 0},
};

static constexpr BuzzerTone TF_BACK_TONES[] = {
    {.start_freq_hz = 1200, .end_freq_hz = 1200, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 850, .end_freq_hz = 850, .duration_ms = 13, .gap_ms = 0},
};

static constexpr BuzzerTone TF_CLOSE_TONES[] = {
    {.start_freq_hz = 1050, .end_freq_hz = 1050, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 700, .end_freq_hz = 700, .duration_ms = 12, .gap_ms = 0},
};

static constexpr BuzzerTone TF_IMAGE_OPEN_TONES[] = {
    {.start_freq_hz = 1040, .end_freq_hz = 1040, .duration_ms = 8, .gap_ms = 4},
    {.start_freq_hz = 1560, .end_freq_hz = 1560, .duration_ms = 10, .gap_ms = 5},
    {.start_freq_hz = 2080, .end_freq_hz = 2080, .duration_ms = 14, .gap_ms = 0},
};

static constexpr BuzzerTone TF_IMAGE_NEXT_TONES[] = {
    {.start_freq_hz = 1450, .end_freq_hz = 1450, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 1900, .end_freq_hz = 1900, .duration_ms = 11, .gap_ms = 0},
};

static constexpr BuzzerTone TF_IMAGE_PREV_TONES[] = {
    {.start_freq_hz = 1900, .end_freq_hz = 1900, .duration_ms = 7, .gap_ms = 4},
    {.start_freq_hz = 1450, .end_freq_hz = 1450, .duration_ms = 11, .gap_ms = 0},
};

static constexpr BuzzerTone TF_OVERLAY_TONES[] = {
    {.start_freq_hz = 1120, .end_freq_hz = 1120, .duration_ms = 7, .gap_ms = 0},
};

static constexpr BuzzerTone TF_UNSUPPORTED_TONES[] = {
    {.start_freq_hz = 520, .end_freq_hz = 520, .duration_ms = 14, .gap_ms = 0},
};

/* -------------------------------------------------------------------------- */
/*                                File Helpers                                */
/* -------------------------------------------------------------------------- */

namespace {

bool pointInRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

std::string fitText(std::string text, int max_width)
{
    static constexpr const char* kEllipsis = "...";
    if (GetHAL().display.textWidth(text.c_str()) <= max_width) {
        return text;
    }

    while (text.size() > 1) {
        std::size_t last_char = text.size() - 1;
        while (last_char > 0 && (static_cast<unsigned char>(text[last_char]) & 0xC0) == 0x80) {
            --last_char;
        }
        text.erase(last_char);
        const std::string clipped = text + kEllipsis;
        if (GetHAL().display.textWidth(clipped.c_str()) <= max_width) {
            return clipped;
        }
    }
    return kEllipsis;
}

std::string limitUtf8Chars(const std::string& text, std::size_t max_chars)
{
    std::size_t bytes = 0;
    std::size_t chars = 0;
    while (bytes < text.size() && chars < max_chars) {
        const unsigned char c  = static_cast<unsigned char>(text[bytes]);
        std::size_t char_bytes = 1;
        if ((c & 0x80) == 0) {
            char_bytes = 1;
        } else if ((c & 0xE0) == 0xC0) {
            char_bytes = 2;
        } else if ((c & 0xF0) == 0xE0) {
            char_bytes = 3;
        } else if ((c & 0xF8) == 0xF0) {
            char_bytes = 4;
        }
        if (bytes + char_bytes > text.size()) {
            break;
        }
        bytes += char_bytes;
        ++chars;
    }

    if (bytes >= text.size()) {
        return text;
    }
    return text.substr(0, bytes) + "...";
}

std::string formatBytes(std::uint64_t bytes)
{
    const char* units[] = {"B", "KB", "MB", "GB"};
    double value        = static_cast<double>(bytes);
    int unit            = 0;
    while (value >= 1024.0 && unit < 3) {
        value /= 1024.0;
        ++unit;
    }

    char text[24] = {};
    if (unit == 0) {
        std::snprintf(text, sizeof(text), "%lluB", static_cast<unsigned long long>(bytes));
    } else {
        std::snprintf(text, sizeof(text), "%.1f%s", value, units[unit]);
    }
    return text;
}

bool isHiddenEntry(const char* name)
{
    return std::strcmp(name, ".") == 0 || std::strcmp(name, "..") == 0;
}

const char* fileExtension(const std::string& path)
{
    const char* dot = std::strrchr(path.c_str(), '.');
    return dot ? dot : "";
}

bool isImageFile(const std::string& path)
{
    const char* ext = fileExtension(path);
    return strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0 || strcasecmp(ext, ".png") == 0 ||
           strcasecmp(ext, ".bmp") == 0;
}

bool isCardPresentStatus(TfCardStatus status)
{
    return status == TfCardStatus::Inserted || status == TfCardStatus::Mounted || status == TfCardStatus::MountFailed;
}

std::string fileNameFromPath(const std::string& path)
{
    const std::size_t slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string browserRelativePath(const std::string& path, const std::string& root)
{
    if (path == root) {
        return "/";
    }

    if (path.size() > root.size() && path.compare(0, root.size(), root) == 0) {
        std::string relative = path.substr(root.size());
        if (relative.empty()) {
            return "/";
        }
        if (relative.front() != '/') {
            relative.insert(relative.begin(), '/');
        }
        return relative;
    }

    return fileNameFromPath(path);
}

void drawEntryIcon(bool is_dir, bool is_image, int x, int center_y)
{
    const std::uint8_t* start = img_file_start;
    const std::uint8_t* end   = img_file_end;
    if (is_dir) {
        start = img_folder_start;
        end   = img_folder_end;
    } else if (is_image) {
        start = img_picture_start;
        end   = img_picture_end;
    }

    GetHAL().display.drawPng(start, end - start, x, center_y - BROWSER_ICON_SIZE / 2);
}

}  // namespace

/* -------------------------------------------------------------------------- */
/*                                 Lifecycle                                  */
/* -------------------------------------------------------------------------- */

void AppTfCard::onCreate()
{
    setAppInfo().name = "AppTfCard";
    _current_path     = GetHAL().tfMountPoint();
    open();
}

void AppTfCard::onOpen()
{
    updateCardState(true);
}

void AppTfCard::onRunning()
{
    if (_browser_open) {
        GetHAL().setUiModalActive(true);
        if (!ensureCardPresentForBrowser()) {
            return;
        }

        if (_image_view_open) {
            handleImageViewerTouch();
            if (!_browser_open) {
                return;
            }
            if (!_image_view_open) {
                return;
            }
            if (!_image_view_drawn || GetHAL().isRefreshRequested()) {
                drawImageViewer(false);
            }
            return;
        }

        handleBrowserTouch();
        if (!_browser_open) {
            return;
        }
        if (!_browser_drawn || GetHAL().isRefreshRequested()) {
            drawBrowser();
        }
        return;
    }

    if (GetHAL().isUiModalActive()) {
        _has_drawn = false;
        return;
    }

    const auto now_ms = GetHAL().millis();
    if (GetHAL().wasTouchClickedArea(TF_SUMMARY_X, TF_SUMMARY_Y, TF_SUMMARY_W, TF_SUMMARY_H) &&
        now_ms - _last_touch_ms >= TF_TOUCH_COOLDOWN_MS) {
        _last_touch_ms                     = now_ms;
        const TfCardStatus previous_status = GetHAL().getTfCardInfo().status;
        const auto& info                   = GetHAL().updateTfCard();
        if (!isCardPresentStatus(previous_status) && info.status == previous_status) {
            GetHAL().playBuzzerPattern(TF_CARD_FAIL_TONES, 2);
            return;
        }
        handleCardStatusSound(info.status);
        if (info.status == TfCardStatus::Mounted) {
            GetHAL().playBuzzerPattern(TF_OPEN_BROWSER_TONES, 2);
            openBrowser();
        } else {
            GetHAL().playBuzzerPattern(TF_CARD_FAIL_TONES, 2);
            drawSummary();
        }
        return;
    }

    if (!_has_drawn || GetHAL().isRefreshRequested()) {
        updateCardState(true);
    } else if (now_ms - _last_card_poll_ms >= TF_CARD_POLL_MS) {
        updateCardState(false);
    }
}

/* -------------------------------------------------------------------------- */
/*                                Card State                                  */
/* -------------------------------------------------------------------------- */

void AppTfCard::updateCardState(bool force_draw)
{
    _last_card_poll_ms                 = GetHAL().millis();
    const TfCardStatus previous_status = GetHAL().getTfCardInfo().status;
    const auto& info                   = force_draw ? GetHAL().updateTfCard() : GetHAL().refreshTfCardPresence();
    handleCardStatusSound(info.status);
    if (force_draw || info.status != previous_status) {
        if (info.status != previous_status) {
            GetHAL().requestRefresh();
        }
        drawSummary();
    }
}

void AppTfCard::drawSummary()
{
    const auto& info = GetHAL().getTfCardInfo();

    GetHAL().display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    GetHAL().display.fillRect(TF_SUMMARY_X, TF_SUMMARY_Y, TF_SUMMARY_W, TF_SUMMARY_H, TFT_WHITE);
    GetHAL().display.setTextColor(TFT_BLACK, TFT_WHITE);
    GetHAL().display.setTextDatum(middle_center);
    GetHAL().display.setFont(&fonts::FreeSansBold9pt7b);
    GetHAL().display.setTextSize(1.0f);

    std::vector<std::string> lines;
    if (info.status == TfCardStatus::Mounted) {
        lines.push_back("TF CARD");
        lines.push_back(formatBytes(info.card_bytes));
        if (info.free_bytes > 0) {
            lines.push_back("FREE " + formatBytes(info.free_bytes));
        }
    } else if (info.status == TfCardStatus::Inserted) {
        lines.push_back("TF CARD");
        lines.push_back("TAP OPEN");
    } else if (info.status == TfCardStatus::MountFailed) {
        lines.push_back("INIT FAIL");
        lines.push_back(info.message.empty() ? "UNKNOWN" : info.message);
    } else {
        lines.push_back("NO FOUND");
    }

    const int line_gap = lines.size() >= 3 ? 25 : 28;
    const int center_y = TF_SUMMARY_Y + TF_SUMMARY_H / 2;
    const int first_y  = center_y - (static_cast<int>(lines.size()) - 1) * line_gap / 2;
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        const std::string line = fitText(lines[i], TF_SUMMARY_W - 8);
        GetHAL().display.drawString(line.c_str(), TF_SUMMARY_X + TF_SUMMARY_W / 2, first_y + i * line_gap);
    }

    if (!GetHAL().isRefreshRequested()) {
        GetHAL().flushDisplay(TF_SUMMARY_X, TF_SUMMARY_Y, TF_SUMMARY_W, TF_SUMMARY_H);
        GetHAL().display.waitDisplay();
    }

    _has_drawn = true;
}

void AppTfCard::openBrowser()
{
    if (!loadDirectory(GetHAL().tfMountPoint())) {
        GetHAL().playBuzzerPattern(TF_CARD_FAIL_TONES, 2);
        drawSummary();
        return;
    }

    _browser_open       = true;
    _browser_drawn      = false;
    _last_card_check_ms = 0;
    GetHAL().setUiModalActive(true);
    drawBrowser();
}

void AppTfCard::closeBrowser()
{
    _browser_open          = false;
    _browser_drawn         = false;
    _image_view_open       = false;
    _image_view_drawn      = false;
    _image_overlay_visible = false;
    _image_path.clear();
    clearImageCleanCache();
    _has_drawn          = false;
    _last_card_check_ms = 0;
    GetHAL().setUiModalActive(false);
    GetHAL().drawBackground(m5gfx::epd_mode_t::epd_fastest, false);
    GetHAL().requestRefreshNextFrame();
}

void AppTfCard::openImageViewer(const std::string& path)
{
    _image_path            = path;
    _image_view_open       = true;
    _image_view_drawn      = false;
    _image_overlay_visible = false;
    clearImageCleanCache();

    auto& display         = GetHAL().display;
    const auto saved_mode = display.getEpdMode();
    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.fillScreen(TFT_WHITE);
    display.display();
    display.waitDisplay();
    display.setEpdMode(saved_mode);

    drawImageViewer(true);
}

void AppTfCard::closeImageViewer()
{
    _image_view_open       = false;
    _image_view_drawn      = false;
    _image_overlay_visible = false;
    _browser_drawn         = false;
    _image_path.clear();
    clearImageCleanCache();
    drawBrowser();
}

bool AppTfCard::ensureCardPresentForBrowser()
{
    const auto now_ms = GetHAL().millis();
    if (now_ms - _last_card_check_ms < TF_BROWSER_CARD_CHECK_MS) {
        return true;
    }
    _last_card_check_ms = now_ms;

    const auto& info = GetHAL().updateTfCard();
    handleCardStatusSound(info.status);
    if (info.status == TfCardStatus::Mounted) {
        return true;
    }

    closeBrowser();
    return false;
}

void AppTfCard::handleCardStatusSound(TfCardStatus status)
{
    if (!_card_sound_ready) {
        _last_card_sound_status = status;
        _card_sound_ready       = true;
        return;
    }

    const bool was_present = isCardPresentStatus(_last_card_sound_status);
    const bool is_present  = isCardPresentStatus(status);
    if (!was_present && is_present) {
        GetHAL().playBuzzerPattern(status == TfCardStatus::MountFailed ? TF_CARD_FAIL_TONES : TF_CARD_INSERT_TONES,
                                   status == TfCardStatus::MountFailed ? 2 : 3);
    } else if (was_present && status == TfCardStatus::NoCard) {
        GetHAL().playBuzzerPattern(TF_CARD_REMOVE_TONES, 2);
    } else if (_last_card_sound_status != TfCardStatus::MountFailed && status == TfCardStatus::MountFailed) {
        GetHAL().playBuzzerPattern(TF_CARD_FAIL_TONES, 2);
    }

    _last_card_sound_status = status;
}

/* -------------------------------------------------------------------------- */
/*                                  Browser                                   */
/* -------------------------------------------------------------------------- */

bool AppTfCard::loadDirectory(const std::string& path)
{
    DIR* dir = opendir(path.c_str());
    if (!dir) {
        _browser_status = "OPEN FAIL";
        return false;
    }

    std::vector<FileEntry> entries;
    while (dirent* entry = readdir(dir)) {
        if (isHiddenEntry(entry->d_name)) {
            continue;
        }

        FileEntry item;
        item.name = entry->d_name;
        item.path = path;
        if (!item.path.empty() && item.path.back() != '/') {
            item.path += "/";
        }
        item.path += item.name;

        struct stat st = {};
        if (stat(item.path.c_str(), &st) == 0) {
            item.is_dir = S_ISDIR(st.st_mode);
            item.size   = static_cast<std::uint64_t>(st.st_size);
        } else {
            item.is_dir = entry->d_type == DT_DIR;
        }
        entries.push_back(std::move(item));
    }
    closedir(dir);

    std::sort(entries.begin(), entries.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.is_dir != b.is_dir) {
            return a.is_dir > b.is_dir;
        }
        return a.name < b.name;
    });

    _entries       = std::move(entries);
    _current_path  = path;
    _current_title = browserRelativePath(path, GetHAL().tfMountPoint());
    _scroll_index  = 0;
    _browser_status.clear();
    _browser_drawn = false;
    return true;
}

void AppTfCard::drawBrowser()
{
    auto& display          = GetHAL().display;
    const int screen_w     = display.width();
    const int screen_h     = display.height();
    const int list_x       = BROWSER_MARGIN;
    const int list_y       = BROWSER_HEADER_H + 10;
    const int list_w       = screen_w - BROWSER_MARGIN * 2;
    const int list_h       = screen_h - BROWSER_HEADER_H - BROWSER_FOOTER_H - 18;
    const int visible_rows = std::max(1, list_h / BROWSER_ROW_H);
    const int max_scroll   = std::max(0, static_cast<int>(_entries.size()) - visible_rows);
    _scroll_index          = static_cast<std::int16_t>(std::clamp(static_cast<int>(_scroll_index), 0, max_scroll));

    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    display.fillScreen(TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);

    display.fillRect(0, 0, screen_w, BROWSER_HEADER_H, TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextDatum(middle_left);
    display.setFont(&fonts::FreeSansBold12pt7b);
    display.setTextSize(1.0f);
    display.drawString("TF FILES", BROWSER_MARGIN, BROWSER_HEADER_H / 2 + 4);

    display.setFont(&fonts::efontCN_24_b);
    display.setTextDatum(middle_right);
    const std::string path_text = fitText(_current_title, screen_w - 190);
    display.drawString(path_text.c_str(), screen_w - BROWSER_MARGIN, BROWSER_HEADER_H / 2);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    const int button_y = screen_h - BROWSER_BUTTON_H - 12;
    const int back_x   = BROWSER_MARGIN;
    const int close_x  = screen_w - BROWSER_MARGIN - BROWSER_BUTTON_W;
    display.drawRoundRect(back_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H, 5, TFT_BLACK);
    display.drawString("BACK", back_x + BROWSER_BUTTON_W / 2, button_y + BROWSER_BUTTON_H / 2);
    display.drawRoundRect(close_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H, 5, TFT_BLACK);
    display.drawString("CLOSE", close_x + BROWSER_BUTTON_W / 2, button_y + BROWSER_BUTTON_H / 2);

    display.setTextDatum(middle_left);
    display.setFont(&fonts::FreeSansBold9pt7b);
    if (!_browser_status.empty()) {
        display.drawString(_browser_status.c_str(), list_x, list_y + 24);
    } else if (_entries.empty()) {
        display.drawString("EMPTY", list_x, list_y + 24);
    } else {
        display.setFont(&fonts::efontCN_24_b);
        for (int row = 0; row < visible_rows; ++row) {
            const int index = _scroll_index + row;
            if (index >= static_cast<int>(_entries.size())) {
                break;
            }

            const FileEntry& item  = _entries[index];
            const int row_y        = list_y + row * BROWSER_ROW_H;
            const int row_center_y = row_y + BROWSER_ROW_H / 2;
            drawEntryIcon(item.is_dir, isImageFile(item.path), list_x + 2, row_center_y);
            const int name_x            = list_x + 2 + BROWSER_ICON_SIZE + BROWSER_ICON_TEXT_GAP;
            const int right_x           = list_x + list_w - 14;
            const std::string size_text = item.is_dir ? std::string() : formatBytes(item.size);
            const int size_w = item.is_dir ? 0 : display.textWidth(size_text.c_str()) + BROWSER_NAME_SIZE_GAP;
            const std::string name =
                fitText(limitUtf8Chars(item.name, BROWSER_NAME_MAX_CHARS), right_x - size_w - name_x);
            display.drawString(name.c_str(), name_x, row_center_y);
            if (!item.is_dir) {
                display.setTextDatum(middle_right);
                display.setFont(&fonts::efontCN_24_b);
                display.drawString(size_text.c_str(), right_x, row_center_y);
                display.setFont(&fonts::efontCN_24_b);
                display.setTextDatum(middle_left);
            }
        }
    }

    if (max_scroll > 0) {
        const int bar_x = screen_w - BROWSER_MARGIN - BROWSER_SCROLL_W;
        display.fillRect(bar_x, list_y, BROWSER_SCROLL_W, list_h, TFT_WHITE);
        display.drawRect(bar_x, list_y, BROWSER_SCROLL_W, list_h, TFT_BLACK);
        const int thumb_h = std::max(24, list_h * visible_rows / static_cast<int>(_entries.size()));
        const int thumb_y = list_y + (list_h - thumb_h) * _scroll_index / max_scroll;
        display.fillRect(bar_x, thumb_y, BROWSER_SCROLL_W, thumb_h, TFT_BLACK);
    }

    GetHAL().flushDisplay();
    display.waitDisplay();
    _browser_drawn = true;
}

void AppTfCard::handleBrowserTouch()
{
    const auto& touch      = GetHAL().getTouchDetail();
    const auto now_ms      = GetHAL().millis();
    const auto& p          = M5.Touch.getTouchPointRaw();
    auto& display          = GetHAL().display;
    const int screen_w     = display.width();
    const int screen_h     = display.height();
    const int list_y       = BROWSER_HEADER_H + 10;
    const int list_h       = screen_h - BROWSER_HEADER_H - BROWSER_FOOTER_H - 18;
    const int visible_rows = std::max(1, list_h / BROWSER_ROW_H);
    const int max_scroll   = std::max(0, static_cast<int>(_entries.size()) - visible_rows);
    const int button_y     = screen_h - BROWSER_BUTTON_H - 12;
    const int back_x       = BROWSER_MARGIN;
    const int close_x      = screen_w - BROWSER_MARGIN - BROWSER_BUTTON_W;

    if (touch.wasPressed()) {
        _browser_touching = true;
        _touch_start_x    = static_cast<std::int16_t>(p.x);
        _touch_start_y    = static_cast<std::int16_t>(p.y);
        return;
    }

    if (!touch.wasReleased() || !_browser_touching) {
        return;
    }
    _browser_touching = false;

    if (now_ms - _last_touch_ms < TF_TOUCH_COOLDOWN_MS) {
        return;
    }
    _last_touch_ms = now_ms;

    const int dx               = p.x - _touch_start_x;
    const int dy               = p.y - _touch_start_y;
    const std::string root     = GetHAL().tfMountPoint();
    const bool at_root         = _current_path == root;
    const bool started_in_list = _touch_start_y >= list_y && _touch_start_y < list_y + list_h;
    const bool is_vertical_swipe =
        started_in_list && std::abs(dy) >= TF_SWIPE_THRESHOLD_PX && std::abs(dy) > std::abs(dx);
    if (max_scroll > 0 && is_vertical_swipe) {
        const std::int16_t old_scroll_index = _scroll_index;
        if (dy < 0) {
            _scroll_index =
                static_cast<std::int16_t>(std::min(max_scroll, static_cast<int>(_scroll_index) + visible_rows));
        } else {
            _scroll_index = static_cast<std::int16_t>(std::max(0, static_cast<int>(_scroll_index) - visible_rows));
        }
        if (_scroll_index != old_scroll_index) {
            drawBrowser();
        }
        return;
    }

    if (pointInRect(p.x, p.y, back_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H)) {
        if (at_root) {
            GetHAL().playBuzzerPattern(TF_CLOSE_TONES, 2);
            closeBrowser();
        } else {
            goParent();
        }
        return;
    }
    if (pointInRect(p.x, p.y, close_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H)) {
        GetHAL().playBuzzerPattern(TF_CLOSE_TONES, 2);
        closeBrowser();
        return;
    }

    if (std::abs(dx) < TF_SWIPE_THRESHOLD_PX && std::abs(dy) < TF_SWIPE_THRESHOLD_PX && p.y >= list_y &&
        p.y < list_y + visible_rows * BROWSER_ROW_H) {
        const int row   = (_touch_start_y - list_y) / BROWSER_ROW_H;
        const int index = _scroll_index + row;
        if (index >= 0 && index < static_cast<int>(_entries.size())) {
            const FileEntry entry = _entries[index];
            if (entry.is_dir && loadDirectory(entry.path)) {
                GetHAL().playBuzzerPattern(TF_FOLDER_OPEN_TONES, 3);
                drawBrowser();
            } else if (!entry.is_dir && isImageFile(entry.path)) {
                GetHAL().playBuzzerPattern(TF_IMAGE_OPEN_TONES, 3);
                openImageViewer(entry.path);
            } else {
                GetHAL().playBuzzerPattern(TF_UNSUPPORTED_TONES, 1);
            }
        }
    }
}

/* -------------------------------------------------------------------------- */
/*                               Image Viewer                                 */
/* -------------------------------------------------------------------------- */

void AppTfCard::drawImageViewer(bool image_refresh)
{
    auto& display      = GetHAL().display;
    const int screen_w = display.width();
    const int screen_h = display.height();
    const int image_y  = 0;
    const int image_h  = screen_h;

    display.setEpdMode(image_refresh ? m5gfx::epd_mode_t::epd_quality : m5gfx::epd_mode_t::epd_fastest);

    if (!restoreImageCleanCache()) {
        display.fillScreen(TFT_WHITE);
        display.setTextSize(1.0f);

        int image_w_px = 0;
        int image_h_px = 0;
        bool ok        = get_image_size_from_file(_image_path.c_str(), &image_w_px, &image_h_px);
        if (ok) {
            const float scale =
                std::min(static_cast<float>(screen_w) / image_w_px, static_cast<float>(image_h) / image_h_px);
            const int draw_w = static_cast<int>(image_w_px * scale);
            const int draw_h = static_cast<int>(image_h_px * scale);
            const int draw_x = (screen_w - draw_w) / 2;
            const int draw_y = image_y + (image_h - draw_h) / 2;
            const char* ext  = fileExtension(_image_path);

            if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
                ok = display.drawJpgFile(_image_path.c_str(), draw_x, draw_y, 0, 0, 0, 0, scale, scale);
            } else if (strcasecmp(ext, ".png") == 0) {
                ok = display.drawPngFile(_image_path.c_str(), draw_x, draw_y, 0, 0, 0, 0, scale, scale);
            } else if (strcasecmp(ext, ".bmp") == 0) {
                ok = display.drawBmpFile(_image_path.c_str(), draw_x, draw_y, 0, 0, 0, 0, scale, scale);
            } else {
                ok = false;
            }
        }

        if (!ok) {
            display.fillRect(0, image_y, screen_w, image_h, TFT_WHITE);
            display.setTextColor(TFT_BLACK, TFT_WHITE);
            display.setTextDatum(middle_center);
            display.setFont(&fonts::FreeSansBold9pt7b);
            display.drawString("IMAGE OPEN FAIL", screen_w / 2, image_y + image_h / 2);
        }

        captureImageCleanCache();
    }

    if (_image_overlay_visible) {
        drawImageOverlay();
    }

    GetHAL().flushDisplay();
    display.waitDisplay();
    _image_view_drawn = true;
}

void AppTfCard::handleImageViewerTouch()
{
    const auto& touch  = GetHAL().getTouchDetail();
    const auto now_ms  = GetHAL().millis();
    const auto& p      = M5.Touch.getTouchPointRaw();
    auto& display      = GetHAL().display;
    const int screen_w = display.width();
    const int screen_h = display.height();
    const int button_y = screen_h - BROWSER_BUTTON_H - 10;
    const int prev_x   = BROWSER_MARGIN;
    const int close_x  = (screen_w - BROWSER_BUTTON_W) / 2;
    const int next_x   = screen_w - BROWSER_MARGIN - BROWSER_BUTTON_W;

    if (touch.wasPressed()) {
        _browser_touching = true;
        _touch_start_x    = static_cast<std::int16_t>(p.x);
        _touch_start_y    = static_cast<std::int16_t>(p.y);
        return;
    }

    if (!touch.wasReleased() || !_browser_touching) {
        return;
    }
    _browser_touching = false;

    if (now_ms - _last_touch_ms < TF_TOUCH_COOLDOWN_MS) {
        return;
    }
    _last_touch_ms = now_ms;

    const int dx = p.x - _touch_start_x;
    const int dy = p.y - _touch_start_y;

    if (_image_overlay_visible && pointInRect(p.x, p.y, prev_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H)) {
        if (switchImage(-1)) {
            GetHAL().playBuzzerPattern(TF_IMAGE_PREV_TONES, 2);
            drawImageViewer(true);
        } else {
            GetHAL().playBuzzerPattern(TF_UNSUPPORTED_TONES, 1);
        }
        return;
    }
    if (_image_overlay_visible && pointInRect(p.x, p.y, close_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H)) {
        GetHAL().playBuzzerPattern(TF_CLOSE_TONES, 2);
        closeBrowser();
        return;
    }
    if (_image_overlay_visible && pointInRect(p.x, p.y, next_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H)) {
        if (switchImage(1)) {
            GetHAL().playBuzzerPattern(TF_IMAGE_NEXT_TONES, 2);
            drawImageViewer(true);
        } else {
            GetHAL().playBuzzerPattern(TF_UNSUPPORTED_TONES, 1);
        }
        return;
    }

    if (std::abs(dx) < TF_SWIPE_THRESHOLD_PX && std::abs(dy) < TF_SWIPE_THRESHOLD_PX) {
        GetHAL().playBuzzerPattern(TF_OVERLAY_TONES, 1);
        _image_overlay_visible = !_image_overlay_visible;
        _image_view_drawn      = false;
        drawImageViewer(!_image_overlay_visible);
    }
}

void AppTfCard::clearImageCleanCache()
{
    _image_clean_cache.clear();
    _image_clean_cache_w     = 0;
    _image_clean_cache_h     = 0;
    _image_clean_cache_valid = false;
}

void AppTfCard::captureImageCleanCache()
{
    auto& display      = GetHAL().display;
    const int screen_w = display.width();
    const int screen_h = display.height();
    if (screen_w <= 0 || screen_h <= 0) {
        clearImageCleanCache();
        return;
    }

    const std::size_t buffer_size = static_cast<std::size_t>(screen_w) * static_cast<std::size_t>(screen_h);
    _image_clean_cache.resize(buffer_size);
    display.readRect(0, 0, screen_w, screen_h, _image_clean_cache.data());
    _image_clean_cache_w     = static_cast<std::int16_t>(screen_w);
    _image_clean_cache_h     = static_cast<std::int16_t>(screen_h);
    _image_clean_cache_valid = true;
}

bool AppTfCard::restoreImageCleanCache()
{
    if (!_image_clean_cache_valid || _image_clean_cache.empty()) {
        return false;
    }

    auto& display = GetHAL().display;
    if (_image_clean_cache_w != display.width() || _image_clean_cache_h != display.height()) {
        clearImageCleanCache();
        return false;
    }

    display.pushImage(0, 0, _image_clean_cache_w, _image_clean_cache_h, _image_clean_cache.data());
    return true;
}

void AppTfCard::drawImageOverlay()
{
    auto& display      = GetHAL().display;
    const int screen_w = display.width();
    const int screen_h = display.height();
    const int button_y = screen_h - BROWSER_BUTTON_H - 10;
    const int prev_x   = BROWSER_MARGIN;
    const int close_x  = (screen_w - BROWSER_BUTTON_W) / 2;
    const int next_x   = screen_w - BROWSER_MARGIN - BROWSER_BUTTON_W;

    display.fillRect(0, 0, screen_w, IMAGE_OVERLAY_HEADER_H, TFT_BLACK);
    display.setTextColor(TFT_WHITE, TFT_BLACK);
    display.setTextDatum(middle_left);
    display.setFont(&fonts::efontCN_24_b);
    display.drawString(fitText(fileNameFromPath(_image_path), screen_w - BROWSER_MARGIN * 2).c_str(), BROWSER_MARGIN,
                       IMAGE_OVERLAY_HEADER_H / 2);

    display.fillRect(0, screen_h - IMAGE_OVERLAY_FOOTER_H, screen_w, IMAGE_OVERLAY_FOOTER_H, TFT_WHITE);
    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.drawRoundRect(prev_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H, 5, TFT_BLACK);
    display.drawString("PREV", prev_x + BROWSER_BUTTON_W / 2, button_y + BROWSER_BUTTON_H / 2);
    display.drawRoundRect(close_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H, 5, TFT_BLACK);
    display.drawString("CLOSE", close_x + BROWSER_BUTTON_W / 2, button_y + BROWSER_BUTTON_H / 2);
    display.drawRoundRect(next_x, button_y, BROWSER_BUTTON_W, BROWSER_BUTTON_H, 5, TFT_BLACK);
    display.drawString("NEXT", next_x + BROWSER_BUTTON_W / 2, button_y + BROWSER_BUTTON_H / 2);
}

bool AppTfCard::switchImage(int direction)
{
    if (_entries.empty() || _image_path.empty()) {
        return false;
    }

    std::vector<int> image_indices;
    image_indices.reserve(_entries.size());
    int current_image_pos = -1;
    for (int i = 0; i < static_cast<int>(_entries.size()); ++i) {
        if (_entries[i].is_dir || !isImageFile(_entries[i].path)) {
            continue;
        }
        if (_entries[i].path == _image_path) {
            current_image_pos = static_cast<int>(image_indices.size());
        }
        image_indices.push_back(i);
    }

    if (image_indices.size() <= 1 || current_image_pos < 0) {
        return false;
    }

    int next_pos = current_image_pos + (direction >= 0 ? 1 : -1);
    if (next_pos < 0) {
        next_pos = static_cast<int>(image_indices.size()) - 1;
    } else if (next_pos >= static_cast<int>(image_indices.size())) {
        next_pos = 0;
    }

    _image_path            = _entries[image_indices[next_pos]].path;
    _image_overlay_visible = true;
    _image_view_drawn      = false;
    clearImageCleanCache();
    return true;
}

bool AppTfCard::goParent()
{
    const std::string root = GetHAL().tfMountPoint();
    if (_current_path == root) {
        GetHAL().playBuzzerPattern(TF_UNSUPPORTED_TONES, 1);
        drawBrowser();
        return false;
    }

    std::string parent      = _current_path;
    const std::size_t slash = parent.find_last_of('/');
    if (slash == std::string::npos || slash <= root.size()) {
        parent = root;
    } else {
        parent = parent.substr(0, slash);
    }

    if (loadDirectory(parent)) {
        GetHAL().playBuzzerPattern(TF_BACK_TONES, 2);
        drawBrowser();
        return true;
    }
    GetHAL().playBuzzerPattern(TF_UNSUPPORTED_TONES, 1);
    return false;
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <hal.h>
#include <mooncake.h>
#include <cstdint>
#include <string>
#include <vector>

class AppTfCard : public mooncake::AppAbility {
public:
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;

private:
    struct FileEntry {
        std::string name;
        std::string path;
        std::uint64_t size = 0;
        bool is_dir        = false;
    };

    std::vector<FileEntry> _entries;
    std::string _current_path;
    std::string _current_title;
    std::string _browser_status;
    std::string _image_path;
    std::vector<std::uint16_t> _image_clean_cache;
    std::uint32_t _last_touch_ms         = 0;
    std::uint32_t _last_card_check_ms    = 0;
    std::uint32_t _last_card_poll_ms     = 0;
    TfCardStatus _last_card_sound_status = {};
    std::int16_t _scroll_index           = 0;
    std::int16_t _touch_start_x          = 0;
    std::int16_t _touch_start_y          = 0;
    std::int16_t _image_clean_cache_w    = 0;
    std::int16_t _image_clean_cache_h    = 0;
    bool _has_drawn                      = false;
    bool _browser_open                   = false;
    bool _browser_drawn                  = false;
    bool _browser_touching               = false;
    bool _card_sound_ready               = false;
    bool _image_view_open                = false;
    bool _image_view_drawn               = false;
    bool _image_overlay_visible          = false;
    bool _image_clean_cache_valid        = false;

    void updateCardState(bool force_draw);
    void drawSummary();
    void openBrowser();
    void closeBrowser();
    void openImageViewer(const std::string& path);
    void closeImageViewer();
    bool ensureCardPresentForBrowser();
    void handleCardStatusSound(TfCardStatus status);
    bool loadDirectory(const std::string& path);
    void drawBrowser();
    void handleBrowserTouch();
    void drawImageViewer(bool image_refresh);
    void handleImageViewerTouch();
    void clearImageCleanCache();
    void captureImageCleanCache();
    bool restoreImageCleanCache();
    void drawImageOverlay();
    bool switchImage(int direction);
    bool goParent();
};

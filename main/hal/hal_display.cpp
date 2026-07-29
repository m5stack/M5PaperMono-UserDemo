/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <algorithm>
#include <assets.h>

static constexpr std::uint8_t LOCAL_REFRESH_FAST_REFRESH_THRESHOLD   = 10;
static constexpr std::uint8_t FAST_REFRESH_QUALITY_REFRESH_THRESHOLD = 5;

/* -------------------------------------------------------------------------- */
/*                                   Display                                  */
/* -------------------------------------------------------------------------- */
bool Hal::drawBackground(m5gfx::epd_mode_t mode, bool flush)
{
    display.setEpdMode(mode);
    display.fillScreen(TFT_WHITE);

    const std::uint8_t* bg_start = isLiteModel() ? img_bg_lite_start : img_bg_start;
    const std::uint8_t* bg_end   = isLiteModel() ? img_bg_lite_end : img_bg_end;
    const bool ok                = display.drawPng(bg_start, bg_end - bg_start, 0, 0);
    if (!ok) {
        return false;
    }

    if (flush) {
        flushDisplay();
        display.waitDisplay();
    }
    return true;
}

bool Hal::captureDisplaySnapshot()
{
    const int screen_w = display.width();
    const int screen_h = display.height();
    if (screen_w <= 0 || screen_h <= 0) {
        clearDisplaySnapshot();
        return false;
    }

    const std::size_t buffer_size = static_cast<std::size_t>(screen_w) * static_cast<std::size_t>(screen_h);
    _display_snapshot.resize(buffer_size);
    display.readRect(0, 0, screen_w, screen_h, _display_snapshot.data());

    // Stable representatives reproduce the same SSD1677 2-bit levels when pushed through its Bayer conversion.
    static constexpr std::uint8_t CACHE_GRAY_LEVELS[] = {0, 96, 160, 255};
    for (auto& pixel : _display_snapshot) {
        const std::size_t level = std::min<std::size_t>((static_cast<std::size_t>(pixel.raw) + 42) / 85, 3);
        pixel.raw               = CACHE_GRAY_LEVELS[level];
    }

    _display_snapshot_w     = static_cast<std::int16_t>(screen_w);
    _display_snapshot_h     = static_cast<std::int16_t>(screen_h);
    _display_snapshot_valid = true;
    return true;
}

bool Hal::restoreDisplaySnapshot()
{
    if (!_display_snapshot_valid || _display_snapshot.empty() || _display_snapshot_w != display.width() ||
        _display_snapshot_h != display.height()) {
        return false;
    }

    display.pushImage(0, 0, _display_snapshot_w, _display_snapshot_h, _display_snapshot.data());
    return true;
}

void Hal::clearDisplaySnapshot()
{
    _display_snapshot.clear();
    _display_snapshot_w     = 0;
    _display_snapshot_h     = 0;
    _display_snapshot_valid = false;
}

void Hal::flushDisplayFastOnce()
{
    const auto saved_mode = display.getEpdMode();
    ++_fast_refresh_count;
    const auto refresh_mode = _fast_refresh_count >= FAST_REFRESH_QUALITY_REFRESH_THRESHOLD
                                  ? m5gfx::epd_mode_t::epd_quality
                                  : m5gfx::epd_mode_t::epd_fast;
    if (refresh_mode == m5gfx::epd_mode_t::epd_quality) {
        _fast_refresh_count = 0;
    }

    display.setEpdMode(refresh_mode);
    display.display();
    if (refresh_mode != saved_mode) {
        display.waitDisplay();
        display.setEpdMode(saved_mode);
    }
}

void Hal::flushDisplay()
{
    const auto mode = display.getEpdMode();
    if (mode == m5gfx::epd_mode_t::epd_fastest) {
        ++_local_refresh_count;
        if (_local_refresh_count >= LOCAL_REFRESH_FAST_REFRESH_THRESHOLD) {
            _local_refresh_count = 0;
            flushDisplayFastOnce();
            return;
        }
    } else if (mode == m5gfx::epd_mode_t::epd_fast) {
        _local_refresh_count = 0;
        flushDisplayFastOnce();
        return;
    } else {
        _local_refresh_count = 0;
        _fast_refresh_count  = 0;
    }

    display.display();
}

void Hal::flushDisplay(int32_t x, int32_t y, int32_t w, int32_t h)
{
    const auto mode = display.getEpdMode();
    if (mode == m5gfx::epd_mode_t::epd_fastest) {
        ++_local_refresh_count;
        if (_local_refresh_count >= LOCAL_REFRESH_FAST_REFRESH_THRESHOLD) {
            _local_refresh_count = 0;
            flushDisplayFastOnce();
            return;
        }
    } else if (mode == m5gfx::epd_mode_t::epd_fast) {
        _local_refresh_count = 0;
        flushDisplayFastOnce();
        return;
    } else {
        _local_refresh_count = 0;
        _fast_refresh_count  = 0;
    }

    display.display(x, y, w, h);
}

void Hal::requestRefresh()
{
    _refresh_request = true;
}

void Hal::requestFullRedraw(m5gfx::epd_mode_t mode)
{
    _full_redraw_request = true;
    _full_redraw_mode    = mode;
    requestRefresh();
}

void Hal::requestRefreshNextFrame(m5gfx::epd_mode_t mode)
{
    _refresh_next_frame     = true;
    _next_frame_redraw_mode = mode;
}

bool Hal::registerDisplayRefreshObserver(DisplayRefreshObserver* observer)
{
    if (!observer) {
        return false;
    }

    for (std::size_t i = 0; i < _display_refresh_observer_count; ++i) {
        if (_display_refresh_observers[i] == observer) {
            return true;
        }
    }

    if (_display_refresh_observer_count >= DISPLAY_REFRESH_OBSERVER_MAX) {
        return false;
    }

    _display_refresh_observers[_display_refresh_observer_count++] = observer;
    return true;
}

void Hal::prepareDisplayRefreshObservers(DisplayRefreshObserver* exclude)
{
    if (_preparing_display_refresh_observers) {
        return;
    }

    _preparing_display_refresh_observers = true;
    for (std::size_t i = 0; i < _display_refresh_observer_count; ++i) {
        auto* observer = _display_refresh_observers[i];
        if (observer && observer != exclude) {
            observer->prepareForDisplayRefresh();
        }
    }
    _preparing_display_refresh_observers = false;
}

bool Hal::isPreparingDisplayRefreshObservers() const
{
    return _preparing_display_refresh_observers;
}

void Hal::releaseModalNextFrame(m5gfx::epd_mode_t mode)
{
    _release_modal_next_frame  = true;
    _release_modal_redraw_mode = mode;
}

void Hal::beginFrame()
{
    if (_release_modal_next_frame) {
        _release_modal_next_frame = false;
        _ui_modal_active          = false;
        requestFullRedraw(_release_modal_redraw_mode);
    }
    if (_refresh_next_frame) {
        _refresh_next_frame = false;
        requestFullRedraw(_next_frame_redraw_mode);
    }
}

bool Hal::isRefreshRequested() const
{
    return _refresh_request;
}

bool Hal::isFullRedrawRequested() const
{
    return _full_redraw_request;
}

m5gfx::epd_mode_t Hal::fullRedrawMode() const
{
    return _full_redraw_mode;
}

void Hal::clearRefreshRequest()
{
    _refresh_request     = false;
    _full_redraw_request = false;
}

void Hal::setUiModalActive(bool active)
{
    _ui_modal_active = active;
}

bool Hal::isUiModalActive() const
{
    return _ui_modal_active;
}

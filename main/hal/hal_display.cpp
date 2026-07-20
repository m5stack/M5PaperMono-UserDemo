/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"

#include <assets.h>

static constexpr std::uint8_t FAST_REFRESH_FULL_REFRESH_THRESHOLD = 30;

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

void Hal::flushDisplayQualityOnce()
{
    const auto saved_mode = display.getEpdMode();
    display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
    display.display();
    display.waitDisplay();
    display.setEpdMode(saved_mode);
}

void Hal::flushDisplay()
{
    const auto mode = display.getEpdMode();
    if (mode == m5gfx::epd_mode_t::epd_fast || mode == m5gfx::epd_mode_t::epd_fastest) {
        ++_fast_refresh_count;
        if (_fast_refresh_count >= FAST_REFRESH_FULL_REFRESH_THRESHOLD) {
            _fast_refresh_count = 0;
            flushDisplayQualityOnce();
        }
    } else {
        _fast_refresh_count = 0;
    }

    display.display();
}

void Hal::flushDisplay(int32_t x, int32_t y, int32_t w, int32_t h)
{
    const auto mode = display.getEpdMode();
    if (mode == m5gfx::epd_mode_t::epd_fast || mode == m5gfx::epd_mode_t::epd_fastest) {
        ++_fast_refresh_count;
        if (_fast_refresh_count >= FAST_REFRESH_FULL_REFRESH_THRESHOLD) {
            _fast_refresh_count = 0;
            flushDisplayQualityOnce();
        }
    } else {
        _fast_refresh_count = 0;
    }

    display.display(x, y, w, h);
}

void Hal::requestRefresh()
{
    _refresh_request = true;
}

void Hal::requestRefreshNextFrame()
{
    _refresh_next_frame = true;
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

void Hal::releaseModalNextFrame()
{
    _release_modal_next_frame = true;
}

void Hal::beginFrame()
{
    if (_release_modal_next_frame) {
        _release_modal_next_frame = false;
        _ui_modal_active          = false;
        requestRefresh();
    }
    if (_refresh_next_frame) {
        _refresh_next_frame = false;
        requestRefresh();
    }
}

bool Hal::isRefreshRequested() const
{
    return _refresh_request;
}

void Hal::clearRefreshRequest()
{
    _refresh_request = false;
}

void Hal::setUiModalActive(bool active)
{
    _ui_modal_active = active;
}

bool Hal::isUiModalActive() const
{
    return _ui_modal_active;
}

/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_keyboard.h"

#include <M5Unified.h>
#include <algorithm>
#include <hal.h>

static constexpr int KEY_GAP                             = 5;
static constexpr int KEY_H                               = 52;
static constexpr int PASSWORD_TOGGLE_W                   = 78;
static constexpr int PASSWORD_TOGGLE_H                   = 28;
static constexpr std::uint32_t DELETE_REPEAT_DELAY_MS    = 1200;
static constexpr std::uint32_t DELETE_REPEAT_INTERVAL_MS = 110;

static constexpr BuzzerTone KEYBOARD_KEY_TONES[] = {
    {.start_freq_hz = 1850, .end_freq_hz = 1850, .duration_ms = 5, .gap_ms = 0},
};

static constexpr BuzzerTone KEYBOARD_BACKSPACE_TONES[] = {
    {.start_freq_hz = 1150, .end_freq_hz = 1150, .duration_ms = 7, .gap_ms = 0},
};

static constexpr BuzzerTone KEYBOARD_MODE_TONES[] = {
    {.start_freq_hz = 980, .end_freq_hz = 980, .duration_ms = 6, .gap_ms = 4},
    {.start_freq_hz = 1450, .end_freq_hz = 1450, .duration_ms = 8, .gap_ms = 0},
};

static constexpr BuzzerTone KEYBOARD_CLOSE_TONES[] = {
    {.start_freq_hz = 1350, .end_freq_hz = 1350, .duration_ms = 6, .gap_ms = 4},
    {.start_freq_hz = 820, .end_freq_hz = 820, .duration_ms = 12, .gap_ms = 0},
};

namespace {

bool pointInRect(int px, int py, int x, int y, int w, int h)
{
    return px >= x && px <= x + w && py >= y && py <= y + h;
}

std::string fitKeyboardText(std::string text, int max_width)
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

std::string maskedText(std::size_t len)
{
    return std::string(len, '*');
}

int panelTop()
{
    return std::max(0, static_cast<int>(GetHAL().display.height()) - 330);
}

void drawKey(const char* label, int x, int y, int w, bool active = false)
{
    auto& display = GetHAL().display;
    if (active) {
        display.fillRoundRect(x, y, w, KEY_H, 5, TFT_BLACK);
        display.setTextColor(TFT_WHITE, TFT_BLACK);
    } else {
        display.fillRoundRect(x, y, w, KEY_H, 5, TFT_WHITE);
        display.drawRoundRect(x, y, w, KEY_H, 5, TFT_BLACK);
        display.setTextColor(TFT_BLACK, TFT_WHITE);
    }
    display.setTextDatum(middle_center);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.setTextSize(1.0f);
    display.drawString(label, x + w / 2, y + KEY_H / 2 + 1);
}

void drawSmallKey(const char* label, int x, int y, int w, int h, bool active = false)
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
    display.drawString(label, x + w / 2, y + h / 2 + 1);
}

}  // namespace

void ScreenKeyboard::open(const char* title, const std::string& text, bool password, std::size_t max_len)
{
    _title            = title ? title : "";
    _text             = text;
    _password         = password;
    _password_visible = false;
    _max_len          = max_len;
    _shift            = false;
    _symbols          = false;
    stopDeleteRepeat();
    _open = true;
    draw();
}

void ScreenKeyboard::close()
{
    stopDeleteRepeat();
    _open = false;
}

bool ScreenKeyboard::isOpen() const
{
    return _open;
}

const std::string& ScreenKeyboard::text() const
{
    return _text;
}

bool ScreenKeyboard::appendChar(char c)
{
    if (_text.size() >= _max_len) {
        return false;
    }
    _text.push_back(c);
    return true;
}

bool ScreenKeyboard::backspace()
{
    if (_text.empty()) {
        return false;
    }
    _text.pop_back();
    return true;
}

void ScreenKeyboard::stopDeleteRepeat()
{
    _delete_held           = false;
    _delete_press_ms       = 0;
    _next_delete_repeat_ms = 0;
}

KeyboardEvent ScreenKeyboard::update(bool immediate_refresh)
{
    if (!_open) {
        return KeyboardEvent::None;
    }

    const auto& touch = GetHAL().getTouchDetail();
    if (!GetHAL().isTouchPressed()) {
        stopDeleteRepeat();
        return KeyboardEvent::None;
    }

    const auto& point = M5.Touch.getTouchPointRaw();
    if (_delete_held) {
        if (!isDeleteKey(point.x, point.y)) {
            stopDeleteRepeat();
            return KeyboardEvent::None;
        }
        const auto now_ms = GetHAL().millis();
        if (now_ms >= _next_delete_repeat_ms) {
            if (backspace()) {
                _next_delete_repeat_ms = now_ms + DELETE_REPEAT_INTERVAL_MS;
                GetHAL().playBuzzerPattern(KEYBOARD_BACKSPACE_TONES, 1);
                draw(immediate_refresh);
                return KeyboardEvent::Changed;
            }
            stopDeleteRepeat();
        }
        return KeyboardEvent::None;
    }

    if (!touch.wasPressed()) {
        return KeyboardEvent::None;
    }

    if (!handleTouch(point.x, point.y)) {
        return KeyboardEvent::None;
    }

    if (!_open) {
        return KeyboardEvent::Closed;
    }
    draw(immediate_refresh);
    if (_delete_held) {
        _delete_press_ms       = GetHAL().millis();
        _next_delete_repeat_ms = _delete_press_ms + DELETE_REPEAT_DELAY_MS;
    }
    return KeyboardEvent::Changed;
}

bool ScreenKeyboard::isDeleteKey(int x, int y) const
{
    const int screen_w = GetHAL().display.width();
    const int top      = panelTop();
    const int row0_y   = top + 76;
    const int row1_y   = row0_y + KEY_H + KEY_GAP;
    const int row2_y   = row1_y + KEY_H + KEY_GAP;
    const int margin   = 16;
    const int key_w    = (screen_w - margin * 2 - KEY_GAP * 9) / 10;
    const int key_wide = 64;
    const int row2_x   = margin + 8;
    const int back_x   = row2_x + key_wide + KEY_GAP + 7 * (key_w + KEY_GAP);
    return pointInRect(x, y, back_x, row2_y, key_wide, KEY_H);
}

bool ScreenKeyboard::handleTouch(int x, int y)
{
    const int screen_w = GetHAL().display.width();
    const int top      = panelTop();
    const int row0_y   = top + 76;
    const int row1_y   = row0_y + KEY_H + KEY_GAP;
    const int row2_y   = row1_y + KEY_H + KEY_GAP;
    const int row3_y   = row2_y + KEY_H + KEY_GAP;
    const int margin   = 16;
    const int key_w    = (screen_w - margin * 2 - KEY_GAP * 9) / 10;

    if (_password &&
        pointInRect(x, y, screen_w - margin - PASSWORD_TOGGLE_W, top + 7, PASSWORD_TOGGLE_W, PASSWORD_TOGGLE_H)) {
        _password_visible = !_password_visible;
        GetHAL().playBuzzerPattern(KEYBOARD_MODE_TONES, 2);
        return true;
    }

    auto tryCharRow = [&](const char* chars, int count, int start_x, int key_y) -> bool {
        for (int i = 0; i < count; ++i) {
            const int key_x = start_x + i * (key_w + KEY_GAP);
            if (pointInRect(x, y, key_x, key_y, key_w, KEY_H)) {
                if (appendChar(chars[i])) {
                    GetHAL().playBuzzerPattern(KEYBOARD_KEY_TONES, 1);
                }
                return true;
            }
        }
        return false;
    };

    if (!_symbols) {
        const char* row0 = _shift ? "QWERTYUIOP" : "qwertyuiop";
        const char* row1 = _shift ? "ASDFGHJKL" : "asdfghjkl";
        const char* row2 = _shift ? "ZXCVBNM" : "zxcvbnm";
        if (tryCharRow(row0, 10, margin, row0_y)) {
            return true;
        }
        if (tryCharRow(row1, 9, margin + key_w / 2, row1_y)) {
            return true;
        }
        const int shift_w = 64;
        const int row2_x  = margin + 8;
        if (pointInRect(x, y, row2_x, row2_y, shift_w, KEY_H)) {
            _shift = !_shift;
            GetHAL().playBuzzerPattern(KEYBOARD_MODE_TONES, 2);
            return true;
        }
        if (tryCharRow(row2, 7, row2_x + shift_w + KEY_GAP, row2_y)) {
            return true;
        }
        const int back_x = row2_x + shift_w + KEY_GAP + 7 * (key_w + KEY_GAP);
        if (pointInRect(x, y, back_x, row2_y, shift_w, KEY_H)) {
            if (backspace()) {
                GetHAL().playBuzzerPattern(KEYBOARD_BACKSPACE_TONES, 1);
            }
            _delete_held           = true;
            _delete_press_ms       = GetHAL().millis();
            _next_delete_repeat_ms = _delete_press_ms + DELETE_REPEAT_DELAY_MS;
            return true;
        }
    } else {
        static constexpr const char* row0 = "1234567890";
        static constexpr const char* row1 = "@#$&*()'\"";
        static constexpr const char* row2 = "-/:;!?+";
        if (tryCharRow(row0, 10, margin, row0_y)) {
            return true;
        }
        if (tryCharRow(row1, 9, margin + key_w / 2, row1_y)) {
            return true;
        }
        const int more_w = 64;
        const int row2_x = margin + 8;
        if (pointInRect(x, y, row2_x, row2_y, more_w, KEY_H)) {
            _symbols = false;
            GetHAL().playBuzzerPattern(KEYBOARD_MODE_TONES, 2);
            return true;
        }
        if (tryCharRow(row2, 7, row2_x + more_w + KEY_GAP, row2_y)) {
            return true;
        }
        const int back_x = row2_x + more_w + KEY_GAP + 7 * (key_w + KEY_GAP);
        if (pointInRect(x, y, back_x, row2_y, more_w, KEY_H)) {
            if (backspace()) {
                GetHAL().playBuzzerPattern(KEYBOARD_BACKSPACE_TONES, 1);
            }
            _delete_held           = true;
            _delete_press_ms       = GetHAL().millis();
            _next_delete_repeat_ms = _delete_press_ms + DELETE_REPEAT_DELAY_MS;
            return true;
        }
    }

    const int mode_w  = 74;
    const int small_w = 46;
    const int down_w  = 74;
    const int row3_x  = 15;
    const int comma_x = row3_x + mode_w + KEY_GAP;
    const int space_x = comma_x + small_w + KEY_GAP;
    const int space_w = screen_w - space_x - KEY_GAP - small_w - KEY_GAP - down_w - row3_x;
    const int dot_x   = space_x + space_w + KEY_GAP;
    const int down_x  = dot_x + small_w + KEY_GAP;

    if (pointInRect(x, y, row3_x, row3_y, mode_w, KEY_H)) {
        _symbols = !_symbols;
        GetHAL().playBuzzerPattern(KEYBOARD_MODE_TONES, 2);
        return true;
    }
    if (pointInRect(x, y, comma_x, row3_y, small_w, KEY_H)) {
        if (appendChar(_symbols ? '_' : ',')) {
            GetHAL().playBuzzerPattern(KEYBOARD_KEY_TONES, 1);
        }
        return true;
    }
    if (pointInRect(x, y, space_x, row3_y, space_w, KEY_H)) {
        if (appendChar(' ')) {
            GetHAL().playBuzzerPattern(KEYBOARD_KEY_TONES, 1);
        }
        return true;
    }
    if (pointInRect(x, y, dot_x, row3_y, small_w, KEY_H)) {
        if (appendChar(_symbols ? '=' : '.')) {
            GetHAL().playBuzzerPattern(KEYBOARD_KEY_TONES, 1);
        }
        return true;
    }
    if (pointInRect(x, y, down_x, row3_y, down_w, KEY_H)) {
        GetHAL().playBuzzerPattern(KEYBOARD_CLOSE_TONES, 2);
        close();
        return true;
    }

    return false;
}

int ScreenKeyboard::refreshTop() const
{
    return panelTop();
}

bool ScreenKeyboard::containsPoint(int x, int y) const
{
    return _open && x >= 0 && x < GetHAL().display.width() && y >= panelTop() && y < GetHAL().display.height();
}

void ScreenKeyboard::draw(bool immediate_refresh)
{
    if (!_open) {
        return;
    }

    auto& display      = GetHAL().display;
    const int screen_w = display.width();
    const int screen_h = display.height();
    const int top      = panelTop();
    const int margin   = 16;
    const int key_w    = (screen_w - margin * 2 - KEY_GAP * 9) / 10;
    const int row0_y   = top + 76;
    const int row1_y   = row0_y + KEY_H + KEY_GAP;
    const int row2_y   = row1_y + KEY_H + KEY_GAP;
    const int row3_y   = row2_y + KEY_H + KEY_GAP;

    display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
    display.fillRect(0, top, screen_w, screen_h - top, TFT_WHITE);
    display.drawFastHLine(0, top, screen_w, TFT_BLACK);

    display.setTextColor(TFT_BLACK, TFT_WHITE);
    display.setTextDatum(middle_left);
    display.setFont(&fonts::FreeSansBold9pt7b);
    display.drawString(_title.c_str(), margin, top + 20);
    if (_password) {
        drawSmallKey(_password_visible ? "HIDE" : "SHOW", screen_w - margin - PASSWORD_TOGGLE_W, top + 7,
                     PASSWORD_TOGGLE_W, PASSWORD_TOGGLE_H, _password_visible);
    }

    display.drawRoundRect(margin, top + 36, screen_w - margin * 2, 34, 5, TFT_BLACK);
    display.setTextDatum(middle_left);
    const std::string shown = _password && !_password_visible ? maskedText(_text.size()) : _text;
    display.drawString(fitKeyboardText(shown, screen_w - margin * 2 - 18).c_str(), margin + 9, top + 53);

    auto drawCharRow = [&](const char* chars, int count, int start_x, int key_y) {
        for (int i = 0; i < count; ++i) {
            char label[2] = {chars[i], '\0'};
            drawKey(label, start_x + i * (key_w + KEY_GAP), key_y, key_w);
        }
    };

    if (!_symbols) {
        drawCharRow(_shift ? "QWERTYUIOP" : "qwertyuiop", 10, margin, row0_y);
        drawCharRow(_shift ? "ASDFGHJKL" : "asdfghjkl", 9, margin + key_w / 2, row1_y);
        const int shift_w = 64;
        const int row2_x  = margin + 8;
        drawKey("Aa", row2_x, row2_y, shift_w, _shift);
        drawCharRow(_shift ? "ZXCVBNM" : "zxcvbnm", 7, row2_x + shift_w + KEY_GAP, row2_y);
        drawKey("DEL", row2_x + shift_w + KEY_GAP + 7 * (key_w + KEY_GAP), row2_y, shift_w);
    } else {
        drawCharRow("1234567890", 10, margin, row0_y);
        drawCharRow("@#$&*()'\"", 9, margin + key_w / 2, row1_y);
        const int more_w = 64;
        const int row2_x = margin + 8;
        drawKey("ABC", row2_x, row2_y, more_w, true);
        drawCharRow("-/:;!?+", 7, row2_x + more_w + KEY_GAP, row2_y);
        drawKey("DEL", row2_x + more_w + KEY_GAP + 7 * (key_w + KEY_GAP), row2_y, more_w);
    }

    const int mode_w  = 74;
    const int small_w = 46;
    const int down_w  = 74;
    const int row3_x  = 15;
    const int comma_x = row3_x + mode_w + KEY_GAP;
    const int space_x = comma_x + small_w + KEY_GAP;
    const int space_w = screen_w - space_x - KEY_GAP - small_w - KEY_GAP - down_w - row3_x;
    const int dot_x   = space_x + space_w + KEY_GAP;
    const int down_x  = dot_x + small_w + KEY_GAP;
    drawKey(_symbols ? "ABC" : "123", row3_x, row3_y, mode_w);
    drawKey(_symbols ? "_" : ",", comma_x, row3_y, small_w);
    drawKey("space", space_x, row3_y, space_w);
    drawKey(_symbols ? "=" : ".", dot_x, row3_y, small_w);
    drawKey("v", down_x, row3_y, down_w);

    if (immediate_refresh) {
        GetHAL().flushDisplay(0, top, screen_w, screen_h - top);
        display.waitDisplay();
    }
}

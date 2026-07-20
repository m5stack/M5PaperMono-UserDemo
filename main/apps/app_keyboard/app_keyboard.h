/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

enum class KeyboardEvent : std::uint8_t {
    None,
    Changed,
    Closed,
};

class ScreenKeyboard {
public:
    void open(const char* title, const std::string& text, bool password, std::size_t max_len);
    void close();
    bool isOpen() const;
    KeyboardEvent update(bool immediate_refresh = true);
    void draw(bool immediate_refresh = true);
    const std::string& text() const;
    int refreshTop() const;
    bool containsPoint(int x, int y) const;

private:
    std::string _title;
    std::string _text;
    std::size_t _max_len                 = 64;
    std::uint32_t _delete_press_ms       = 0;
    std::uint32_t _next_delete_repeat_ms = 0;
    bool _open                           = false;
    bool _password                       = false;
    bool _password_visible               = false;
    bool _shift                          = false;
    bool _symbols                        = false;
    bool _delete_held                    = false;

    bool appendChar(char c);
    bool backspace();
    bool isDeleteKey(int x, int y) const;
    void stopDeleteRepeat();
    bool handleTouch(int x, int y);
};

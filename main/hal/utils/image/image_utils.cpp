/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "image_utils.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>

static bool get_bmp_size_mem(const std::uint8_t* data, std::size_t len, int* width, int* height)
{
    if (len < 26 || data[0] != 'B' || data[1] != 'M') {
        return false;
    }

    const std::uint32_t width_u = static_cast<std::uint32_t>(data[18]) | (static_cast<std::uint32_t>(data[19]) << 8) |
                                  (static_cast<std::uint32_t>(data[20]) << 16) |
                                  (static_cast<std::uint32_t>(data[21]) << 24);
    const std::uint32_t height_u = static_cast<std::uint32_t>(data[22]) | (static_cast<std::uint32_t>(data[23]) << 8) |
                                   (static_cast<std::uint32_t>(data[24]) << 16) |
                                   (static_cast<std::uint32_t>(data[25]) << 24);
    *width  = static_cast<std::int32_t>(width_u);
    *height = static_cast<std::int32_t>(height_u);
    if (*height < 0) {
        *height = -*height;
    }
    return *width > 0 && *height > 0;
}

static bool get_png_size_mem(const std::uint8_t* data, std::size_t len, int* width, int* height)
{
    if (len < 24) {
        return false;
    }

    const std::uint8_t png_sig[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    if (std::memcmp(data, png_sig, sizeof(png_sig)) != 0) {
        return false;
    }

    *width =
        static_cast<int>((static_cast<std::uint32_t>(data[16]) << 24) | (static_cast<std::uint32_t>(data[17]) << 16) |
                         (static_cast<std::uint32_t>(data[18]) << 8) | static_cast<std::uint32_t>(data[19]));
    *height =
        static_cast<int>((static_cast<std::uint32_t>(data[20]) << 24) | (static_cast<std::uint32_t>(data[21]) << 16) |
                         (static_cast<std::uint32_t>(data[22]) << 8) | static_cast<std::uint32_t>(data[23]));
    return *width > 0 && *height > 0;
}

static bool get_jpg_size_mem(const std::uint8_t* data, std::size_t len, int* width, int* height)
{
    if (len < 2 || data[0] != 0xFF || data[1] != 0xD8) {
        return false;
    }

    std::size_t pos = 2;
    while (pos < len) {
        while (pos < len && data[pos] != 0xFF) {
            ++pos;
        }
        if (pos >= len) {
            break;
        }
        ++pos;

        while (pos < len && data[pos] == 0xFF) {
            ++pos;
        }
        if (pos >= len) {
            break;
        }

        const std::uint8_t marker = data[pos++];
        if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            if (pos + 7 > len) {
                break;
            }
            *height = (static_cast<int>(data[pos + 3]) << 8) | data[pos + 4];
            *width  = (static_cast<int>(data[pos + 5]) << 8) | data[pos + 6];
            return *width > 0 && *height > 0;
        }

        if (pos + 2 > len) {
            break;
        }
        const std::uint16_t seg_len = (static_cast<std::uint16_t>(data[pos]) << 8) | data[pos + 1];
        if (seg_len < 2) {
            break;
        }
        pos += seg_len;
    }
    return false;
}

bool get_image_size_from_memory(const std::uint8_t* data, std::size_t len, int* width, int* height, const char* ext)
{
    if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
        return get_jpg_size_mem(data, len, width, height);
    }
    if (strcasecmp(ext, ".bmp") == 0) {
        return get_bmp_size_mem(data, len, width, height);
    }
    if (strcasecmp(ext, ".png") == 0) {
        return get_png_size_mem(data, len, width, height);
    }
    return false;
}

bool get_image_size_from_file(const char* path, int* width, int* height)
{
    const char* dot = std::strrchr(path, '.');
    if (!dot) {
        return false;
    }

    FILE* f = std::fopen(path, "rb");
    if (!f) {
        return false;
    }

    constexpr std::size_t BUF_SIZE = 65536;
    auto* buf                      = static_cast<std::uint8_t*>(std::malloc(BUF_SIZE));
    if (!buf) {
        std::fclose(f);
        return false;
    }

    const std::size_t n = std::fread(buf, 1, BUF_SIZE, f);
    std::fclose(f);

    bool ok = false;
    if (n > 0) {
        ok = get_image_size_from_memory(buf, n, width, height, dot);
    }
    std::free(buf);
    return ok;
}

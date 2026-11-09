/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screenshot.h"
#include "../../../common/screenshot.h"

static int save_pixels(const uint32_t *left, const uint32_t *right,
                       int left_width, int right_width, int height,
                       const char *mode, const char *role, char *out, size_t capacity)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, left_width + right_width, height, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return -1;
    for (int y = 0; y < height; y++) {
        unsigned char *row = (unsigned char *)surface->pixels + y * surface->pitch;
        memcpy(row, left + y * left_width, left_width * sizeof(uint32_t));
        if (right) {
            memcpy(row + left_width * sizeof(uint32_t),
                   right + y * right_width,
                   right_width * sizeof(uint32_t));
        }
    }
    int result = integral_screenshot_save_surface(surface, mode, role, out, capacity);
    SDL_FreeSurface(surface);
    return result;
}

int integral_gb_runtime_screenshot_save_pair(const IntegralGBRuntimeSlot *a,
    const IntegralGBRuntimeSlot *b, const char *mode, const char *role,
    char *out, size_t capacity)
{
    if (!a || !a->initialized || (b && !b->initialized)) return -1;
    const unsigned height = integral_gb_runtime_slot_screen_height(a);
    if (b && integral_gb_runtime_slot_screen_height(b) != height) return -1;
    return save_pixels(integral_gb_runtime_slot_presented_pixels(a),
                       b ? integral_gb_runtime_slot_presented_pixels(b) : NULL,
                       (int)integral_gb_runtime_slot_screen_width(a),
                       b ? (int)integral_gb_runtime_slot_screen_width(b) : 0,
                       (int)height, mode, role, out, capacity);
}

int integral_gb_runtime_screenshot_save_link_pixels(const uint32_t *slot1,
    const uint32_t *slot2, char *out, size_t capacity)
{
    return save_pixels(slot1, slot2, INTEGRAL_GB_RUNTIME_GB_WIDTH, INTEGRAL_GB_RUNTIME_GB_WIDTH,
                       INTEGRAL_GB_RUNTIME_GB_HEIGHT, "local_gb_server2", "local", out, capacity);
}

int integral_gb_runtime_screenshot_save_slot(const IntegralGBRuntimeSlot *slot,
                                            char *out, size_t capacity)
{
    return integral_gb_runtime_screenshot_save_pair(slot, NULL, "local_gb", "local", out, capacity);
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screenshot.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "file_util.h"
#include "net_compat.h"

static int ensure_screenshot_directory(void)
{
    return integral_gb_runtime_ensure_directory("screenshot", 0755);
}

static int build_screenshot_path(char *out, size_t out_size)
{
    static unsigned counter;
    time_t now = time(NULL);
    struct tm tm_now;
    if (!integral_gb_runtime_localtime(now, &tm_now)) {
        return -1;
    }

    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        int n = snprintf(out,
                         out_size,
                         "screenshot/gb_runtime_%04d%02d%02d_%02d%02d%02d_%u_%03u.bmp",
                         tm_now.tm_year + 1900,
                         tm_now.tm_mon + 1,
                         tm_now.tm_mday,
                         tm_now.tm_hour,
                         tm_now.tm_min,
                         tm_now.tm_sec,
                         (unsigned)integral_gb_runtime_getpid(),
                         counter++);
        if (n < 0 || (size_t)n >= out_size) {
            return -1;
        }
        if (!integral_gb_runtime_file_exists_regular(out)) {
            return 0;
        }
    }
    return -1;
}

int integral_gb_runtime_screenshot_save_slot(const IntegralGBRuntimeSlot *slot, char *out_path, size_t out_path_size)
{
    if (!slot || !slot->initialized || !out_path || out_path_size == 0) {
        return -1;
    }
    if (ensure_screenshot_directory() != 0 ||
        build_screenshot_path(out_path, out_path_size) != 0) {
        return -1;
    }

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
                                                              (void *)integral_gb_runtime_slot_presented_pixels(slot),
                                                              INTEGRAL_GB_RUNTIME_GB_WIDTH,
                                                              INTEGRAL_GB_RUNTIME_GB_HEIGHT,
                                                              32,
                                                              INTEGRAL_GB_RUNTIME_GB_WIDTH * (int)sizeof(uint32_t),
                                                              SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        return -1;
    }

    int result = SDL_SaveBMP(surface, out_path);
    SDL_FreeSurface(surface);
    return result == 0 ? 0 : -1;
}

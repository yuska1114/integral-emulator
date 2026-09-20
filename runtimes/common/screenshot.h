/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_SCREENSHOT_H
#define INTEGRAL_SCREENSHOT_H
#include <SDL.h>
#include "screenshot_path.h"

static inline int integral_screenshot_save_surface(SDL_Surface *surface,
                                                   const char *mode, const char *role,
                                                   char *path, size_t capacity)
{
    if (!surface || integral_screenshot_path("screenshot", mode, role, "bmp", path, capacity)) return -1;
    return SDL_SaveBMP(surface, path);
}
#endif

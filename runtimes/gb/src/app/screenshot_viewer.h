/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_APP_SCREENSHOT_VIEWER_H
#define INTEGRAL_GB_RUNTIME_APP_SCREENSHOT_VIEWER_H

#include <SDL.h>

#include "key_config.h"

void integral_gb_runtime_screenshot_viewer_run(SDL_Renderer *renderer,
                                     SDL_Window *window,
                                     const IntegralGBRuntimeKeyConfig *menu_keys,
                                     SDL_Keycode escape_key);

#endif

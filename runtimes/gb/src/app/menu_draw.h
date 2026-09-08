/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_APP_MENU_DRAW_H
#define INTEGRAL_GB_RUNTIME_APP_MENU_DRAW_H

#include <SDL.h>

#include "menu_state.h"

enum {
    INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH = 480,
    INTEGRAL_GB_RUNTIME_APP_WINDOW_HEIGHT = 480,
};

void integral_gb_runtime_app_draw_text(SDL_Renderer *renderer,
                             int x,
                             int y,
                             const char *text,
                             int scale,
                             SDL_Color color);
void integral_gb_runtime_app_draw_text_fit(SDL_Renderer *renderer,
                                 int x,
                                 int y,
                                 const char *text,
                                 int scale,
                                 SDL_Color color,
                                 int max_width);
void integral_gb_runtime_menu_draw_key_config_menu(SDL_Renderer *renderer,
                                         const IntegralGBRuntimeMenu *menu,
                                         unsigned selected_row);
void integral_gb_runtime_menu_draw(SDL_Renderer *renderer, const IntegralGBRuntimeMenu *menu);

#endif

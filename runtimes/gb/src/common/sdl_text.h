/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_COMMON_SDL_TEXT_H
#define INTEGRAL_GB_RUNTIME_COMMON_SDL_TEXT_H

#include <SDL.h>

void integral_gb_runtime_sdl_draw_text(SDL_Renderer *renderer,
                             int x,
                             int y,
                             const char *text,
                             int scale,
                             SDL_Color color);
void integral_gb_runtime_sdl_draw_text_fit(SDL_Renderer *renderer,
                                 int x,
                                 int y,
                                 const char *text,
                                 int scale,
                                 SDL_Color color,
                                 int max_width);

#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_LEAGUE_SDL_UNICODE_TEXT_H
#define INTEGRAL_LEAGUE_SDL_UNICODE_TEXT_H

#include <SDL.h>

void integral_sdl_draw_utf8_text(SDL_Renderer *renderer,
                            int x,
                            int y,
                            const char *text,
                            int font_size,
                            SDL_Color color,
                            int max_width);

#endif

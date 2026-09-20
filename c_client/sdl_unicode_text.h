/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_LEAGUE_SDL_UNICODE_TEXT_H
#define INTEGRAL_LEAGUE_SDL_UNICODE_TEXT_H

#include <SDL.h>
#include <stdbool.h>

/* Scroll a rendered UTF-8 line, never slicing its encoded bytes. */
int integral_text_scroll_offset(int text_width, int view_width, Uint32 ticks, bool tail);
void integral_sdl_draw_utf8_scrolled(SDL_Renderer *, int, int, const char *, int,
                                    SDL_Color, int, Uint32 ticks, bool tail);

void integral_sdl_draw_utf8_text(SDL_Renderer *renderer,
                            int x,
                            int y,
                            const char *text,
                            int font_size,
                            SDL_Color color,
                            int max_width);

#endif

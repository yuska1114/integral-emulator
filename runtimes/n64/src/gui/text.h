/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_GUI_TEXT_H
#define INTEGRAL_N64_RUNTIME_GUI_TEXT_H

#include <SDL.h>

void integral_n64_runtime_gui_draw_text(SDL_Renderer *renderer, int x, int y,
                             const char *text, int scale, SDL_Color color);
void integral_n64_runtime_gui_draw_text_fit(SDL_Renderer *renderer, int x, int y,
                                 const char *text, int scale, SDL_Color color,
                                 int max_width);

#endif

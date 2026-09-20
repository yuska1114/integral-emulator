/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_VIEW_H
#define INTEGRAL_CLIENT_VIEW_H
#include "client_state.h"

void format_room_phase(const AppState *state,
                              const IntegralApiRoom *room,
                              int ready1,
                              int ready2,
                              char *out,
                              size_t out_size);
void draw_runtime_exit_confirmation(SDL_Renderer *renderer,
                                           const AppState *state);
void draw_app(SDL_Renderer *renderer, const AppState *state);
#endif

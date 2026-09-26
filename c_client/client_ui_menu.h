/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_UI_MENU_H
#define INTEGRAL_CLIENT_UI_MENU_H

#include <SDL.h>
#include <stdbool.h>
#include "client_settings.h"

#define INTEGRAL_MAIN_ROWS 6
#define INTEGRAL_LOCAL_MODE_ROWS 3
#define INTEGRAL_ROOM_MODE_ROWS 3
typedef struct IntegralClientMenuView {
    const char *username;
    const char *server;
    const char *version;
    const char *status;
    const char *save_notice;
    const char *save_error;
    unsigned selected;
} IntegralClientMenuView;

void integral_client_ui_draw_main_menu(SDL_Renderer *renderer, const IntegralClientMenuView *view);
void integral_client_ui_draw_settings(SDL_Renderer *renderer, const IntegralClientMenuView *view);
void integral_client_ui_draw_options(SDL_Renderer *renderer, const IntegralClientMenuView *view,
                                     unsigned ir_off_delay_ticks,
                                     int sgb_enabled);
void integral_client_ui_draw_room_mode(SDL_Renderer *renderer, const IntegralClientMenuView *view);
void integral_client_ui_draw_join_room(SDL_Renderer *renderer, const IntegralClientMenuView *view,
                                      const char *room_code, bool editing);
void integral_client_ui_draw_local_mode(SDL_Renderer *renderer, const IntegralClientMenuView *view);

#endif

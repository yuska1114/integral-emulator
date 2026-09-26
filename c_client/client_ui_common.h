/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_UI_COMMON_H
#define INTEGRAL_CLIENT_UI_COMMON_H

#include <SDL.h>
void integral_client_ui_draw_text_fit(SDL_Renderer *, int, int, const char *, int, SDL_Color, int);
#include <stdbool.h>

#define INTEGRAL_CLIENT_UI_WIDTH 480

typedef struct IntegralClientUiTheme {
    SDL_Color background;
    SDL_Color panel_fill;
    SDL_Color panel_border;
    SDL_Color label;
    SDL_Color value;
    SDL_Color title;
    SDL_Color selected;
    SDL_Color selected_bright;
    SDL_Color muted;
    SDL_Color body_text;
    SDL_Color warning;
    SDL_Color warning_error;
    SDL_Color cursor;
    SDL_Color selection_background;
    SDL_Color active_background;
    SDL_Color modal_background;
} IntegralClientUiTheme;

const IntegralClientUiTheme *integral_client_ui_theme(void);
void integral_client_ui_clear_screen(SDL_Renderer *renderer);
void integral_client_ui_fill_selection(SDL_Renderer *renderer, SDL_Rect bounds, bool editing);
void integral_client_ui_draw_selection(SDL_Renderer *renderer,
                                      SDL_Rect bounds,
                                      int cursor_x,
                                      int cursor_y,
                                      int cursor_scale,
                                      bool editing);

typedef enum AppScreen {
    SCREEN_LOGIN,
    SCREEN_PASSWORD_CHANGE,
    SCREEN_MAIN_MENU,
    SCREEN_LOCAL_MODE,
    SCREEN_LOCAL,
    SCREEN_GB_MOBILE,
    SCREEN_ROOM_MODE,
    SCREEN_JOIN_ROOM,
    SCREEN_ROOM,
    SCREEN_N64_ROOM,
    SCREEN_SETTINGS,
    SCREEN_OPTIONS,
    SCREEN_GB_KEY_CONFIG,
    SCREEN_N64_KEY_CONFIG,
    SCREEN_UTIL_KEY_CONFIG,
    SCREEN_ROM_REGISTER,
    SCREEN_N64_RUNTIME,
    SCREEN_SCREENSHOTS,
} AppScreen;

void integral_client_ui_draw_panel(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color border);
void integral_client_ui_draw_field(SDL_Renderer *renderer,
                                   int y,
                                   const char *label,
                                   const char *value,
                                   bool selected,
                                   bool editing);
void integral_client_ui_draw_header(SDL_Renderer *renderer,
                                    const char *subtitle,
                                    const char *login_id,
                                    const char *server_url,
                                    const char *version);
void integral_client_ui_draw_marquee_text_fit(SDL_Renderer *renderer,
                                               int x,
                                               int y,
                                               const char *text,
                                               int scale,
                                               SDL_Color color,
                                               int max_width,
                                               unsigned phase_seed);

#endif

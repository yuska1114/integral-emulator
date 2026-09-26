/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_ui_account.h"

#include <string.h>

#include "client_ui_common.h"
#include "sdl_text.h"

void integral_client_ui_draw_login(SDL_Renderer *renderer, const IntegralClientLoginView *view)
{
    integral_client_ui_clear_screen(renderer);
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    SDL_Color selected = theme->selected;
    SDL_Color muted = theme->muted;
    SDL_Color value = theme->value;
    SDL_Color warning = theme->warning;

    integral_client_ui_draw_header(renderer, "ACCOUNT LOGIN", NULL, NULL, view->version);
    integral_client_ui_draw_field(renderer, 88, "SERVER", view->server,
                                   view->selected == FIELD_SERVER, view->editing);
    integral_client_ui_draw_field(renderer, 142, "ENV", view->server_label,
                                   view->selected == FIELD_ENV, false);
    integral_client_ui_draw_field(renderer, 196, "USERNAME", view->username,
                                   view->selected == FIELD_USERNAME, view->editing);
    integral_client_ui_draw_field(renderer, 250, "PASSWORD", view->password_display,
                                   view->selected == FIELD_PASSWORD, view->editing);

    if (view->selected == FIELD_ACTION) {
        SDL_Rect highlight = {.x = 112, .y = 352, .w = 180, .h = 42};
        integral_client_ui_draw_selection(renderer, highlight, 122, 364, 3, false);
        integral_sdl_draw_text(renderer, 140, 364, "LOGIN", 3, selected);
    }
    else {
        integral_client_ui_draw_panel(renderer, 112, 352, 180, 42, theme->panel_border);
        integral_sdl_draw_text(renderer, 140, 364, "LOGIN", 3, value);
    }

    if (view->selected == FIELD_REMEMBER) {
        SDL_Rect highlight = {.x = 14, .y = 306, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 34};
        integral_client_ui_draw_selection(renderer, highlight, 24, 316, 1, false);
    }
    integral_sdl_draw_text(renderer,
                           48,
                           316,
                           view->remember_login ? "[ON] REMEMBER LOGIN" : "[OFF] REMEMBER LOGIN",
                           1,
                           view->selected == FIELD_REMEMBER ? selected : value);

    bool plain_http = strncmp(view->server, "http://", 7) == 0;
    if (plain_http) {
        integral_sdl_draw_text_fit(renderer, 22, 398,
                                   "WARNING HTTP CONNECTION NOT ENCRYPTED",
                                   1, warning, INTEGRAL_CLIENT_UI_WIDTH - 44);
    }
    const char *status = view->status;
    if (!status[0] || strcmp(status, "ENTER SERVER URL") == 0) {
        status = view->selected == FIELD_USERNAME ? "ENTER USERNAME" :
                 view->selected == FIELD_PASSWORD ? "ENTER PASSWORD" :
                 view->selected == FIELD_SERVER ? "ENTER SERVER URL" :
                 view->selected == FIELD_ENV ? "LEFT/RIGHT SELECT ENVIRONMENT" :
                 view->selected == FIELD_REMEMBER ? "ENTER TOGGLE REMEMBER LOGIN" : "ENTER LOGIN";
    }
    integral_sdl_draw_text_fit(renderer, 22, plain_http ? 416 : 408,
                               status, 1, muted,
                               INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  LEFT/RIGHT ENV  F2 EDIT  F3 SHOW PASS", 1, muted);
    integral_sdl_draw_text(renderer, 22, 458, "ESC CANCEL", 1, muted);
    SDL_RenderPresent(renderer);
}

void integral_client_ui_draw_password_change(SDL_Renderer *renderer,
                                             const IntegralClientPasswordChangeView *view)
{
    integral_client_ui_clear_screen(renderer);
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    SDL_Color selected = theme->selected;
    SDL_Color muted = theme->muted;
    SDL_Color value = theme->value;

    integral_client_ui_draw_header(renderer, "CHANGE PASSWORD", view->username, view->server, view->version);
    integral_sdl_draw_text(renderer, 24, 88, "INITIAL PASSWORD MUST BE CHANGED", 1, muted);

    integral_client_ui_draw_field(renderer,
               132,
               "NEW PASSWORD",
               view->new_display,
               view->selected == PASSWORD_CHANGE_NEW,
               view->editing);
    integral_client_ui_draw_field(renderer,
               202,
               "CONFIRM",
               view->confirm_display,
               view->selected == PASSWORD_CHANGE_CONFIRM,
               view->editing);

    if (view->selected == PASSWORD_CHANGE_SAVE) {
        SDL_Rect highlight = {.x = 14, .y = 304, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 42};
        integral_client_ui_fill_selection(renderer, highlight, false);
        integral_sdl_draw_text(renderer, 132, 316, "> SAVE", 3, selected);
    }
    else {
        integral_client_ui_draw_panel(renderer, 112, 304, 180, 42, theme->panel_border);
        integral_sdl_draw_text(renderer, 144, 316, "SAVE", 3, value);
    }
    integral_sdl_draw_text_fit(renderer, 22, 392, view->status, 1, muted, INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 420, "ALNUM ONLY  8+ CHARS", 1, muted);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT  F2 EDIT", 1, muted);
    SDL_RenderPresent(renderer);
}

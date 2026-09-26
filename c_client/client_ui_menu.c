/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_ui_menu.h"

#include <stdio.h>

#include "client_ui_common.h"
#include "sdl_text.h"

void integral_client_ui_draw_main_menu(SDL_Renderer *renderer, const IntegralClientMenuView *view)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    integral_client_ui_draw_header(renderer, "MAIN MENU", view->username, view->server, view->version);
    const char *labels[] = {
        "LOCAL",
        "CREATE ROOM",
        "JOIN ROOM",
        "ROM REGISTER",
        "SETTINGS",
        "SCREENSHOTS",
    };
    const char *values[] = {
        "PLAY ON THIS MACHINE",
        "GET A 5 DIGIT CODE",
        "ENTER A 5 DIGIT CODE",
        "MANAGE 8 ROM SLOTS",
        "",
        "",
    };

    for (unsigned i = 0; i < INTEGRAL_MAIN_ROWS; i++) {
        int y = 92 + (int)i * 46;
        if (view->selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 6, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 42};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 6, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y, labels[i], 2, view->selected == i ? selected : label);
        integral_sdl_draw_text_fit(renderer, 48, y + 24, values[i], 1, value, 380);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, view->status, 1, muted, INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC LOGOUT", 1, muted);
    if (view->save_notice) integral_sdl_draw_text_fit(renderer, 22, 374, view->save_notice, 1, value, INTEGRAL_CLIENT_UI_WIDTH - 44);
    if (view->save_error) integral_sdl_draw_text_fit(renderer, 22, 392, view->save_error, 1, value, INTEGRAL_CLIENT_UI_WIDTH - 44);
    SDL_RenderPresent(renderer);
}


void integral_client_ui_draw_settings(SDL_Renderer *renderer, const IntegralClientMenuView *view)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    const char *labels[INTEGRAL_SETTINGS_ROWS] = {
        "GB KEYS CONFIG",
        "N64 KEYS CONFIG",
        "UTIL KEYS CONFIG",
        "OPTIONS",
        "BACK",
    };

    integral_client_ui_draw_header(renderer, "SETTINGS", view->username, view->server, view->version);
    for (unsigned i = 0; i < INTEGRAL_SETTINGS_ROWS; i++) {
        int y = 110 + (int)i * 54;
        if (view->selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 42};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y, labels[i], 2,
                               view->selected == i ? selected : label);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, view->status, 1, muted, INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MAIN MENU", 1, muted);
    SDL_RenderPresent(renderer);
}


void integral_client_ui_draw_options(SDL_Renderer *renderer,
                                     const IntegralClientMenuView *view,
                                     unsigned ir_off_delay_ticks,
                                     int sgb_enabled)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    char ir_delay_value[24];
    snprintf(ir_delay_value, sizeof(ir_delay_value), "<%u> tick", ir_off_delay_ticks);
    const char *labels[INTEGRAL_OPTIONS_ROWS] = {
        "IR RELEASE DELAY",
        "SGB",
        "BACK",
    };
    const char *values[INTEGRAL_OPTIONS_ROWS] = {
        ir_delay_value,
        sgb_enabled ? "<ENABLE>" : "<DISABLE>",
        "",
    };

    integral_client_ui_draw_header(renderer, "OPTIONS",
                                   view->username, view->server, view->version);
    for (unsigned i = 0; i < INTEGRAL_OPTIONS_ROWS; i++) {
        int y = 126 + (int)i * 70;
        if (view->selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 10,
                             .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 46};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y, labels[i], 2,
                               view->selected == i ? selected : label);
        if (values[i][0]) {
            integral_sdl_draw_text(renderer, 320, y, values[i], 2, value);
        }
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, view->status, 1, muted,
                               INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  LEFT/RIGHT CHANGE", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ENTER SELECT  ESC SETTINGS", 1, muted);
    SDL_RenderPresent(renderer);
}


void integral_client_ui_draw_room_mode(SDL_Renderer *renderer, const IntegralClientMenuView *view)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    const char *buttons[] = {"LINK CABLE ROOM", "N64 ROOM", "BACK"};

    integral_client_ui_draw_header(renderer, "CREATE ROOM", view->username, view->server, view->version);
    for (unsigned i = 0; i < INTEGRAL_ROOM_MODE_ROWS; i++) {
        int y = 126 + (int)i * 86;
        if (view->selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect button = {.x = 14, .y = y - 10, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 58};
            SDL_RenderFillRect(renderer, &button);
            integral_sdl_draw_text(renderer, 24, y + 7, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y,
                               buttons[i], 3,
                               view->selected == i ? selected : label);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, view->status, 1, muted, INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "ARROWS MOVE  ENTER SELECT", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MAIN MENU", 1, muted);
    SDL_RenderPresent(renderer);
}


void integral_client_ui_draw_join_room(SDL_Renderer *renderer, const IntegralClientMenuView *view,
                                      const char *room_code, bool editing)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    integral_client_ui_draw_header(renderer, "JOIN ROOM", view->username, view->server, view->version);
    integral_sdl_draw_text(renderer, 48, 150, "ROOM CODE", 2, label);
    SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
    SDL_Rect input = {.x = 46, .y = 188, .w = 388, .h = 72};
    SDL_RenderFillRect(renderer, &input);
    char display[16];
    snprintf(display, sizeof(display), "%s%s", room_code,
             editing ? "_" : "");
    integral_sdl_draw_text_fit(renderer, 154, 208,
                               display[0] ? display : "-----", 4,
                               editing ? selected : value, 220);
    integral_sdl_draw_text(renderer, 48, 272, "5 DIGITS", 1, muted);
    integral_sdl_draw_text(renderer, 48, 294, "SAVE DATA NOTICE", 1, label);
    const char *notice_lines[] = {
        "WHEN LINK PLAY STARTS, YOUR SELECTED SAV DATA",
        "WILL BE SENT TEMPORARILY TO THE ROOM HOST.",
        "THE OFFICIAL CLIENT USES IT ONLY IN EMULATOR",
        "MEMORY AND DOES NOT SAVE IT AS A FILE ON THE",
        "HOST PC. ONLY JOIN A ROOM CODE RECEIVED FROM",
        "SOMEONE YOU TRUST.",
    };
    for (size_t i = 0; i < sizeof(notice_lines) / sizeof(notice_lines[0]); i++) {
        integral_sdl_draw_text_fit(renderer, 48, 314 + (int)i * 14,
                                   notice_lines[i], 1, muted,
                                   INTEGRAL_CLIENT_UI_WIDTH - 72);
    }
    integral_sdl_draw_text_fit(renderer, 22, 410, view->status, 1, muted,
                               INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "ENTER JOIN  BACKSPACE CLEAR", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MAIN MENU", 1, muted);
    SDL_RenderPresent(renderer);
}


void integral_client_ui_draw_local_mode(SDL_Renderer *renderer, const IntegralClientMenuView *view)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    integral_client_ui_draw_header(renderer, "LOCAL", view->username, view->server, view->version);
    const char *labels[] = {
        "GB MODE",
        "MOBILE MODE",
        "N64 MODE",
    };

    for (unsigned i = 0; i < INTEGRAL_LOCAL_MODE_ROWS; i++) {
        int y = 126 + (int)i * 76;
        if (view->selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 10, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 58};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
        }
        integral_sdl_draw_text_fit(renderer,
                                   48,
                                   y,
                                   labels[i],
                                   2,
                                   view->selected == i ? selected : label,
                                   390);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, view->status, 1, muted, INTEGRAL_CLIENT_UI_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MENU", 1, muted);
    if (view->save_notice) integral_sdl_draw_text_fit(renderer, 22, 374, view->save_notice, 1, (SDL_Color){238, 238, 238, 255}, INTEGRAL_CLIENT_UI_WIDTH - 44);
    if (view->save_error) integral_sdl_draw_text_fit(renderer, 22, 392, view->save_error, 1, (SDL_Color){238, 238, 238, 255}, INTEGRAL_CLIENT_UI_WIDTH - 44);
    SDL_RenderPresent(renderer);
}

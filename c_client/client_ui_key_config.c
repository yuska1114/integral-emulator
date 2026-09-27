/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_view.h"
#include "client_app.h"
#include "client_rom_catalog.h"
#include "client_runtime_support.h"
#include "client_version.h"
#include "client_ui_common.h"
#include "client_key_config.h"
#include "client_room_common.h"
#include "sdl_text.h"
#include "sdl_unicode_text.h"
#include <stdio.h>
#include <string.h>

#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#define INTEGRAL_WINDOW_HEIGHT 480

static void draw_header(SDL_Renderer *renderer, const char *subtitle,
                        const char *login_id, const char *server_url)
{
    integral_client_ui_draw_header(renderer, subtitle, login_id, server_url, INTEGRAL_CLIENT_VERSION);
}

void integral_client_ui_draw_key_config(SDL_Renderer *renderer, const AppState *state)
{
    integral_client_ui_clear_screen(renderer);
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    SDL_Color label = theme->label;
    SDL_Color value = theme->value;
    SDL_Color selected = theme->selected;
    SDL_Color muted = theme->muted;

    const char *title = "GB KEYS CONFIG";
    if (state->key_editor.page == KEY_CONFIG_PAGE_N64) title = "N64 KEYS CONFIG";
    if (state->key_editor.page == KEY_CONFIG_PAGE_UTIL) title = "UTIL KEYS";
    draw_header(renderer, title, state->login.username, state->login.server);
    char slot1_line1[192];
    char slot1_line2[192];
    char slot2_line1[192];
    char slot2_line2[192];
    char n64_line1[192];
    char n64_line2[192];
    char n64_line3[192];
    char util_line1[192];
    char util_line2[192];
    char util_line3[192];
    char alias_line1[192];
    char alias_line2[192];
    format_slot_key_summary(state->keys.slot1, slot1_line1, sizeof(slot1_line1), slot1_line2, sizeof(slot1_line2));
    format_slot_key_summary(state->keys.slot2, slot2_line1, sizeof(slot2_line1), slot2_line2, sizeof(slot2_line2));
    const char *n64_key_spec = state->keys.n64_p1;
    switch (state->key_editor.n64_controller_index) {
        case 1u:
            n64_key_spec = state->keys.n64_p2;
            break;
        case 2u:
            n64_key_spec = state->keys.n64_p3;
            break;
        case 3u:
            n64_key_spec = state->keys.n64_p4;
            break;
        default:
            break;
    }
    format_n64_key_summary(n64_key_spec,
                           n64_line1,
                           sizeof(n64_line1),
                           n64_line2,
                           sizeof(n64_line2),
                           n64_line3,
                           sizeof(n64_line3));
    format_util_key_summary(&state->keys,
                            util_line1,
                            sizeof(util_line1),
                            util_line2,
                            sizeof(util_line2),
                            util_line3,
                            sizeof(util_line3));
    format_client_alias_summary(&state->keys,
                                alias_line1,
                                sizeof(alias_line1),
                                alias_line2,
                                sizeof(alias_line2));

    unsigned reset_row = state->key_editor.page == KEY_CONFIG_PAGE_GB ? 2u :
        (state->key_editor.page == KEY_CONFIG_PAGE_N64 ? 2u : 2u);
    unsigned back_row = reset_row + 1u;
    const char *group_label = state->key_editor.page == KEY_CONFIG_PAGE_GB ? NULL :
        (state->key_editor.page == KEY_CONFIG_PAGE_N64 ? "N64 KEYS" : "UTIL KEYS");

    if (state->key_editor.page == KEY_CONFIG_PAGE_GB) {
        const char *labels[] = {"SLOT 1 KEYS", "SLOT 2 KEYS"};
        const char *line1[] = {slot1_line1, slot2_line1};
        const char *line2[] = {slot1_line2, slot2_line2};
        const int y[] = {88, 188};
        for (unsigned i = 0; i < 2; i++) {
            if (state->key_editor.key_selected == i) {
                SDL_Rect rect = {.x = 14, .y = y[i] - 6, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 62};
                integral_client_ui_draw_selection(renderer, rect, 24, y[i] + 4, 2, false);
            }
            integral_sdl_draw_text(renderer, 48, y[i], labels[i], 2,
                                   state->key_editor.key_selected == i ? selected : label);
            integral_client_ui_draw_text_fit(renderer, 48, y[i] + 26, line1[i], 1, value, 400);
            integral_client_ui_draw_text_fit(renderer, 48, y[i] + 42, line2[i], 1, value, 400);
        }
    }
    else if (state->key_editor.page == KEY_CONFIG_PAGE_N64) {
        char controller_label[32];
        snprintf(controller_label,
                 sizeof(controller_label),
                 "N64 CONTROLLER < %uP >",
                 state->key_editor.n64_controller_index + 1u);
        if (state->key_editor.key_selected == 0u) {
            SDL_Rect rect = {.x = 14, .y = 94, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 26};
            integral_client_ui_draw_selection(renderer, rect, 24, 99, 2, false);
        }
        integral_sdl_draw_text(renderer, 48, 100, controller_label, 2,
                               state->key_editor.key_selected == 0u ? selected : label);
        if (state->key_editor.key_selected == 1u) {
            SDL_Rect rect = {.x = 14, .y = 143, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 88};
            integral_client_ui_draw_selection(renderer, rect, 24, 148, 2, false);
        }
        integral_sdl_draw_text(renderer, 48, 146, group_label,
                               2, state->key_editor.key_selected == 1u ? selected : label);
        integral_client_ui_draw_text_fit(renderer, 48, 174, n64_line1, 1, value, 400);
        integral_client_ui_draw_text_fit(renderer, 48, 192, n64_line2, 1, value, 400);
        integral_client_ui_draw_text_fit(renderer, 48, 210, n64_line3, 1, value, 400);
    }
    else {
        if (state->key_editor.key_selected == 0) {
            SDL_Rect rect = {.x = 14, .y = 76, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 78};
            integral_client_ui_draw_selection(renderer, rect, 24, 81, 2, false);
        }
        integral_sdl_draw_text(renderer, 48, 80, group_label,
                               2, state->key_editor.key_selected == 0 ? selected : label);
        integral_client_ui_draw_text_fit(renderer, 48, 108, util_line1, 1, value, 400);
        integral_client_ui_draw_text_fit(renderer, 48, 126, util_line2, 1, value, 400);
        integral_client_ui_draw_text_fit(renderer, 48, 144, util_line3, 1, value, 400);

        integral_sdl_draw_text(renderer, 22, 162, "FAST / TURBO: LOCAL GB 1P ONLY", 1, muted);
        integral_sdl_draw_text(renderer, 22, 180, "RESET: LOCAL MODES", 1, muted);
        integral_sdl_draw_text(renderer, 22, 198, "SCREENSHOT / ESCAPE: ALL MODES", 1, muted);
        if (state->key_editor.key_selected == 1u) {
            SDL_Rect rect = {.x = 14, .y = 220, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 66};
            integral_client_ui_draw_selection(renderer, rect, 24, 227, 2, false);
        }
        integral_sdl_draw_text(renderer, 48, 226, "CLIENT ALIAS", 2,
                               state->key_editor.key_selected == 1u ? selected : label);
        integral_client_ui_draw_text_fit(renderer, 48, 254, alias_line1, 1, value, 400);
        integral_client_ui_draw_text_fit(renderer, 48, 272, alias_line2, 1, value, 400);
    }

    for (unsigned row = reset_row; row <= back_row; row++) {
        const char *text = row == reset_row ? "RESET DEFAULTS" : "BACK";
        int y = state->key_editor.page == KEY_CONFIG_PAGE_UTIL ? 310 + (int)(row - reset_row) * 34 :
                300 + (int)(row - reset_row) * 34;
        if (state->key_editor.key_selected == row) {
            SDL_Rect rect = {.x = 14, .y = y - 5, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 26};
            integral_client_ui_draw_selection(renderer, rect, 24, y, 2, false);
        }
        integral_sdl_draw_text(renderer, 48, y, text, 2,
                               state->key_editor.key_selected == row ? selected : label);
    }

    if (state->key_editor.key_capture_target != KEY_CAPTURE_NONE) {
        SDL_Rect overlay = {.x = 42, .y = 320, .w = 396, .h = 88};
        SDL_Color overlay_color = theme->panel_fill;
        overlay_color.a = 245;
        SDL_SetRenderDrawColor(renderer, overlay_color.r, overlay_color.g, overlay_color.b, overlay_color.a);
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 62, 342, "PRESS KEY FOR", 2, selected);
        integral_sdl_draw_text(renderer,
                          62,
                          372,
                          key_config_step_label(state->key_editor.key_capture_target, state->key_editor.key_capture_step),
                          2,
                          value);
        integral_sdl_draw_text(renderer, 62, 394, "KEYBOARD / GAMEPAD", 1, muted);
    }

    integral_client_ui_draw_text_fit(renderer, 22, 416, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER CONFIGURE  USB / BLUETOOTH", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC BACK / CANCEL", 1, muted);
    SDL_RenderPresent(renderer);
}

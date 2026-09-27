/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_view.h"
#include "client_app.h"
#include "client_local.h"
#include "client_runtime_support.h"
#include "client_version.h"
#include "client_ui_account.h"
#include "client_ui_menu.h"
#include "client_ui_common.h"
#include "sdl_text.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>


#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#define INTEGRAL_GB_RUNTIME_MODE_ROWS 3
#define INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS 3
#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

static void mask_password(const char *password, char *out, size_t out_size)
{
    size_t len = strlen(password);
    if (len >= out_size) {
        len = out_size - 1;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = '*';
    }
    out[len] = '\0';
}


static void draw_header(SDL_Renderer *renderer, const char *subtitle, const char *login_id, const char *server_url)
{
    integral_client_ui_draw_header(renderer, subtitle, login_id, server_url, INTEGRAL_CLIENT_VERSION);
}


static void draw_login(SDL_Renderer *renderer, const LoginState *state)
{
    char password_display[80];
    if (state->password_visible) {
        copy_text(password_display, sizeof(password_display), state->password);
    }
    else {
        mask_password(state->password, password_display, sizeof(password_display));
    }
    IntegralClientLoginView view = {
        .server = state->server,
        .server_label = login_server_label(state),
        .username = state->username,
        .password_display = password_display,
        .status = state->status,
        .version = INTEGRAL_CLIENT_VERSION,
        .selected = state->selected,
        .editing = state->editing,
        .remember_login = state->remember_login,
    };
    integral_client_ui_draw_login(renderer, &view);
}


static void draw_password_change(SDL_Renderer *renderer, const AppState *app)
{
    const PasswordChangeState *state = &app->password_change;
    char new_display[80];
    char confirm_display[80];
    if (state->password_visible) {
        copy_text(new_display, sizeof(new_display), state->new_password);
        copy_text(confirm_display, sizeof(confirm_display), state->confirm_password);
    }
    else {
        mask_password(state->new_password, new_display, sizeof(new_display));
        mask_password(state->confirm_password, confirm_display, sizeof(confirm_display));
    }

    IntegralClientPasswordChangeView view = {
        .username = app->login.username,
        .server = app->login.server,
        .version = INTEGRAL_CLIENT_VERSION,
        .new_display = new_display,
        .confirm_display = confirm_display,
        .status = state->status,
        .selected = state->selected,
        .editing = state->editing,
    };
    integral_client_ui_draw_password_change(renderer, &view);
}


static IntegralClientMenuView menu_view(const AppState *state, unsigned selected)
{
    return (IntegralClientMenuView){
        .username = state->login.username,
        .server = state->login.server,
        .version = INTEGRAL_CLIENT_VERSION,
        .status = state->login.status,
        .save_notice = state->local.save_sync_notice,
        .save_error = state->local.save_sync_error,
        .selected = selected,
    };
}


static void draw_main_menu(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, state->ui.main_selected);
    integral_client_ui_draw_main_menu(renderer, &view);
}


static void draw_settings(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, state->ui.settings_selected);
    integral_client_ui_draw_settings(renderer, &view);
}


static void draw_options(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, state->ui.options_selected);
    integral_client_ui_draw_options(renderer, &view,
                                    state->ui.options_ir_off_delay_ticks,
                                    state->ui.options_sgb_enabled);
}


static void draw_room_mode(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, state->room.common.room_mode_selected);
    integral_client_ui_draw_room_mode(renderer, &view);
}


static void draw_join_room(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, 0);
    integral_client_ui_draw_join_room(renderer, &view, state->room.common.room_code_input, state->room.common.room_code_editing);
}


static void format_local_slot_label(const AppState *state, unsigned local_slot, const char *label, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = local_selected_rom_slot(state, local_slot);
    if (!slot) {
        snprintf(out, out_size, "%s EMPTY", label);
        return;
    }
    snprintf(out, out_size, "%s ROM%d", label, state->local.local_slot_indices[local_slot] + 1);
}


static void format_rom_filename_with_header(const AppState *state, int slot_index, char *out, size_t out_size)
{
    if (slot_index < 0 || slot_index >= INTEGRAL_ROM_SLOTS) {
        out[0] = '\0';
        return;
    }
    const IntegralConfigRomSlot *slot = &state->catalog.rom_slots[slot_index];
    const IntegralClientRomMetadataCache *cached = client_rom_metadata_for_slot(state, slot_index);

    if (cached && cached->header_valid && cached->metadata.header_title[0] != '\0') {
        snprintf(out, out_size, "%s (%s)", path_file_name(slot->rom_path), cached->metadata.header_title);
        return;
    }
    copy_text(out, out_size, path_file_name(slot->rom_path));
}


static void format_local_slot_detail(const AppState *state, unsigned local_slot, char *out, size_t out_size)
{
    if (!local_selected_rom_slot(state, local_slot)) {
        copy_text(out, out_size, "LEFT/RIGHT SELECT ROM1-8");
        return;
    }
    format_rom_filename_with_header(state, state->local.local_slot_indices[local_slot], out, out_size);
}








static void draw_local_mode(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, state->local.local_mode_selected);
    integral_client_ui_draw_local_mode(renderer, &view);
}


static void draw_gb_slot_screen(SDL_Renderer *renderer, const AppState *state, bool mobile_mode)
{
    integral_client_ui_clear_screen(renderer);
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    SDL_Color label = theme->label;
    SDL_Color value = theme->value;
    SDL_Color selected = theme->selected;
    SDL_Color muted = theme->muted;

    draw_header(renderer,
                mobile_mode ? "MOBILE MODE" : "GB MODE",
                state->login.username,
                state->login.server);

    char slot1[220];
    char slot1_detail[220];
    format_local_slot_label(state, 0, "SLOT1", slot1, sizeof(slot1));
    format_local_slot_detail(state, 0, slot1_detail, sizeof(slot1_detail));
    char slot2[220] = "";
    char slot2_detail[220] = "";
    if (!mobile_mode) {
        format_local_slot_label(state, 1, "SLOT2", slot2, sizeof(slot2));
        format_local_slot_detail(state, 1, slot2_detail, sizeof(slot2_detail));
    }
    char scenario_detail[96] = "SELECT SLOT1 FIRST";
    if (mobile_mode && state->local.mobile_scenario_count > 0u &&
        state->local.mobile_scenario_selected < state->local.mobile_scenario_count) {
        copy_text(scenario_detail, sizeof(scenario_detail),
                  state->local.mobile_scenarios[state->local.mobile_scenario_selected].display_name);
    }
    else if (mobile_mode && local_selected_rom_slot(state, 0)) {
        copy_text(scenario_detail, sizeof(scenario_detail), "NOT AVAILABLE");
    }
    const char *labels[] = {"START", slot1, mobile_mode ? "SCENARIO" : slot2};
    const char *details[] = {
        "",
        slot1_detail,
        mobile_mode ? scenario_detail : slot2_detail,
    };
    unsigned row_count = mobile_mode ? INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS : INTEGRAL_GB_RUNTIME_MODE_ROWS;

    for (unsigned i = 0; i < row_count; i++) {
        int y = 118 + (int)i * 76;
        if (state->local.local_selected == i) {
            SDL_Rect rect = {.x = 14, .y = y - 10, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 58};
            integral_client_ui_draw_selection(renderer, rect, 24, y, 2, false);
        }
        integral_client_ui_draw_text_fit(renderer, 48, y, labels[i], 2, state->local.local_selected == i ? selected : label, 390);
        if (details[i][0] != '\0') {
            integral_client_ui_draw_text_fit(renderer, 48, y + 28, details[i], 1, value, 390);
        }
    }

    integral_client_ui_draw_text_fit(renderer, 22, 374, state->local.save_sync_notice, 1, value, INTEGRAL_WINDOW_WIDTH - 44);
    integral_client_ui_draw_text_fit(renderer, 22, 392, state->local.save_sync_error, 1, value, INTEGRAL_WINDOW_WIDTH - 44);
    integral_client_ui_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    const char *hint = state->local.local_selected == 0 ? "ENTER START" :
        mobile_mode && state->local.local_selected == 2 ? "LEFT/RIGHT: SELECT SCENARIO" : "LEFT/RIGHT: SELECT ROM1-8";
    integral_sdl_draw_text(renderer, 22, 438, hint, 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "TAB MOVE  ESC LOCAL MODES", 1, muted);
    SDL_RenderPresent(renderer);
}


static void draw_local(SDL_Renderer *renderer, const AppState *state)
{
    draw_gb_slot_screen(renderer, state, false);
}


static void draw_gb_mobile(SDL_Renderer *renderer, const AppState *state)
{
    draw_gb_slot_screen(renderer, state, true);
}


static void draw_n64_runtime(SDL_Renderer *renderer, const AppState *state)
{
    integral_client_ui_clear_screen(renderer);
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    SDL_Color label = theme->label;
    SDL_Color value = theme->value;
    SDL_Color selected = theme->selected;
    SDL_Color muted = theme->muted;

    draw_header(renderer, "N64 MODE", state->login.username, state->login.server);

    char n64_label[240];
    char n64_detail[240] = "";
    const IntegralConfigRomSlot *n64_slot = selected_n64_rom_slot(state);
    if (n64_slot) {
        snprintf(n64_label,
                 sizeof(n64_label),
                 "N64 SLOT ROM%d",
                 state->local.integral_n64_runtime_n64_slot_index + 1);
        format_rom_filename_with_header(state, state->local.integral_n64_runtime_n64_slot_index,
                                        n64_detail, sizeof(n64_detail));
    }
    else {
        copy_text(n64_label, sizeof(n64_label), "N64 SLOT <EMPTY>");
    }

    char transfer_labels[4][240];
    for (unsigned i = 0; i < 4; i++) {
        int index = state->local.integral_n64_runtime_transfer_slot_indices[i];
        const IntegralConfigRomSlot *slot = selected_transfer_rom_slot(state, i);
        if (slot) {
            snprintf(transfer_labels[i], sizeof(transfer_labels[i]), "SLOT%u ROM%d", i + 1, index + 1);
        }
        else {
            snprintf(transfer_labels[i], sizeof(transfer_labels[i]), "SLOT%u <EMPTY>", i + 1);
        }
    }

    const char *labels[] = {
        "START",
        n64_label,
        transfer_labels[0],
        transfer_labels[1],
        transfer_labels[2],
        transfer_labels[3],
    };

    for (unsigned i = 0; i < 6; i++) {
        int y = 102 + (int)i * 48;
        if (state->local.integral_n64_runtime_selected == i) {
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 38};
            integral_client_ui_draw_selection(renderer, rect, 24, y + 2, 2, false);
        }
        integral_client_ui_draw_text_fit(renderer, 48, y, labels[i], 2, state->local.integral_n64_runtime_selected == i ? selected : label, 390);
        if (i == 1 && n64_slot) {
            integral_client_ui_draw_text_fit(renderer, 48, y + 24, n64_detail, 1, value, 390);
        }
        else if (i >= 2) {
            const IntegralConfigRomSlot *slot = selected_transfer_rom_slot(state, i - 2);
            if (slot) {
                char slot_detail[240];
                int slot_index = state->local.integral_n64_runtime_transfer_slot_indices[i - 2];
                format_rom_filename_with_header(state, slot_index, slot_detail, sizeof(slot_detail));
                integral_client_ui_draw_text_fit(renderer, 48, y + 24, slot_detail, 1, value, 390);
            }
        }
    }

    integral_client_ui_draw_text_fit(renderer, 22, 374, state->local.save_sync_notice, 1, value, INTEGRAL_WINDOW_WIDTH - 44);
    integral_client_ui_draw_text_fit(renderer, 22, 392, state->local.save_sync_error, 1, value, INTEGRAL_WINDOW_WIDTH - 44);
    integral_client_ui_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER START/SELECT  LEFT/RIGHT ROM1-8", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC LOCAL MODES", 1, muted);
    SDL_RenderPresent(renderer);
}




























void draw_app(SDL_Renderer *renderer, const AppState *state)
{
    switch (state->ui.screen) {
        case SCREEN_LOGIN:
            draw_login(renderer, &state->login);
            break;
        case SCREEN_PASSWORD_CHANGE:
            draw_password_change(renderer, state);
            break;
        case SCREEN_MAIN_MENU:
            draw_main_menu(renderer, state);
            break;
        case SCREEN_SCREENSHOTS:
            integral_screenshots_draw(renderer, state->screenshots);
            break;
        case SCREEN_LOCAL_MODE:
            draw_local_mode(renderer, state);
            break;
        case SCREEN_LOCAL:
            draw_local(renderer, state);
            break;
        case SCREEN_GB_MOBILE:
            draw_gb_mobile(renderer, state);
            break;
        case SCREEN_ROOM_MODE:
            draw_room_mode(renderer, state);
            break;
        case SCREEN_N64_RUNTIME:
            draw_n64_runtime(renderer, state);
            break;
        case SCREEN_JOIN_ROOM:
            draw_join_room(renderer, state);
            break;
        case SCREEN_ROOM:
            integral_client_ui_draw_room_link(renderer, state);
            break;
        case SCREEN_N64_ROOM:
            integral_client_ui_draw_room_n64(renderer, state);
            break;
        case SCREEN_SETTINGS:
            draw_settings(renderer, state);
            break;
        case SCREEN_OPTIONS:
            draw_options(renderer, state);
            break;
        case SCREEN_GB_KEY_CONFIG:
        case SCREEN_N64_KEY_CONFIG:
        case SCREEN_UTIL_KEY_CONFIG:
            integral_client_ui_draw_key_config(renderer, state);
            break;
        case SCREEN_ROM_REGISTER:
            integral_client_ui_draw_rom_register(renderer, state);
            break;
    }
}

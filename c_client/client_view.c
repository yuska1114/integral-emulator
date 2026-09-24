/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_view.h"
#include "client_file_io.h"
#include "client_app.h"
#include "client_local.h"
#include "client_runtime_support.h"
#include "client_version.h"
#include "client_ui_account.h"
#include "client_ui_menu.h"
#include "client_rom_catalog.h"
#include "sdl_text.h"
#include "sdl_unicode_text.h"
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL.h>


#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#define INTEGRAL_WINDOW_HEIGHT 480
#define INTEGRAL_GB_RUNTIME_MODE_ROWS 3
#define INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS 3
#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

static void mask_password(const char *password, char *out, size_t out_size);
static void draw_header(SDL_Renderer *renderer, const char *subtitle, const char *login_id, const char *server_url);
static void draw_login(SDL_Renderer *renderer, const LoginState *state);
static void draw_password_change(SDL_Renderer *renderer, const AppState *app);
static IntegralClientMenuView menu_view(const AppState *state, unsigned selected);
static void draw_main_menu(SDL_Renderer *renderer, const AppState *state);
static void draw_room_mode(SDL_Renderer *renderer, const AppState *state);
static void draw_join_room(SDL_Renderer *renderer, const AppState *state);
static void format_local_slot_label(const AppState *state, unsigned local_slot, const char *label, char *out, size_t out_size);
static void format_local_slot_detail(const AppState *state, unsigned local_slot, char *out, size_t out_size);
static void format_rom_filename_with_header(const IntegralConfigRomSlot *slot, char *out, size_t out_size);
static const char *room_link_save_label(IntegralRoomLinkMode mode);
static void format_link_room_slot(const AppState *state,
                                  const IntegralApiRoom *room,
                                  unsigned user_index,
                                  bool local_user,
                                  char *label_out,
                                  size_t label_out_size,
                                  char *detail_out,
                                  size_t detail_out_size);
static void draw_local_mode(SDL_Renderer *renderer, const AppState *state);
static void draw_gb_slot_screen(SDL_Renderer *renderer, const AppState *state, bool mobile_mode);
static void draw_local(SDL_Renderer *renderer, const AppState *state);
static void draw_gb_mobile(SDL_Renderer *renderer, const AppState *state);
static void draw_n64_runtime(SDL_Renderer *renderer, const AppState *state);
static void draw_room(SDL_Renderer *renderer, const AppState *state);
static void format_n64_room_selection_line(char *label_out,
                                           size_t label_out_size,
                                           char *detail_out,
                                           size_t detail_out_size,
                                           const char *owner,
                                           const char *system,
                                           const char *slot,
                                           const char *filename,
                                           const char *header_title,
                                           bool local_user);
static void draw_n64_room(SDL_Renderer *renderer, const AppState *state);
static void draw_key_config(SDL_Renderer *renderer, const AppState *state);
static unsigned format_rom_slot_summary(const IntegralConfigRomSlot *slot,
                                        const IntegralApiRomSlot *server_slot,
                                        char *out,
                                        size_t out_size);
static void draw_rom_register(SDL_Renderer *renderer, const AppState *state);

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


static void format_rom_filename_with_header(const IntegralConfigRomSlot *slot, char *out, size_t out_size)
{
    if (!slot) {
        out[0] = '\0';
        return;
    }

    IntegralRomMetadata header = {0};
    if (read_supported_rom_header(slot->rom_path, &header) == 0 && header.header_title[0] != '\0') {
        snprintf(out, out_size, "%s (%s)", path_file_name(slot->rom_path), header.header_title);
        return;
    }
    copy_text(out, out_size, path_file_name(slot->rom_path));
}


static void format_local_slot_detail(const AppState *state, unsigned local_slot, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = local_selected_rom_slot(state, local_slot);
    if (!slot) {
        copy_text(out, out_size, "LEFT/RIGHT SELECT ROM1-8");
        return;
    }
    format_rom_filename_with_header(slot, out, out_size);
}


static const char *room_link_save_label(IntegralRoomLinkMode mode)
{
    switch (mode) {
        case INTEGRAL_ROOM_MODE_BATTLE:
            return "SAVE OFF";
        case INTEGRAL_ROOM_MODE_TRADE:
            return "SAVE ON";
    }
    return "SAVE ON";
}


static void format_link_room_slot(const AppState *state,
                                  const IntegralApiRoom *room,
                                  unsigned user_index,
                                  bool local_user,
                                  char *label_out,
                                  size_t label_out_size,
                                  char *detail_out,
                                  size_t detail_out_size)
{
    const char *slot_name = "";
    const char *filename = "";
    const char *header_title = "";

    if (room) {
        if (user_index == 0u) {
            slot_name = room->slot1;
            filename = room->slot_filename1;
            header_title = room->slot_header_title1;
        }
        else {
            slot_name = room->slot2;
            filename = room->slot_filename2;
            header_title = room->slot_header_title2;
        }
    }

    const IntegralConfigRomSlot *local_slot = NULL;
    char local_slot_name[16] = "";
    if (local_user) {
        local_slot = registered_rom_slot_at(state, state->room.link.room_slot_index);
        if (local_slot) {
            snprintf(local_slot_name,
                     sizeof(local_slot_name),
                     "ROM%d",
                     state->room.link.room_slot_index + 1);
            slot_name = local_slot_name;
        }
    }

    snprintf(label_out,
             label_out_size,
             "USER%u GB SLOT : %s",
             user_index + 1u,
             slot_name && slot_name[0] ? slot_name : "<EMPTY>");

    detail_out[0] = '\0';
    if (local_user) {
        if (local_slot) {
            format_rom_filename_with_header(local_slot, detail_out, detail_out_size);
        }
        else if (filename && filename[0]) {
            if (header_title && header_title[0]) {
                snprintf(detail_out,
                         detail_out_size,
                         "%s (%s)",
                         path_file_name(filename),
                         header_title);
            }
            else {
                copy_text(detail_out, detail_out_size, path_file_name(filename));
            }
        }
    }
    else if (header_title && header_title[0]) {
        copy_text(detail_out, detail_out_size, header_title);
    }
}


static void draw_local_mode(SDL_Renderer *renderer, const AppState *state)
{
    IntegralClientMenuView view = menu_view(state, state->local.local_mode_selected);
    integral_client_ui_draw_local_mode(renderer, &view);
}


static void draw_gb_slot_screen(SDL_Renderer *renderer, const AppState *state, bool mobile_mode)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

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
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 10, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 58};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
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
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "N64 MODE", state->login.username, state->login.server);

    char n64_label[240];
    char n64_detail[240] = "";
    const IntegralConfigRomSlot *n64_slot = selected_n64_rom_slot(state);
    if (n64_slot) {
        snprintf(n64_label,
                 sizeof(n64_label),
                 "N64 SLOT ROM%d",
                 state->local.integral_n64_runtime_n64_slot_index + 1);
        format_rom_filename_with_header(n64_slot, n64_detail, sizeof(n64_detail));
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
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 38};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 2, ">", 2, selected);
        }
        integral_client_ui_draw_text_fit(renderer, 48, y, labels[i], 2, state->local.integral_n64_runtime_selected == i ? selected : label, 390);
        if (i == 1 && n64_slot) {
            integral_client_ui_draw_text_fit(renderer, 48, y + 24, n64_detail, 1, value, 390);
        }
        else if (i >= 2) {
            const IntegralConfigRomSlot *slot = selected_transfer_rom_slot(state, i - 2);
            if (slot) {
                char slot_detail[240];
                format_rom_filename_with_header(slot, slot_detail, sizeof(slot_detail));
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


void format_room_phase(const AppState *state,
                              const IntegralApiRoom *room,
                              int ready1,
                              int ready2,
                              char *out,
                              size_t out_size)
{
    if (room_status_is_actionable_error(state->login.status)) {
        copy_text(out, out_size, state->login.status);
    }
    else if (current_room_game_ended(&state->room)) {
        copy_text(out, out_size, "GAME ENDED");
    }
    else if (state->room.link.room_client_started) {
        if (state->room.common.room_remaining_seconds >= 0) {
            long long minutes = state->room.common.room_remaining_seconds / 60;
            long long seconds = state->room.common.room_remaining_seconds % 60;
            snprintf(out,
                     out_size,
                     "%s RUNNING %02lld:%02lld%s",
                     room_link_mode_label(state->room.link.room_link_mode),
                     minutes,
                     seconds,
                     state->room.common.room_remaining_seconds <= 120 ? " 2MIN WARNING" :
                     (state->room.common.room_remaining_seconds <= 600 ? " 10MIN WARNING" : ""));
        }
        else {
            snprintf(out, out_size, "%s RUNNING", room_link_mode_label(state->room.link.room_link_mode));
        }
    }
    else if (strcmp(state->login.status, "CONNECTING") == 0) {
        copy_text(out, out_size, "CONNECTING");
    }
    else if (strcmp(state->login.status, "SERVER STARTING") == 0) {
        copy_text(out, out_size, "SERVER STARTING");
    }
    else if (state->room.link.room_link_session_id[0] != '\0') {
        copy_text(out, out_size, "CONNECTING");
    }
    else if ((room && ready1 && ready2) || (state->room.common.room_ready_self && state->room.common.room_ready_peer)) {
        copy_text(out, out_size, "BOTH OK");
    }
    else if (state->room.common.room_ready_self) {
        copy_text(out, out_size, "WAIT PEER");
    }
    else {
        copy_text(out, out_size, "ROOM");
    }
}


static void draw_room(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color cursor = {230, 92, 76, 255};
    SDL_Color muted = {112, 122, 130, 255};

    char subtitle[32];
    const IntegralApiRoom *header_room = state->room.common.room_number >= 1 && state->room.common.room_number <= INTEGRAL_API_ROOMS
                                              ? &state->room.common.current_room : NULL;
    snprintf(subtitle, sizeof(subtitle), "ROOM CODE %s",
             header_room && header_room->room_code[0] ? header_room->room_code : "-----");
    draw_header(renderer, subtitle, state->login.username, state->login.server);

    const IntegralApiRoom *room = NULL;
    if (state->room.common.room_number >= 1 && state->room.common.room_number <= INTEGRAL_API_ROOMS) {
        room = &state->room.common.current_room;
    }
    const char *user1 = room && room->user1[0] ? room->user1 : (state->login.username[0] ? state->login.username : "USER1");
    const char *user2 = room && room->user2[0] ? room->user2 : "";
    int ready1 = room ? room->ready1 : 0;
    int ready2 = room ? room->ready2 : 0;
    char user_line[128];
    snprintf(user_line,
             sizeof(user_line),
             "USER1 : %s%s",
             user1,
             ready1 ? " READY" : "");
    integral_client_ui_draw_text_fit(renderer, 48, 104, user_line, 2, value, 380);
    snprintf(user_line,
             sizeof(user_line),
             "USER2 : %s%s",
             user2[0] ? user2 : "<EMPTY>",
             ready2 ? " READY" : "");
    integral_client_ui_draw_text_fit(renderer, 48, 140, user_line, 2, value, 380);

    char room_phase[160];
    format_room_phase(state, room, ready1, ready2, room_phase, sizeof(room_phase));
    bool game_ended = current_room_game_ended(&state->room);
    bool local_is_user1 = current_room_is_user1(&state->room);

    char slot_labels[2][96];
    char slot_details[2][260];
    format_link_room_slot(state,
                          room,
                          0u,
                          local_is_user1,
                          slot_labels[0],
                          sizeof(slot_labels[0]),
                          slot_details[0],
                          sizeof(slot_details[0]));
    format_link_room_slot(state,
                          room,
                          1u,
                          !local_is_user1,
                          slot_labels[1],
                          sizeof(slot_labels[1]),
                          slot_details[1],
                          sizeof(slot_details[1]));

    const char *labels[] = {
        "",
        "",
        "",
        "CHAT LOG",
        "CHAT INPUT",
    };
    const char *details[] = {
        "",
        "",
        "",
        "LEFT/RIGHT SCROLL",
        state->room.common.room_chat_editing ? "TEXT INPUT ACTIVE" : "ENTER EDIT",
    };

    const int slot_y[2] = {170, 190};
    unsigned local_slot_row = local_is_user1 ? 0u : 1u;
    if (state->room.common.room_selected == 0) {
        int y = slot_y[local_slot_row];
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = y - 4, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 18};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, y, ">", 1, selected);
    }

    for (unsigned i = 0; i < 2; i++) {
        bool is_local = (i == local_slot_row);
        SDL_Color slot_label_color =
            is_local && state->room.common.room_selected == 0 ? selected :
            (is_local ? label : muted);
        SDL_Color slot_detail_color = is_local ? value : muted;
        integral_client_ui_draw_text_fit(renderer,
                                         58,
                                         slot_y[i],
                                         slot_labels[i],
                                         1,
                                         slot_label_color,
                                         166);
        if (slot_details[i][0] != '\0') {
            integral_client_ui_draw_text_fit(renderer,
                                             230,
                                             slot_y[i],
                                             slot_details[i],
                                             1,
                                             slot_detail_color,
                                             210);
        }
    }

    int save_y = 210;
    if (state->room.common.room_selected == 1) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = save_y - 4, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 18};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, save_y, ">", 1, selected);
    }
    integral_client_ui_draw_text_fit(renderer,
                                     58,
                                     save_y,
                                     room_link_save_label(state->room.link.room_link_mode),
                                     1,
                                     state->room.common.room_selected == 1 ? selected : label,
                                     166);
    integral_client_ui_draw_text_fit(renderer,
                                     230,
                                     save_y,
                                     game_ended ? "LOCKED" :
                                     (local_is_user1 ? "LEFT/RIGHT SELECT" : "USER1 SELECTS"),
                                     1,
                                     muted,
                                     210);

    int ready_y = 230;
    if (state->room.common.room_selected == 2) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = ready_y - 4, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 18};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, ready_y, ">", 1, selected);
    }
    integral_client_ui_draw_text_fit(renderer,
                                     58,
                                     ready_y,
                                     game_ended ? "GAME ENDED" :
                                     (state->room.common.room_ready_self ? "READY OK" : "READY"),
                                     1,
                                     state->room.common.room_selected == 2 ? selected : label,
                                     166);
    integral_client_ui_draw_text_fit(renderer,
                                     230,
                                     ready_y,
                                     room_phase,
                                     1,
                                     muted,
                                     210);

    int log_y = 276;
    if (state->room.common.room_selected == 3) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = log_y - 22, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 122};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, log_y - 12, ">", 2, selected);
    }
    integral_sdl_draw_text(renderer, 48, log_y - 16, labels[3], 1, state->room.common.room_selected == 3 ? selected : label);
    integral_sdl_draw_text(renderer, 140, log_y - 16, details[3], 1, muted);
    integral_client_ui_draw_panel(renderer, 22, log_y + 2, INTEGRAL_WINDOW_WIDTH - 44, 100, (SDL_Color){55, 64, 70, 255});
    unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room.common.room_chat_log);
    unsigned start = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
    if (state->room.common.room_chat_scroll > 0 && message_count > INTEGRAL_CHAT_VISIBLE_LINES) {
        unsigned max_scroll = message_count - INTEGRAL_CHAT_VISIBLE_LINES;
        unsigned offset = state->room.common.room_chat_scroll > max_scroll ? max_scroll : state->room.common.room_chat_scroll;
        start = message_count - INTEGRAL_CHAT_VISIBLE_LINES - offset;
    }
    unsigned seen = 0;
    unsigned drawn = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES && drawn < INTEGRAL_CHAT_VISIBLE_LINES; i++) {
        if (state->room.common.room_chat_log[i][0] == '\0') {
            continue;
        }
        if (seen++ < start) {
            continue;
        }
        integral_sdl_draw_utf8_text(renderer, 34, log_y + 8 + (int)drawn * 18, state->room.common.room_chat_log[i], 14, value, INTEGRAL_WINDOW_WIDTH - 68);
        drawn++;
    }
    if (message_count == 0) {
        integral_sdl_draw_text(renderer, 34, log_y + 42, "NO MESSAGES", 1, muted);
    }

    int input_y = 402;
    if (state->room.common.room_selected == 4) {
        if (state->room.common.room_chat_editing) {
            SDL_SetRenderDrawColor(renderer, 112, 38, 44, 255);
        }
        else {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        }
        SDL_Rect rect = {.x = 14, .y = input_y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 36};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, input_y + 1, ">", 2, selected);
    }
    integral_sdl_draw_text(renderer, 48, input_y, labels[4], 1, state->room.common.room_selected == 4 ? selected : label);
    integral_client_ui_draw_panel(renderer, 142, input_y - 6, INTEGRAL_WINDOW_WIDTH - 164, 30, (SDL_Color){55, 64, 70, 255});
    char chat_display[INTEGRAL_CHAT_MESSAGE_MAX * 2];
    if (state->room.common.room_chat_input[0] || state->room.common.room_chat_composition[0]) {
        snprintf(chat_display,
                 sizeof(chat_display),
                 "%s%s",
                 state->room.common.room_chat_input,
                 state->room.common.room_chat_composition);
    }
    else {
        copy_text(chat_display, sizeof(chat_display), "<EMPTY>");
    }
    integral_sdl_draw_utf8_text(renderer, 154, input_y - 1, chat_display, 14, value, INTEGRAL_WINDOW_WIDTH - 202);
    if (state->room.common.room_chat_composition[0]) {
        integral_sdl_draw_utf8_text(renderer, 154, input_y + 14, state->room.common.room_chat_composition, 12, selected, INTEGRAL_WINDOW_WIDTH - 202);
    }
    if (state->room.common.room_chat_editing) {
        SDL_SetRenderDrawColor(renderer, cursor.r, cursor.g, cursor.b, cursor.a);
        SDL_Rect cursor_rect = {.x = INTEGRAL_WINDOW_WIDTH - 42, .y = input_y - 2, .w = 3, .h = 22};
        SDL_RenderFillRect(renderer, &cursor_rect);
    }

    char footer_status[160];
    if (state->room.common.room_chat_editing) {
        copy_text(footer_status, sizeof(footer_status), "CHAT INPUT ACTIVE");
    }
    else {
        copy_text(footer_status, sizeof(footer_status), room_phase);
        if (game_ended) {
            copy_text(footer_status, sizeof(footer_status), "GAME ENDED  ESC MAIN MENU");
        }
    }
    integral_client_ui_draw_text_fit(renderer, 22, 438, footer_status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer,
                      22,
                      456,
                      game_ended ? "TAB MOVE  ENTER CHAT/SEND  LEFT/RIGHT LOG  ESC MAIN"
                                 : "TAB MOVE  ENTER EDIT/SEND  LEFT/RIGHT SLOT/MODE/LOG  ESC MAIN",
                      1,
                      muted);
    SDL_RenderPresent(renderer);
}


static void format_n64_room_selection_line(char *label_out,
                                           size_t label_out_size,
                                           char *detail_out,
                                           size_t detail_out_size,
                                           const char *owner,
                                           const char *system,
                                           const char *slot,
                                           const char *filename,
                                           const char *header_title,
                                           bool local_user)
{
    if (!slot || !slot[0]) {
        snprintf(label_out, label_out_size, "%s %s SLOT : <EMPTY>", owner, system);
        detail_out[0] = '\0';
        return;
    }

    snprintf(label_out, label_out_size, "%s %s SLOT : %s", owner, system, slot);
    if (local_user) {
        if (filename && filename[0] && header_title && header_title[0]) {
            snprintf(detail_out, detail_out_size, "%s (%s)", path_file_name(filename), header_title);
        }
        else if (filename && filename[0]) {
            copy_text(detail_out, detail_out_size, path_file_name(filename));
        }
        else if (header_title && header_title[0]) {
            copy_text(detail_out, detail_out_size, header_title);
        }
        else {
            copy_text(detail_out, detail_out_size, "<FILE UNKNOWN>");
        }
        return;
    }

    copy_text(detail_out,
              detail_out_size,
              header_title && header_title[0] ? header_title : "HEADER UNKNOWN");
}


static void draw_n64_room(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    if (state->room.n64.n64_runtime_media_paired && strcmp(state->room.n64.n64_runtime_media_role, "remote") == 0) {
        SDL_Rect video_bounds = {.x = 0, .y = 0, .w = INTEGRAL_WINDOW_WIDTH, .h = INTEGRAL_WINDOW_HEIGHT};
        (void)integral_n64_runtime_media_stream_render(state->room.n64.n64_runtime_media_stream,
                                                renderer,
                                                &video_bounds);
    }

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color warning = {236, 142, 108, 255};
    char subtitle[32];
    const IntegralApiRoom *header_room = state->room.common.room_number >= 65 && state->room.common.room_number <= 128
                                              ? &state->room.common.current_room : NULL;
    snprintf(subtitle, sizeof(subtitle), "ROOM CODE %s",
             header_room && header_room->room_code[0] ? header_room->room_code : "-----");
    draw_header(renderer, subtitle, state->login.username, state->login.server);

    const IntegralApiRoom *room = NULL;
    if (state->room.common.room_number >= 65 && state->room.common.room_number <= 128) {
        room = &state->room.common.current_room;
    }
    char user_line[128];
    snprintf(user_line,
             sizeof(user_line),
             "USER1 : %s",
             room && room->user1[0] ? room->user1 : "PLAYER001");
    integral_client_ui_draw_text_fit(renderer, 48, 88, user_line, 2, value, 380);
    snprintf(user_line,
             sizeof(user_line),
             "USER2 : %s",
             room && room->user2[0] ? room->user2 : "<EMPTY>");
    integral_client_ui_draw_text_fit(renderer, 48, 116, user_line, 2, value, 380);

    char n64_label[96];
    char n64_detail[288];
    char user1_gb_label[96];
    char user1_gb_detail[288];
    char user2_gb_label[96];
    char user2_gb_detail[288];
    const char *n64_slot = room && room->n64_slot1[0] ? room->n64_slot1 : "";
    const char *n64_filename = room ? room->n64_slot_filename1 : "";
    const char *n64_header_title = room ? room->n64_slot_header_title1 : "";
    const char *user1_gb_slot = room && room->slot1[0] ? room->slot1 : "";
    const char *user1_gb_filename = room ? room->slot_filename1 : "";
    const char *user1_gb_header_title = room ? room->slot_header_title1 : "";
    const char *user2_gb_slot = room && room->slot2[0] ? room->slot2 : "";
    const char *user2_gb_filename = room ? room->slot_filename2 : "";
    const char *user2_gb_header_title = room ? room->slot_header_title2 : "";

    char fallback_n64_slot[16] = "";
    char fallback_user1_slot[16] = "";
    char fallback_user2_slot[16] = "";
    IntegralRomMetadata fallback_header;
    int local_user_index = n64_room_local_user_index(&state->room);
    if (!n64_slot[0] && local_user_index == 0 && state->room.n64.n64_room_n64_slot_index >= 0) {
        snprintf(fallback_n64_slot, sizeof(fallback_n64_slot), "ROM%d", state->room.n64.n64_room_n64_slot_index + 1);
        n64_slot = fallback_n64_slot;
        n64_filename = state->catalog.rom_slots[state->room.n64.n64_room_n64_slot_index].rom_path;
        if (read_supported_rom_header(n64_filename, &fallback_header) == 0) {
            n64_header_title = fallback_header.header_title;
        }
    }
    if (!user1_gb_slot[0] && local_user_index == 0 && state->room.n64.n64_room_user1_gb_slot_index >= 0) {
        snprintf(fallback_user1_slot, sizeof(fallback_user1_slot), "ROM%d", state->room.n64.n64_room_user1_gb_slot_index + 1);
        user1_gb_slot = fallback_user1_slot;
        user1_gb_filename = state->catalog.rom_slots[state->room.n64.n64_room_user1_gb_slot_index].rom_path;
        if (read_supported_rom_header(user1_gb_filename, &fallback_header) == 0) {
            user1_gb_header_title = fallback_header.header_title;
        }
    }
    if (!user2_gb_slot[0] && local_user_index == 1 && state->room.n64.n64_room_user2_gb_slot_index >= 0) {
        snprintf(fallback_user2_slot, sizeof(fallback_user2_slot), "ROM%d", state->room.n64.n64_room_user2_gb_slot_index + 1);
        user2_gb_slot = fallback_user2_slot;
        user2_gb_filename = state->catalog.rom_slots[state->room.n64.n64_room_user2_gb_slot_index].rom_path;
        if (read_supported_rom_header(user2_gb_filename, &fallback_header) == 0) {
            user2_gb_header_title = fallback_header.header_title;
        }
    }

    bool user1_is_local = local_user_index == 0;
    bool user2_is_local = local_user_index == 1;
    format_n64_room_selection_line(n64_label, sizeof(n64_label), n64_detail, sizeof(n64_detail),
                                   "USER1", "N64", n64_slot, n64_filename, n64_header_title, user1_is_local);
    format_n64_room_selection_line(user1_gb_label, sizeof(user1_gb_label), user1_gb_detail, sizeof(user1_gb_detail),
                                   "USER1", "GB", user1_gb_slot, user1_gb_filename, user1_gb_header_title, user1_is_local);
    format_n64_room_selection_line(user2_gb_label, sizeof(user2_gb_label), user2_gb_detail, sizeof(user2_gb_detail),
                                   "USER2", "GB", user2_gb_slot, user2_gb_filename, user2_gb_header_title, user2_is_local);

    const char *labels[INTEGRAL_N64_RUNTIME_ROOM_ROWS] = {
        n64_label,
        user1_gb_label,
        user2_gb_label,
        state->room.n64.n64_room_ready ? "READY OK" : "READY",
        "CHAT LOG",
    };
    const char *details[INTEGRAL_N64_RUNTIME_ROOM_ROWS] = {
        n64_detail,
        user1_gb_detail,
        user2_gb_detail,
        "ENTER READY",
        "LEFT/RIGHT SCROLL",
    };
    const bool rom_row_is_local[3] = {user1_is_local, user1_is_local, user2_is_local};
    const int rom_y[3] = {150, 174, 198};

    for (unsigned i = 0; i < 3; i++) {
        int y = rom_y[i];
        if (state->room.common.room_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 5, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 20};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 1, selected);
        }
        integral_client_ui_draw_text_fit(renderer, 48, y, labels[i], 1,
                                         state->room.common.room_selected == i ? selected : value, 176);
        if (details[i][0] != '\0') {
            integral_client_ui_draw_text_fit(renderer, 230, y, details[i], 1,
                                             rom_row_is_local[i] ? value : muted, 210);
        }
    }

    const int save_y = 222;
    integral_client_ui_draw_text_fit(renderer, 48, save_y, "SAVE OFF", 1, label, 176);
    integral_client_ui_draw_text_fit(renderer, 240, save_y, "SAVE OFF ONLY", 1, muted, 200);

    const int ready_y = 246;
    if (state->room.common.room_selected == 3) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = ready_y - 5, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 20};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, ready_y, ">", 1, selected);
    }
    integral_client_ui_draw_text_fit(renderer, 48, ready_y, labels[3], 1,
                                     state->room.common.room_selected == 3 ? selected : label, 190);
    integral_client_ui_draw_text_fit(renderer, 240, ready_y, details[3], 1, muted, 200);

    int log_y = 294;
    if (state->room.common.room_selected == 4) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = log_y - 22, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 122};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, log_y - 12, ">", 2, selected);
    }
    integral_sdl_draw_text(renderer, 48, log_y - 16, labels[4], 1, state->room.common.room_selected == 4 ? selected : label);
    integral_sdl_draw_text(renderer, 140, log_y - 16, details[4], 1, muted);
    integral_client_ui_draw_panel(renderer, 22, log_y + 2, INTEGRAL_WINDOW_WIDTH - 44, 100, (SDL_Color){55, 64, 70, 255});
    unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room.common.room_chat_log);
    unsigned start = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
    if (state->room.common.room_chat_scroll > 0 && message_count > INTEGRAL_CHAT_VISIBLE_LINES) {
        unsigned max_scroll = message_count - INTEGRAL_CHAT_VISIBLE_LINES;
        unsigned offset = state->room.common.room_chat_scroll > max_scroll ? max_scroll : state->room.common.room_chat_scroll;
        start = message_count - INTEGRAL_CHAT_VISIBLE_LINES - offset;
    }
    unsigned seen = 0;
    unsigned drawn = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES && drawn < INTEGRAL_CHAT_VISIBLE_LINES; i++) {
        if (state->room.common.room_chat_log[i][0] == '\0') {
            continue;
        }
        if (seen++ < start) {
            continue;
        }
        integral_sdl_draw_utf8_text(renderer, 34, log_y + 8 + (int)drawn * 18, state->room.common.room_chat_log[i], 14, value, INTEGRAL_WINDOW_WIDTH - 68);
        drawn++;
    }
    if (message_count == 0) {
        integral_sdl_draw_text(renderer, 34, log_y + 42, "NO MESSAGES", 1, muted);
    }

    bool status_is_warning = strstr(state->login.status, "FAILED") != NULL ||
                             strstr(state->login.status, "REQUIRED") != NULL ||
                             strstr(state->login.status, "INVALID") != NULL ||
                             strstr(state->login.status, "BLOCKED") != NULL ||
                             strstr(state->login.status, "ERROR") != NULL;
    char n64_status[192];
    if (!status_is_warning && state->room.common.room_remaining_seconds >= 0 && state->room.n64.n64_runtime_media_authenticated) {
        snprintf(n64_status,
                 sizeof(n64_status),
                 "%s  REMAIN %02lld:%02lld%s",
                 state->login.status,
                 state->room.common.room_remaining_seconds / 60,
                 state->room.common.room_remaining_seconds % 60,
                 state->room.common.room_remaining_seconds <= 120 ? " 2MIN WARNING" :
                 (state->room.common.room_remaining_seconds <= 600 ? " 10MIN WARNING" : ""));
    }
    else {
        copy_text(n64_status, sizeof(n64_status), state->login.status);
    }
    integral_client_ui_draw_text_fit(renderer,
                          22,
                          410,
                          n64_status,
                          1,
                          status_is_warning ? warning : muted,
                          INTEGRAL_WINDOW_WIDTH - 44);
    integral_client_ui_draw_text_fit(renderer, 22, 438, "TRANSFER PAK  NO SAV OVERWRITE", 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 456, "TAB MOVE  LEFT/RIGHT ROM1-8  ENTER READY  ESC MAIN", 1, muted);
    SDL_RenderPresent(renderer);
}


void draw_runtime_exit_confirmation(SDL_Renderer *renderer,
                                           const AppState *state)
{
    if (!state->room.n64.runtime_exit_confirming) return;
    SDL_Rect panel = {.x = INTEGRAL_WINDOW_WIDTH / 2 - 210,
                      .y = INTEGRAL_WINDOW_HEIGHT / 2 - 70,
                      .w = 420,
                      .h = 140};
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color text = {185, 205, 216, 255};
    SDL_Color selected = {86, 220, 150, 255};
    SDL_SetRenderDrawColor(renderer, 10, 14, 18, 245);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 86, 162, 126, 255);
    SDL_RenderDrawRect(renderer, &panel);
    integral_sdl_draw_text(renderer, panel.x + 36, panel.y + 28,
                           "EXIT GAME?", 3, title);
    integral_sdl_draw_text(renderer, panel.x + 86, panel.y + 84,
                           state->room.n64.runtime_exit_confirm_yes ? "> YES" : "  YES", 3,
                           state->room.n64.runtime_exit_confirm_yes ? selected : text);
    integral_sdl_draw_text(renderer, panel.x + 244, panel.y + 84,
                           state->room.n64.runtime_exit_confirm_yes ? "  NO" : "> NO", 3,
                           state->room.n64.runtime_exit_confirm_yes ? text : selected);
    SDL_RenderPresent(renderer);
}


static void draw_key_config(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "KEY CONFIG", state->login.username, state->login.server);
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
    format_slot_key_summary(state->keys.slot1, slot1_line1, sizeof(slot1_line1), slot1_line2, sizeof(slot1_line2));
    format_slot_key_summary(state->keys.slot2, slot2_line1, sizeof(slot2_line1), slot2_line2, sizeof(slot2_line2));
    format_n64_key_summary(state->keys.n64_p1,
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

    const char *labels[] = {"SLOT 1 KEYS", "SLOT 2 KEYS", "N64 KEYS", "UTIL KEYS", "RESET DEFAULTS"};
    const char *details1[] = {slot1_line1, slot2_line1, n64_line1, util_line1, "RESTORE DEFAULTS"};
    const char *details2[] = {slot1_line2, slot2_line2, n64_line2, util_line2, ""};
    const char *details3[] = {"", "", n64_line3, util_line3, ""};

    for (unsigned i = 0; i < INTEGRAL_KEY_ROWS; i++) {
        int y = 86 + (int)i * 58;
        if (state->key_editor.key_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 56};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 6, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y, labels[i], 2, state->key_editor.key_selected == i ? selected : label);
        integral_client_ui_draw_text_fit(renderer, 48, y + 22, details1[i], 1, value, 400);
        if (details2[i][0] != '\0') {
            integral_client_ui_draw_text_fit(renderer, 48, y + 36, details2[i], 1, value, 400);
        }
        if (details3[i][0] != '\0') {
            integral_client_ui_draw_text_fit(renderer, 48, y + 50, details3[i], 1, value, 400);
        }
    }

    integral_sdl_draw_text(renderer, 22, 365, "FAST / TURBO: LOCAL GB 1P ONLY", 2, muted);
    integral_sdl_draw_text(renderer, 22, 383, "RESET: LOCAL MODES", 2, muted);
    integral_sdl_draw_text(renderer, 22, 401, "SCREENSHOT / ESCAPE: ALL MODES", 2, muted);

    if (state->key_editor.key_capture_target != KEY_CAPTURE_NONE) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 42, .y = 320, .w = 396, .h = 88};
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

    integral_client_ui_draw_text_fit(renderer, 22, 424, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER CONFIGURE  USB / BLUETOOTH", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC BACK / CANCEL", 1, muted);
    SDL_RenderPresent(renderer);
}


enum {
    ROM_SLOT_WARNING_SERVER_UNREGISTERED = 1u << 0,
    ROM_SLOT_WARNING_LOCAL_NOT_FOUND = 1u << 1,
};


static unsigned format_rom_slot_summary(const IntegralConfigRomSlot *slot,
                                        const IntegralApiRomSlot *server_slot,
                                        char *out,
                                        size_t out_size)
{
    const char *filename = slot->rom_path[0] ? path_file_name(slot->rom_path) : "";
    bool registered = slot_has_server_registration(slot);
    bool local_found = slot->rom_path[0] != '\0' && local_file_exists(slot->rom_path);

    /*
     * A pending local replacement deliberately has its local IDs cleared for
     * presentation, so do not let the previous server slot make it look
     * registered. Only fall back to server registration when there is no local
     * path at all.
     */
    if (!registered && slot->rom_path[0] == '\0' && server_slot &&
        server_slot->rom_id[0] != '\0' && server_slot->save_id[0] != '\0') {
        registered = true;
    }

    if (!filename[0] && server_slot && server_slot->filename[0]) {
        filename = server_slot->filename;
    }

    bool has_slot = slot->rom_path[0] != '\0' ||
                    (server_slot && (server_slot->filename[0] != '\0' ||
                                     server_slot->rom_id[0] != '\0' ||
                                     server_slot->save_id[0] != '\0'));
    if (!has_slot) {
        copy_text(out, out_size, "<EMPTY>");
        return 0u;
    }

    IntegralRomMetadata header = {0};
    const char *header_title = "";
    if (local_found && read_supported_rom_header(slot->rom_path, &header) == 0) {
        header_title = header.header_title;
    }
    else if (registered && server_slot && server_slot->rom_header_title[0] != '\0') {
        header_title = server_slot->rom_header_title;
    }

    if (filename[0] && header_title[0]) {
        snprintf(out, out_size, "%s (%s)", filename, header_title);
    }
    else if (filename[0]) {
        copy_text(out, out_size, filename);
    }
    else if (header_title[0]) {
        snprintf(out, out_size, "SERVER ROM (%s)", header_title);
    }
    else {
        copy_text(out, out_size, "SERVER ROM");
    }

    unsigned warnings = 0u;
    if (!registered) {
        warnings |= ROM_SLOT_WARNING_SERVER_UNREGISTERED;
    }
    if (!local_found) {
        warnings |= ROM_SLOT_WARNING_LOCAL_NOT_FOUND;
    }
    return warnings;
}

static void draw_rom_register(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color warning = {230, 92, 76, 255};

    draw_header(renderer, "ROM REGISTER", state->login.username, state->login.server);
    integral_sdl_draw_text(renderer, 24, 88, "MAX 8 ROMS  SAV IS SERVER MANAGED", 1, muted);

    for (unsigned i = 0; i < INTEGRAL_ROM_ROWS; i++) {
        int y = 112 + (int)i * 27;
        if (state->catalog.rom_editor.rom_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 6, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 27};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
        }
        if (i < INTEGRAL_ROM_SLOTS) {
            char label_text[24];
            char summary[320];
            snprintf(label_text, sizeof(label_text), "ROM%u", i + 1);
            IntegralConfigRomSlot display_slot = state->catalog.rom_slots[i];
            if (state->catalog.rom_editor.pending_paths[i] &&
                strcmp(display_slot.rom_path, state->catalog.rom_editor.confirmed_slots[i].rom_path) != 0) {
                /* Presentation only: the authoritative IDs remain intact. */
                display_slot.rom_id[0] = '\0';
                display_slot.save_id[0] = '\0';
            }
            unsigned warnings =
                format_rom_slot_summary(&display_slot, &state->catalog.server_rom_slots[i], summary, sizeof(summary));
            char warning_text[64] = "";
            if ((warnings & ROM_SLOT_WARNING_SERVER_UNREGISTERED) &&
                (warnings & ROM_SLOT_WARNING_LOCAL_NOT_FOUND)) {
                copy_text(warning_text, sizeof(warning_text), "SERVER UNREGISTERED  LOCAL NOT FOUND");
            }
            else if (warnings & ROM_SLOT_WARNING_SERVER_UNREGISTERED) {
                copy_text(warning_text, sizeof(warning_text), "SERVER UNREGISTERED");
            }
            else if (warnings & ROM_SLOT_WARNING_LOCAL_NOT_FOUND) {
                copy_text(warning_text, sizeof(warning_text), "LOCAL NOT FOUND");
            }

            integral_sdl_draw_text(renderer, 48, y, label_text, 2, state->catalog.rom_editor.rom_selected == i ? selected : label);

            const int detail_width = 300;
            const int warning_gap = warning_text[0] ? 12 : 0;
            int warning_width = (int)strlen(warning_text) * 6;
            int summary_width = detail_width - warning_gap - warning_width;
            if (summary_width < 6) {
                summary_width = 6;
            }
            if (state->catalog.rom_editor.rom_selected == i) {
                integral_client_ui_draw_marquee_text_fit(renderer, 146, y + 4, summary, 1, value, summary_width, i * 7u);
            }
            else {
                integral_client_ui_draw_text_fit(renderer, 146, y + 4, summary, 1, value, summary_width);
            }
            if (warning_text[0]) {
                integral_sdl_draw_text(renderer,
                                       146 + summary_width + warning_gap,
                                       y + 4,
                                       warning_text,
                                       1,
                                       warning);
            }
        }
        else if (i == INTEGRAL_ROM_EXPORT_ROW) {
            integral_sdl_draw_text(renderer, 48, y, "EXPORT", 2, state->catalog.rom_editor.rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "DOWNLOAD SAVS", 1, muted);
        }
        else {
            integral_sdl_draw_text(renderer, 48, y, "BACK", 2, state->catalog.rom_editor.rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "RETURN MENU", 1, muted);
        }
    }

    if (state->catalog.rom_editor.rom_edit_target != ROM_EDIT_NONE && state->catalog.rom_editor.rom_selected < INTEGRAL_ROM_SLOTS) {
        const IntegralConfigRomSlot *slot = &state->catalog.rom_slots[state->catalog.rom_editor.rom_selected];
        const char *label_text = state->catalog.rom_editor.rom_edit_target == ROM_EDIT_INITIAL_SAVE
                                      ? "SELECT INITIAL SAV PATH"
                                      : "EDIT ROM PATH";
        const char *value_text = state->catalog.rom_editor.rom_edit_target == ROM_EDIT_INITIAL_SAVE
                                     ? state->catalog.rom_editor.rom_initial_save_import_path
                                     : slot->rom_path;
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 240);
        SDL_Rect overlay = {.x = 42, .y = 320, .w = 396, .h = 88};
        SDL_RenderFillRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 62, 342, label_text, 2, selected);
        integral_sdl_draw_utf8_scrolled(renderer, 62, 372, value_text[0] ? value_text : "<EMPTY>",
                                       12, value, 328, 0, true);
        integral_sdl_draw_text(renderer, 394, 372, "_", 1, selected);
    }
    else if (state->catalog.rom_editor.rom_browser_active) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 86, .w = 412, .h = 322};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 106, "ROMS FOLDER", 2, selected);
        unsigned visible = state->catalog.rom_editor.rom_browser_count < 8 ? state->catalog.rom_editor.rom_browser_count : 8;
        unsigned start = 0;
        if (state->catalog.rom_editor.rom_browser_selected >= visible) {
            start = state->catalog.rom_editor.rom_browser_selected - visible + 1;
        }
        for (unsigned i = 0; i < visible; i++) {
            unsigned index = start + i;
            int y = 142 + (int)i * 28;
            if (index == state->catalog.rom_editor.rom_browser_selected) {
                SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
                SDL_Rect row = {.x = 46, .y = y - 6, .w = 388, .h = 24};
                SDL_RenderFillRect(renderer, &row);
                integral_sdl_draw_text(renderer, 56, y, ">", 1, selected);
            }
            integral_client_ui_draw_text_fit(renderer,
                                  76,
                                  y,
                                  path_file_name(state->catalog.rom_editor.rom_browser_entries[index]),
                                  1,
                                  value,
                                  340);
        }
        integral_sdl_draw_text(renderer, 54, 374, "LEFT/RIGHT MOVE  ENTER CHOOSE  ESC CLOSE", 1, muted);
    }
    else if (state->catalog.rom_editor.rom_confirm_delete) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 242, .w = 412, .h = 166};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        unsigned index = state->catalog.rom_editor.rom_selected;
        char target[160];
        snprintf(target, sizeof(target), "ACCOUNT %s / ROM%u", state->login.username, index + 1);
        integral_sdl_draw_text(renderer, 54, 252, "REPLACE ROM AND DELETE OLD SAV?", 1, selected);
        integral_client_ui_draw_text_fit(renderer, 54, 274, target, 1, value, 372);
        if (index < INTEGRAL_ROM_SLOTS) {
            char old_rom[384], new_rom[384];
            IntegralRomMetadata header = {0};
            (void)read_supported_rom_header(state->catalog.rom_slots[index].rom_path, &header);
            snprintf(old_rom, sizeof(old_rom), "%s (%s)", state->catalog.server_rom_slots[index].filename,
                     state->catalog.server_rom_slots[index].rom_header_title);
            snprintf(new_rom, sizeof(new_rom), "%s (%s)", path_file_name(state->catalog.rom_slots[index].rom_path), header.header_title);
            integral_sdl_draw_text(renderer, 54, 298, "OLD:", 1, label);
            integral_client_ui_draw_text_fit(renderer, 86, 298, old_rom, 1, value, 340);
            integral_sdl_draw_text(renderer, 54, 324, "NEW:", 1, label);
            integral_client_ui_draw_text_fit(renderer, 86, 324, new_rom, 1, value, 340);
        }
        integral_sdl_draw_text(renderer, 54, 354, "OLD SAVE WILL BE LOST", 1, selected);
        integral_sdl_draw_text(renderer, 54, 382, "ENTER YES   ESC NO", 1, value);
    }
    else if (state->catalog.rom_editor.rom_confirm_initial_save_import) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 316, .w = 412, .h = 92};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 338, "SEND SELECTED INITIAL SAV?", 2, selected);
        integral_sdl_draw_text(renderer, 54, 368, "ENTER YES   ESC NO", 1, value);
    }

    integral_client_ui_draw_text_fit(renderer, 22, 424, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    if (state->catalog.rom_editor.rom_edit_target != ROM_EDIT_NONE) {
        integral_sdl_draw_text(renderer, 22, 442, "ENTER APPLY INPUT  ESC CANCEL EDIT", 1, muted);
        integral_sdl_draw_text(renderer, 22, 460, "BACKSPACE DELETE CHAR", 1, muted);
    }
    else if (state->catalog.allow_user_initial_save_import) {
        integral_sdl_draw_text(renderer, 22, 442, "LEFT/RIGHT SET ROM  ENTER REGISTER  F2 EDIT  F4 LIST", 1, muted);
        if (state->catalog.rom_editor.rom_initial_save_import_slot >= 0) {
            char import_status[64];
            snprintf(import_status, sizeof(import_status),
                     "F5 SAV  INITIAL SAV SELECTED FOR ROM%d",
                     state->catalog.rom_editor.rom_initial_save_import_slot + 1);
            integral_sdl_draw_text(renderer, 22, 460, import_status, 1, muted);
        }
        else {
            integral_sdl_draw_text(renderer, 22, 460, "F5 SAV", 1, muted);
        }
    }
    else {
        integral_sdl_draw_text(renderer, 22, 442, "LEFT/RIGHT SET ROM  ENTER REGISTER  F2 EDIT  F4 LIST", 1, muted);
        integral_sdl_draw_text(renderer, 22, 460, "", 1, muted);
    }
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
            draw_room(renderer, state);
            break;
        case SCREEN_N64_ROOM:
            draw_n64_room(renderer, state);
            break;
        case SCREEN_KEY_CONFIG:
            draw_key_config(renderer, state);
            break;
        case SCREEN_ROM_REGISTER:
            draw_rom_register(renderer, state);
            break;
    }
}

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
#include "client_ui_room_common.h"
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


static void format_local_rom_detail(const AppState *state, int slot_index, char *out, size_t out_size)
{
    if (slot_index < 0 || slot_index >= INTEGRAL_ROM_SLOTS) {
        out[0] = '\0';
        return;
    }
    const IntegralConfigRomSlot *slot = &state->catalog.rom_slots[slot_index];
    const IntegralClientRomMetadataCache *cached = client_rom_metadata_for_slot(state, slot_index);
    if (cached && cached->header_valid && cached->metadata.header_title[0]) {
        snprintf(out, out_size, "%s (%s)", path_file_name(slot->rom_path), cached->metadata.header_title);
    }
    else {
        copy_text(out, out_size, path_file_name(slot->rom_path));
    }
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

    bool has_local_slot = false;
    char local_slot_name[16] = "";
    if (local_user) {
        has_local_slot = registered_rom_slot_at(state, state->room.link.room_slot_index) != NULL;
        if (has_local_slot) {
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
        if (has_local_slot) {
            format_local_rom_detail(state, state->room.link.room_slot_index, detail_out, detail_out_size);
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

void integral_client_ui_draw_room_link(SDL_Renderer *renderer, const AppState *state)
{
    integral_client_ui_clear_screen(renderer);
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    SDL_Color label = theme->label;
    SDL_Color value = theme->value;
    SDL_Color selected = theme->selected;
    SDL_Color cursor = theme->cursor;
    SDL_Color muted = theme->muted;

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
        SDL_Rect rect = {.x = 14, .y = y - 4, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 18};
        integral_client_ui_draw_selection(renderer, rect, 24, y, 1, false);
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
        SDL_Rect rect = {.x = 14, .y = save_y - 4, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 18};
        integral_client_ui_draw_selection(renderer, rect, 24, save_y, 1, false);
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
        SDL_Rect rect = {.x = 14, .y = ready_y - 4, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 18};
        integral_client_ui_draw_selection(renderer, rect, 24, ready_y, 1, false);
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
        SDL_Rect rect = {.x = 14, .y = log_y - 22, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 122};
        integral_client_ui_draw_selection(renderer, rect, 24, log_y - 12, 2, false);
    }
    integral_sdl_draw_text(renderer, 48, log_y - 16, labels[3], 1, state->room.common.room_selected == 3 ? selected : label);
    integral_sdl_draw_text(renderer, 140, log_y - 16, details[3], 1, muted);
    integral_client_ui_draw_panel(renderer, 22, log_y + 2, INTEGRAL_WINDOW_WIDTH - 44, 100, theme->panel_border);
    integral_client_ui_draw_room_chat_messages(renderer,
                                               &state->room.common,
                                               34,
                                               log_y + 8,
                                               INTEGRAL_WINDOW_WIDTH - 68);

    int input_y = 402;
    if (state->room.common.room_selected == 4) {
        SDL_Rect rect = {.x = 14, .y = input_y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 36};
        integral_client_ui_draw_selection(renderer, rect, 24, input_y + 1, 2,
                                          state->room.common.room_chat_editing);
    }
    integral_sdl_draw_text(renderer, 48, input_y, labels[4], 1, state->room.common.room_selected == 4 ? selected : label);
    integral_client_ui_draw_panel(renderer, 142, input_y - 6, INTEGRAL_WINDOW_WIDTH - 164, 30, theme->panel_border);
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

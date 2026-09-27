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

void integral_client_ui_draw_room_n64(SDL_Renderer *renderer, const AppState *state)
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
    int local_user_index = n64_room_local_user_index(&state->room);
    if (!n64_slot[0] && local_user_index == 0 && state->room.n64.n64_room_n64_slot_index >= 0) {
        int index = state->room.n64.n64_room_n64_slot_index;
        const IntegralClientRomMetadataCache *cached = client_rom_metadata_for_slot(state, index);
        snprintf(fallback_n64_slot, sizeof(fallback_n64_slot), "ROM%d", index + 1);
        n64_slot = fallback_n64_slot;
        n64_filename = state->catalog.rom_slots[index].rom_path;
        if (cached && cached->header_valid) n64_header_title = cached->metadata.header_title;
    }
    if (!user1_gb_slot[0] && local_user_index == 0 && state->room.n64.n64_room_user1_gb_slot_index >= 0) {
        int index = state->room.n64.n64_room_user1_gb_slot_index;
        const IntegralClientRomMetadataCache *cached = client_rom_metadata_for_slot(state, index);
        snprintf(fallback_user1_slot, sizeof(fallback_user1_slot), "ROM%d", index + 1);
        user1_gb_slot = fallback_user1_slot;
        user1_gb_filename = state->catalog.rom_slots[index].rom_path;
        if (cached && cached->header_valid) user1_gb_header_title = cached->metadata.header_title;
    }
    if (!user2_gb_slot[0] && local_user_index == 1 && state->room.n64.n64_room_user2_gb_slot_index >= 0) {
        int index = state->room.n64.n64_room_user2_gb_slot_index;
        const IntegralClientRomMetadataCache *cached = client_rom_metadata_for_slot(state, index);
        snprintf(fallback_user2_slot, sizeof(fallback_user2_slot), "ROM%d", index + 1);
        user2_gb_slot = fallback_user2_slot;
        user2_gb_filename = state->catalog.rom_slots[index].rom_path;
        if (cached && cached->header_valid) user2_gb_header_title = cached->metadata.header_title;
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

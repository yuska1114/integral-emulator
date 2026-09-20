/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_room_common.h"
#include "client_room_link.h"
#include "client_room_n64.h"
#include "client_runtime_support.h"
#include "client_log.h"
#include "client_file_io.h"
#include "client_rom_catalog.h"
#include "client_rom_editor.h"
#include "client_key_config.h"
#include "client_ui_menu.h"
#include "rom_metadata.h"
#include "../runtimes/gb/src/common/key_config.h"
#include "../runtimes/common/window_focus.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <io.h>
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif


#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif
static void move_room_mode_selection(IntegralRoomContext *state, int delta);
static void refresh_room_with_status(IntegralRoomContext *state, bool update_status);
static bool lifecycle_status_stops_emulator(const char *status);
const IntegralConfigRomSlot *room_registered_rom_slot_at(const IntegralRoomContext *state, int index)
{
    if (index < 0 || index >= INTEGRAL_ROM_SLOTS) {
        return NULL;
    }
    if (state->rom_slots[index].rom_path[0] == '\0') {
        return NULL;
    }
    if (state->rom_slots[index].rom_id[0] == '\0' || state->rom_slots[index].save_id[0] == '\0') {
        return NULL;
    }
    return &state->rom_slots[index];
}


int current_room_user_position(const IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS ||
        state->login->username[0] == '\0') {
        return 0;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    if (room->user1[0] != '\0' && strcasecmp(state->login->username, room->user1) == 0) {
        return 1;
    }
    if (room->user2[0] != '\0' && strcasecmp(state->login->username, room->user2) == 0) {
        return 2;
    }
    return 0;
}


bool current_room_is_user1(const IntegralRoomContext *state)
{
    return current_room_user_position(state) == 1;
}


void load_room_chat_from_api(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) {
        return;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    mark_room_game_ended_if_used(state);
    if (room->link_session_id[0] != '\0') {
        set_room_link_session_id(state, room->link_session_id);
        state->link.room_session_missing_since_ticks = 0;
    }
    else if (state->link.room_link_session_id[0] == '\0') {
        clear_room_link_session_id(state);
    }
    else if (state->link.room_game_ended) {
        client_room_log(state,
                   "room_session_clear_server_ended",
                   "session=%s game_ended=%d",
                   state->link.room_link_session_id,
                   state->link.room_game_ended ? 1 : 0);
        clear_room_link_session_id(state);
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED");
    }
    else {
        Uint32 now = SDL_GetTicks();
        if (state->link.room_session_missing_since_ticks == 0) {
            state->link.room_session_missing_since_ticks = now ? now : 1;
        }
        char status[32];
        char error[160];
        int session_room_number = 0;
        int get_rc = integral_api_get_link_session_info(state->login->server,
                                                   state->login->token,
                                                   state->link.room_link_session_id,
                                                   status,
                                                   sizeof(status),
                                                   &session_room_number,
                                                   error,
                                                   sizeof(error));
        Uint32 missing_for = now - state->link.room_session_missing_since_ticks;
        if (get_rc != 0) {
            client_room_log(state,
                       "room_session_verify_failed",
                       "session=%s missing_ms=%u error=%s",
                       state->link.room_link_session_id,
                       (unsigned)missing_for,
                       error);
            if (missing_for >= 5000u) {
                clear_room_link_session_id(state);
            }
        }
        else if (!link_session_status_is_active(status) || session_room_number != (int)state->common.room_number) {
            client_room_log(state,
                       "room_session_clear_verified_missing",
                       "session=%s status=%s session_room=%d current_room=%u missing_ms=%u",
                       state->link.room_link_session_id,
                       status,
                       session_room_number,
                       state->common.room_number,
                       (unsigned)missing_for);
            clear_room_link_session_id(state);
        }
        else {
            state->link.room_session_missing_since_ticks = 0;
            client_room_log(state,
                       "room_session_keep_verified_active",
                       "session=%s status=%s room=%d missing_ms=%u",
                       state->link.room_link_session_id,
                       status,
                       session_room_number,
                       (unsigned)missing_for);
        }
    }
    memset(state->common.room_chat_log, 0, sizeof(state->common.room_chat_log));
    state->common.room_chat_scroll = 0;
    if (room->chat_count == 0) {
        return;
    }
    unsigned start = room->chat_count > INTEGRAL_CHAT_LOG_LINES ? room->chat_count - INTEGRAL_CHAT_LOG_LINES : 0;
    unsigned out = INTEGRAL_CHAT_LOG_LINES - (room->chat_count - start);
    for (unsigned i = start; i < room->chat_count && out < INTEGRAL_CHAT_LOG_LINES; i++) {
        copy_text(state->common.room_chat_log[out++], INTEGRAL_CHAT_MESSAGE_MAX, room->chat[i]);
    }
}


unsigned chat_message_count(char log[INTEGRAL_CHAT_LOG_LINES][INTEGRAL_CHAT_MESSAGE_MAX])
{
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES; i++) {
        if (log[i][0] != '\0') {
            count++;
        }
    }
    return count;
}


bool room_status_is_actionable_error(const char *status)
{
    if (!status || status[0] == '\0') {
        return false;
    }
    return ((strncmp(status, "ROOM ", 5) == 0 &&
             (strstr(status, "FAILED") || strstr(status, "REQUIRED") || strstr(status, "NOT MATCHED"))) ||
            strncmp(status, "HOST START FAILED", 17) == 0 ||
            strncmp(status, "LINK START FAILED", 17) == 0 ||
            strncmp(status, "LINK NODE", 9) == 0 ||
            strncmp(status, "PROTOCOL CHECK FAILED", 21) == 0 ||
            strncmp(status, "FIXED HOST ROM CHECK", 20) == 0 ||
            strncmp(status, "FIXED HOST MANIFEST FAILED", 26) == 0 ||
            strncmp(status, "FIXED HOST PREFLIGHT FAILED", 27) == 0 ||
            strncmp(status, "FIXED HOST SERVER TIME FAILED", 29) == 0 ||
            strcmp(status, "FIXED HOST CLIENT UPDATE REQUIRED") == 0 ||
            strcmp(status, "FIXED HOST ROLE INVALID") == 0 ||
            strcmp(status, "CLIENT CAPABILITY MISMATCH") == 0 ||
            strcmp(status, "WAITING PEER ROM") == 0);
}


static void move_room_mode_selection(IntegralRoomContext *state, int delta)
{
    int selected = (int)state->common.room_mode_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_ROOM_MODE_ROWS - 1;
    }
    if (selected >= INTEGRAL_ROOM_MODE_ROWS) {
        selected = 0;
    }
    state->common.room_mode_selected = (unsigned)selected;
}


const char *create_room_mode_api_name(unsigned selection)
{
    if (selection == 0u) return "link_cable";
    if (selection == 1u) return "n64";
    return NULL;
}


void handle_room_mode_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            (*state->screen) = SCREEN_MAIN_MENU;
            copy_text(state->login->status, sizeof(state->login->status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
        case SDLK_RIGHT:
            move_room_mode_selection(state, 1);
            break;
        case SDLK_UP:
        case SDLK_LEFT:
            move_room_mode_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        {
            if (state->common.room_mode_selected == 2u) {
                (*state->screen) = SCREEN_MAIN_MENU;
                copy_text(state->login->status, sizeof(state->login->status), "MAIN MENU");
                break;
            }
            IntegralApiRoom room;
            char error[160];
            const char *mode = create_room_mode_api_name(state->common.room_mode_selected);
            copy_text(state->login->status, sizeof(state->login->status), "CREATING ROOM");
            if (integral_api_create_room(state->login->server, state->login->token, mode,
                                         &room, error, sizeof(error)) != 0) {
                if (strstr(error, "no ROOM") || strstr(error, "allocation")) {
                    copy_text(state->login->status, sizeof(state->login->status),
                              state->common.room_mode_selected == 0
                                  ? "NO LINK CABLE ROOM AVAILABLE"
                                  : "NO N64 ROOM AVAILABLE");
                }
                else if (strstr(error, "already in a ROOM")) {
                    copy_text(state->login->status, sizeof(state->login->status), "ALREADY IN A ROOM");
                }
                else {
                    snprintf(state->login->status, sizeof(state->login->status),
                             "CREATE ROOM FAILED %s", error);
                }
                client_room_log(state, "room_create_failed", "mode=%s error=%s", mode, error);
                break;
            }
            client_room_log(state, "room_create_ok", "mode=%s room=%u", mode, room.room_number);
            activate_matched_room(state, &room);
            break;
        }
        default:
            break;
    }
}


void handle_join_room_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) return;
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->common.room_code_editing = false;
            SDL_StopTextInput();
            (*state->screen) = SCREEN_MAIN_MENU;
            copy_text(state->login->status, sizeof(state->login->status), "MAIN MENU");
            break;
        case SDLK_BACKSPACE:
            remove_last_char(state->common.room_code_input);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        {
            IntegralApiRoom room;
            char error[160];
            if (strlen(state->common.room_code_input) != 5) {
                copy_text(state->login->status, sizeof(state->login->status),
                          "ROOM CODE MUST BE 5 DIGITS");
                break;
            }
            if (integral_api_join_room_code(state->login->server, state->login->token,
                                            state->common.room_code_input, &room,
                                            error, sizeof(error)) != 0) {
                if (strstr(error, "not available")) {
                    copy_text(state->login->status, sizeof(state->login->status), "ROOM NOT AVAILABLE");
                }
                else if (strstr(error, "too many ROOM join")) {
                    copy_text(state->login->status, sizeof(state->login->status),
                              "TOO MANY ATTEMPTS  TRY LATER");
                }
                else if (strstr(error, "already in a ROOM")) {
                    copy_text(state->login->status, sizeof(state->login->status), "ALREADY IN A ROOM");
                }
                else {
                    snprintf(state->login->status, sizeof(state->login->status),
                             "JOIN ROOM FAILED %s", error);
                }
                client_room_log(state, "room_code_join_failed", "error=%s", error);
                break;
            }
            state->common.room_code_editing = false;
            SDL_StopTextInput();
            client_room_log(state, "room_code_join_ok", "room=%u mode=%s",
                       room.room_number, room.room_type);
            activate_matched_room(state, &room);
            break;
        }
        default:
            break;
    }
}


void handle_join_room_text_input(IntegralRoomContext *state, const SDL_TextInputEvent *text)
{
    for (const char *p = text->text; *p && strlen(state->common.room_code_input) < 5; p++) {
        if (*p >= '0' && *p <= '9') {
            size_t length = strlen(state->common.room_code_input);
            state->common.room_code_input[length] = *p;
            state->common.room_code_input[length + 1] = '\0';
        }
    }
}


static void refresh_room_with_status(IntegralRoomContext *state, bool update_status)
{
    if (state->login->token[0] == '\0') {
        if (update_status) {
            copy_text(state->login->status, sizeof(state->login->status), "LOGIN TOKEN REQUIRED");
        }
        return;
    }
    if ((*state->screen) == SCREEN_N64_ROOM) state->common.room_poll_epoch++;
    char error[160];
    IntegralApiRoom current;
    int has_room = 0;
    if (integral_api_get_current_room(state->login->server,
                                state->login->token,
                                &current,
                                &has_room,
                                error,
                                sizeof(error)) != 0) {
        if (update_status) {
            snprintf(state->login->status, sizeof(state->login->status), "ROOM REFRESH FAILED %s", error);
        }
        return;
    }
    memset(&state->common.current_room, 0, sizeof(state->common.current_room));
    if (has_room && current.room_number >= 1 && current.room_number <= INTEGRAL_API_ROOMS) {
        state->common.current_room = current;
        state->common.room_number = current.room_number;
    }
    if ((*state->screen) == SCREEN_ROOM || (*state->screen) == SCREEN_N64_ROOM) {
        if (!has_room) {
            /* The successful current-ROOM response is authoritative. Reap
             * before discarding identity, even when transport EOF raced the
             * terminal notice. HTTP failures above retain the recovery path. */
            if ((*state->screen) == SCREEN_ROOM) {
                if (state->link.room_client_pid > 0 && !stop_room_client(state)) return;
                clear_room_link_session_id(state);
                state->common.room_ready_self = false;
                state->common.room_ready_peer = false;
                state->common.room_poll_epoch++;
            }
            (*state->screen) = SCREEN_MAIN_MENU;
            state->common.room_number = 0;
            copy_text(state->login->status, sizeof(state->login->status), "ROOM CLOSED");
            return;
        }
        load_room_chat_from_api(state);
        sync_room_ready_flags_from_api(state);
    }
}


void refresh_room(IntegralRoomContext *state)
{
    refresh_room_with_status(state, true);
}


void refresh_room_quiet(IntegralRoomContext *state)
{
    refresh_room_with_status(state, false);
}






void activate_matched_room(IntegralRoomContext *state, const IntegralApiRoom *matched_room)
{
    if (!matched_room || matched_room->room_number < 1 ||
        matched_room->room_number > INTEGRAL_API_ROOMS) return;
    if (strcmp(matched_room->room_type, "n64") == 0) activate_n64_room(state, matched_room);
    else activate_link_room(state, matched_room);
}


void leave_current_room(IntegralRoomContext *state, bool update_status)
{
    if ((*state->screen) == SCREEN_N64_ROOM &&
        (state->n64.lifecycle_session_id[0] || state->n64.n64_runtime_media_session_id[0])) {
        suspend_n64_runtime_media_connection(state);
        if (state->n64.n64_runtime_media_host_pid) return;
        char error[160];
        if (integral_api_terminate_n64_room(state->login->server, state->login->token,
                state->n64.lifecycle_session_id, state->n64.lifecycle_room_code,
                error, sizeof(error)) != 0) {
            copy_text(state->login->status, sizeof(state->login->status),
                      "ROOM EXIT UNCONFIRMED - ESC RETRY");
            return;
        }
        finish_n64_room_locally(state);
        copy_text(state->login->status, sizeof(state->login->status), "N64 GAME ENDED");
        return;
    }
    reset_n64_runtime_media_connection(state);
    if (state->link.room_client_pid > 0 &&
        (!state->login->token[0] || !state->link.room_link_session_id[0])) {
        if (!stop_room_client(state)) return;
    }
    if (state->login->token[0] == '\0') {
        return;
    }
    if (state->link.room_client_pid > 0 && state->link.room_link_session_id[0] != '\0') {
        if (!stop_room_client(state)) {
            client_room_log(state, "room_leave_blocked", "reason=client_stop_failed session=%s", state->link.room_link_session_id);
            return;
        }
        if (!stop_current_game_session(state)) {
            client_room_log(state, "room_leave_blocked", "reason=game_stop_failed session=%s", state->link.room_link_session_id);
            return;
        }
        client_room_log(state, "room_leave_local_after_client_stop", "session=%s", state->link.room_link_session_id);
        /* Stopping the game is not leaving its ROOM. Keep ownership until the
         * leave response confirms membership cleanup. */
    }
    char error[160];
    client_room_log(state, "room_leave_request", "room=%u session=%s", state->common.room_number, state->link.room_link_session_id);
    if (integral_api_leave_room(state->login->server, state->login->token, error, sizeof(error)) != 0) {
        if (update_status) {
            snprintf(state->login->status, sizeof(state->login->status), "ROOM LEAVE FAILED %s", error);
        }
        client_room_log(state, "room_leave_failed", "room=%u error=%s", state->common.room_number, error);
        return;
    }
    client_room_log(state, "room_leave_ok", "room=%u", state->common.room_number);
    clear_room_link_session_id(state);
    memset(&state->common.current_room, 0, sizeof(state->common.current_room));
    state->common.room_poll_epoch++;
    state->common.room_number = 0;
    state->common.room_ready_self = false;
    state->common.room_ready_peer = false;
    state->link.room_link_session_id[0] = '\0';
    state->link.room_game_session_id[0] = '\0';
    state->link.room_fencing_token = 0;
    state->link.room_start_requested = false;
    state->link.room_client_started = false;
    state->link.room_gb_runtime_save_preflight_blocked = false;
    state->link.room_game_ended = false;
    state->link.room_client_pid = 0;
    state->link.room_session_missing_since_ticks = 0;
    state->common.room_chat_editing = false;
    state->common.room_chat_scroll = 0;
    state->common.room_chat_input[0] = '\0';
    state->common.room_chat_composition[0] = '\0';
    memset(state->common.room_chat_log, 0, sizeof(state->common.room_chat_log));
    if (update_status) {
        copy_text(state->login->status, sizeof(state->login->status), "LEFT ROOM");
    }
}


static bool lifecycle_status_stops_emulator(const char *status)
{
    return status && (
        strcmp(status, "FINALIZING") == 0 ||
        strcmp(status, "RECOVERING") == 0 ||
        strcmp(status, "COMPLETED") == 0 ||
        strcmp(status, "FAILED") == 0 ||
        strcmp(status, "CANCELLED") == 0 ||
        strcmp(status, "CLOSED") == 0 ||
        strcmp(status, "EXPIRED") == 0
    );
}


void finish_n64_room_locally(IntegralRoomContext *state)
{
    bool was_room = (*state->screen) == SCREEN_N64_ROOM;
    reset_n64_runtime_media_connection(state);
    if (state->n64.n64_runtime_media_host_pid) return;
    state->common.room_number = 0;
    state->common.room_ready_self = state->common.room_ready_peer = false;
    state->common.room_code_input[0] = '\0';
    state->common.room_chat_editing = false;
    state->common.room_chat_input[0] = state->common.room_chat_composition[0] = '\0';
    memset(state->common.room_chat_log, 0, sizeof(state->common.room_chat_log));
    state->n64.n64_room_ready = false;
    state->n64.runtime_exit_confirming = false;
    memset(&state->common.current_room, 0, sizeof(state->common.current_room));
    state->common.room_poll_epoch++;
    state->link.room_game_ended = true;
    (*state->screen) = SCREEN_MAIN_MENU;
    if (was_room) integral_focus_new_game_window(state->main_window);
}

void handle_room_heartbeat_result(IntegralRoomContext *state,
                                          const IntegralApiHeartbeatStatus *heartbeat,
                                          bool succeeded,
                                          const char *error)
{
    if (!succeeded) {
        bool authoritative = error &&
            (strstr(error, "401") || strstr(error, "403") ||
             strstr(error, "fence") || strstr(error, "session mismatch"));
        state->common.room_heartbeat_failures = authoritative
                                                ? 3u
                                                : state->common.room_heartbeat_failures + 1u;
        client_room_log(state,
                   "room_heartbeat_failed",
                   "failures=%u error=%s",
                   state->common.room_heartbeat_failures,
                   error && error[0] ? error : "unknown");
        if (state->common.room_heartbeat_failures < 3) {
            return;
        }
        if ((*state->screen) == SCREEN_N64_ROOM) {
            suspend_n64_runtime_media_connection(state);
        }
        else if ((*state->screen) == SCREEN_ROOM && state->link.room_client_pid > 0) {
            if (!stop_room_client(state)) return;
        }
        state->link.room_game_ended = true;
        copy_text(state->login->status,
                  sizeof(state->login->status),
                  "SESSION LOST  EMULATOR STOPPED");
        return;
    }

    state->common.room_heartbeat_failures = 0;
    if (!heartbeat) {
        return;
    }
    if (strcmp(heartbeat->lifecycle_kind, "link") == 0 &&
        (heartbeat->lifecycle_session_id[0] == '\0' ||
         state->link.room_link_session_id[0] == '\0' ||
         strcmp(heartbeat->lifecycle_session_id,
                state->link.room_link_session_id) != 0)) {
        client_room_log(state,
                   "room_stale_lifecycle_ignored",
                   "notice_session=%s current_session=%s status=%s",
                   heartbeat->lifecycle_session_id[0]
                       ? heartbeat->lifecycle_session_id : "-",
                   state->link.room_link_session_id[0]
                       ? state->link.room_link_session_id : "-",
                   heartbeat->lifecycle_status[0]
                       ? heartbeat->lifecycle_status : "-");
        return;
    }
    const char *media_session = state->n64.lifecycle_session_id[0] &&
                               state->n64.lifecycle_room_number == state->common.room_number
        ? state->n64.lifecycle_session_id : state->n64.n64_runtime_media_session_id;
    if (strcmp(heartbeat->lifecycle_kind, "media") == 0 &&
        (heartbeat->lifecycle_session_id[0] == '\0' ||
         media_session[0] == '\0' || strcmp(heartbeat->lifecycle_session_id, media_session) != 0)) {
        client_room_log(state,
                   "room_stale_lifecycle_ignored",
                   "notice_session=%s current_session=%s status=%s",
                   heartbeat->lifecycle_session_id[0]
                       ? heartbeat->lifecycle_session_id : "-",
                   state->n64.n64_runtime_media_session_id[0]
                       ? state->n64.n64_runtime_media_session_id : "-",
                   heartbeat->lifecycle_status[0]
                       ? heartbeat->lifecycle_status : "-");
        return;
    }
    copy_text(state->common.room_lifecycle_status,
              sizeof(state->common.room_lifecycle_status),
              heartbeat->lifecycle_status);
    copy_text(state->common.room_termination_reason,
              sizeof(state->common.room_termination_reason),
              heartbeat->termination_reason);
    long long expires_unix = 0;
    state->common.room_remaining_seconds =
        heartbeat->server_unix_time > 0 &&
        parse_iso8601_unix(heartbeat->expires_at, &expires_unix)
            ? (expires_unix > heartbeat->server_unix_time
                   ? expires_unix - heartbeat->server_unix_time
                   : 0)
            : -1;
    if ((strcmp(heartbeat->lifecycle_kind, "media") == 0 &&
         strcmp(heartbeat->lifecycle_status, "RECOVERING") == 0) ||
        !lifecycle_status_stops_emulator(heartbeat->lifecycle_status)) {
        return;
    }
    if ((*state->screen) != SCREEN_ROOM && (*state->screen) != SCREEN_N64_ROOM) {
        return;
    }

    if (strcmp(heartbeat->lifecycle_kind, "room") == 0) {
        bool n64 = (*state->screen) == SCREEN_N64_ROOM;
        if (n64) {
            finish_n64_room_locally(state);
            snprintf(state->login->status, sizeof(state->login->status), "%s",
                strcmp(heartbeat->termination_reason, "host_finished") == 0 ||
                strcmp(heartbeat->termination_reason, "participant_left") == 0 ||
                strcmp(heartbeat->termination_reason, "room_closed_by_creator") == 0
                    ? "N64 GAME ENDED" : "N64 SESSION EXPIRED");
            return;
        }
        reset_n64_runtime_media_connection(state);
        if (state->link.room_client_pid > 0) {
            if (!stop_room_client(state)) return;
        }
        clear_room_link_session_id(state);
        state->common.room_number = 0;
        state->common.room_ready_self = false;
        state->common.room_ready_peer = false;
        state->n64.n64_room_ready = false;
        memset(&state->common.current_room, 0, sizeof(state->common.current_room));
        state->common.room_poll_epoch++;
        state->link.room_game_ended = true;
        (*state->screen) = SCREEN_MAIN_MENU;
        snprintf(state->login->status,
                 sizeof(state->login->status),
                 "ROOM EXPIRED  %s",
                 heartbeat->termination_reason[0]
                     ? heartbeat->termination_reason
                     : "IDLE TIMEOUT");
        return;
    }

    client_room_log(state,
               "room_session_terminal",
               "status=%s reason=%s",
               heartbeat->lifecycle_status,
               heartbeat->termination_reason[0] ? heartbeat->termination_reason : "-");
    if ((*state->screen) == SCREEN_N64_ROOM &&
        strcmp(heartbeat->lifecycle_kind, "media") == 0 &&
        strcmp(heartbeat->termination_reason, "preflight_failed") == 0) {
        reset_n64_runtime_media_connection(state);
        state->common.room_ready_self = state->common.room_ready_peer = false;
        state->n64.preflight_failed = true;
        state->n64.n64_room_ready = false;
        state->common.room_poll_epoch++;
        state->common.current_room.ready1 = state->common.current_room.ready2 = 0;
        copy_text(state->login->status, sizeof(state->login->status),
                  "SAV NOT READY: RUN IN LOCAL, EXIT NORMALLY, THEN RETRY.");
        integral_focus_new_game_window(state->main_window);
        return;
    }
    if ((*state->screen) == SCREEN_N64_ROOM) {
        finish_n64_room_locally(state);
    }
    else if ((*state->screen) == SCREEN_ROOM && state->link.room_client_pid > 0) {
        if (!stop_room_client(state)) return;
    }
    state->link.room_game_ended = true;
    if (strcmp(heartbeat->lifecycle_kind, "media") == 0 &&
        ((strcmp(heartbeat->lifecycle_status, "COMPLETED") == 0 &&
          strcmp(heartbeat->termination_reason, "host_finished") == 0) ||
         (strcmp(heartbeat->lifecycle_status, "CANCELLED") == 0 &&
          strcmp(heartbeat->termination_reason, "participant_left") == 0))) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 GAME ENDED");
        return;
    }
    snprintf(state->login->status,
             sizeof(state->login->status),
             "SESSION %s  %s",
             heartbeat->lifecycle_status,
             heartbeat->termination_reason[0] ? heartbeat->termination_reason : "SERVER END");
}


void send_room_heartbeat(IntegralRoomContext *state, Uint32 now)
{
    if (state->login->token[0] == '\0') {
        return;
    }
    if ((*state->screen) != SCREEN_ROOM && (*state->screen) != SCREEN_N64_ROOM) {
        return;
    }
    Uint32 heartbeat_interval = ((*state->screen) == SCREEN_ROOM || (*state->screen) == SCREEN_N64_ROOM)
                                    ? INTEGRAL_ROOM_HEARTBEAT_MS
                                    : INTEGRAL_LINK_ROOM_HEARTBEAT_MS;
    if (!integral_room_heartbeat_attempt_due(state->common.last_room_heartbeat_attempt_ticks,
                                              state->common.room_heartbeat_attempted,
                                              now,
                                              heartbeat_interval)) {
        return;
    }
    state->common.last_room_heartbeat_attempt_ticks = now;
    state->common.room_heartbeat_attempted = true;
    char error[160];
    IntegralApiHeartbeatStatus heartbeat;
    bool succeeded = integral_api_room_heartbeat_status(
                         state->login->server,
                         state->login->token,
                         &heartbeat,
                         error,
                         sizeof(error)) == 0;
    if (succeeded) {
        state->common.last_room_heartbeat_ticks = now;
    }
    handle_room_heartbeat_result(state, &heartbeat, succeeded, error);
}


void handle_room_text_input(IntegralRoomContext *state, const SDL_TextInputEvent *text)
{
    if (state->common.room_selected != 4 || !state->common.room_chat_editing) {
        return;
    }
    state->common.room_chat_composition[0] = '\0';
    append_text(state->common.room_chat_input, sizeof(state->common.room_chat_input), text->text);
}


void handle_room_text_editing(IntegralRoomContext *state, const SDL_TextEditingEvent *edit)
{
    if (state->common.room_selected != 4 || !state->common.room_chat_editing) {
        return;
    }
    copy_text(state->common.room_chat_composition, sizeof(state->common.room_chat_composition), edit->text);
}

Uint32 main_loop_delay_ms(const IntegralRoomContext *state)
{
    if (state && (*state->screen) == SCREEN_N64_ROOM && state->n64.n64_runtime_media_paired) {
        if (strcmp(state->n64.n64_runtime_media_role, "remote") == 0 &&
            integral_n64_runtime_media_stream_is_video_vsync_paced(state->n64.n64_runtime_media_stream)) {
            return 0;
        }
        return 1;
    }
    return 16;
}

void poll_server_state(IntegralRoomContext *state)
{
    Uint32 now = SDL_GetTicks();
    monitor_room_gb_runtime_client_exit(state);
    poll_n64_room_async(state, now);
    if ((*state->screen) != SCREEN_N64_ROOM) send_room_heartbeat(state, now);
    if ((*state->screen) == SCREEN_ROOM) {
        if (now - state->common.last_room_poll_ticks >= 1000u) {
            state->common.last_room_poll_ticks = now;
            refresh_room_quiet(state);
            if (!state->link.room_client_started && (current_room_has_link_session(state) || current_room_is_ready_to_start(state))) {
                maybe_start_room_session(state);
                if (state->link.room_link_session_id[0] == '\0' && state->link.room_start_requested) {
                    copy_text(state->login->status, sizeof(state->login->status), "BOTH READY  SERVER START PENDING");
                }
            }
        }
    }
    else if ((*state->screen) == SCREEN_N64_ROOM) {
        (void)poll_n64_runtime_media_transport(state, now);
    }
}

void integral_room_reset_selection(IntegralRoomContext *state)
{
    state->link.room_slot_index = -1;
    state->n64.n64_room_n64_slot_index = -1;
    state->n64.n64_room_user1_gb_slot_index = -1;
    state->n64.n64_room_user2_gb_slot_index = -1;
}

void integral_room_validate_selection(IntegralRoomContext *state, int local_indices[2])
{
    validate_local_indices(state->rom_slots, local_indices, &state->link.room_slot_index);
}

bool integral_room_create_media(IntegralRoomContext *state, SDL_Window *window, SDL_Renderer *renderer)
{
    state->main_window = window;
    state->n64.n64_runtime_media_stream = integral_n64_runtime_media_stream_create(window, renderer);
    return state->n64.n64_runtime_media_stream != NULL;
}

bool integral_room_create_poll_worker(IntegralRoomContext *state)
{
    state->n64.n64_room_poll_worker = integral_room_poll_worker_create();
    return state->n64.n64_room_poll_worker != NULL;
}

void integral_room_destroy_resources(IntegralRoomContext *state)
{
    integral_room_poll_worker_destroy(state->n64.n64_room_poll_worker);
    integral_n64_runtime_media_stream_destroy(state->n64.n64_runtime_media_stream);
    state->n64.n64_room_poll_worker = NULL;
    state->n64.n64_runtime_media_stream = NULL;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_room_link.h"

#include "client_room_common.h"
#include "client_runtime_support.h"
#include "client_rom_catalog.h"
#include "client_log.h"

#include <stdio.h>
#include <string.h>

const char *room_link_mode_api_name(IntegralRoomLinkMode mode)
{
    switch (mode) {
        case INTEGRAL_ROOM_MODE_BATTLE:
            return "battle";
        case INTEGRAL_ROOM_MODE_TRADE:
            return "trade";
    }
    return "trade";
}

bool room_link_mode_from_api(const char *mode, IntegralRoomLinkMode *mode_out)
{
    if (!mode_out) return false;
    if (mode && strcmp(mode, "battle") == 0) {
        *mode_out = INTEGRAL_ROOM_MODE_BATTLE;
        return true;
    }
    if (mode && strcmp(mode, "trade") == 0) {
        *mode_out = INTEGRAL_ROOM_MODE_TRADE;
        return true;
    }
    return false;
}

void set_room_link_session_id(IntegralRoomContext *state, const char *session_id)
{
    if (!session_id || session_id[0] == '\0') return;
    if (strcmp(state->link.room_link_session_id, session_id) == 0) return;
    if (state->link.room_client_pid > 0 && !stop_room_client(state)) return;
    client_room_log(state, "room_session_set", "old_session=%s new_session=%s",
                    state->link.room_link_session_id[0] ? state->link.room_link_session_id : "-",
                    session_id);
    copy_text(state->link.room_link_session_id, sizeof(state->link.room_link_session_id), session_id);
    state->link.room_game_session_id[0] = '\0';
    state->link.room_fencing_token = 0;
    state->link.room_client_started = false;
    state->link.room_gb_runtime_save_preflight_blocked = false;
    state->link.preflight_block_pending[0] = '\0';
    state->link.room_gb_runtime_fixed_host_active = false;
    state->link.room_gb_runtime_fixed_host_role[0] = '\0';
    state->link.room_gb_runtime_fixed_host_result_read = -1;
    state->link.room_gb_runtime_fixed_host_ticket_write = -1;
    SDL_AtomicSet(&state->link.room_gb_runtime_fixed_host_ticket_requested, 0);
    state->link.room_gb_runtime_fixed_host_save_policy[0] = '\0';
    state->link.room_game_ended = false;
    state->link.room_link_mode_local_override = false;
    state->link.room_client_pid = 0;
    state->link.room_session_missing_since_ticks = 0;
    state->common.room_heartbeat_failures = 0;
    state->common.room_heartbeat_attempted = false;
    state->common.room_lifecycle_status[0] = '\0';
    state->common.room_termination_reason[0] = '\0';
    state->common.room_remaining_seconds = -1;
}

void clear_room_link_session_id(IntegralRoomContext *state)
{
    if (state->link.room_client_pid > 0) {
        monitor_room_gb_runtime_client_exit(state);
        if (state->link.room_client_pid > 0 && !stop_room_client(state)) return;
    }
    if (state->link.room_link_session_id[0] == '\0') return;
    client_room_log(state, "room_session_clear", "session=%s", state->link.room_link_session_id);
    state->link.room_link_session_id[0] = '\0';
    state->link.room_game_session_id[0] = '\0';
    state->link.room_fencing_token = 0;
    state->link.room_start_requested = false;
    state->link.room_client_started = false;
    state->link.room_gb_runtime_save_preflight_blocked = false;
    state->link.preflight_block_pending[0] = '\0';
    state->link.room_gb_runtime_fixed_host_active = false;
    state->link.room_gb_runtime_fixed_host_role[0] = '\0';
    state->link.room_gb_runtime_fixed_host_result_read = -1;
    state->link.room_gb_runtime_fixed_host_save_policy[0] = '\0';
    state->link.room_game_ended = false;
    state->link.room_link_mode_local_override = false;
    state->link.room_client_pid = 0;
    state->link.room_session_missing_since_ticks = 0;
}

bool link_session_status_is_active(const char *status)
{
    return strcmp(status, "CREATED") == 0 || strcmp(status, "WAITING_PLAYER_A") == 0 ||
           strcmp(status, "WAITING_PLAYER_B") == 0 || strcmp(status, "PREPARING") == 0 ||
           strcmp(status, "RUNNING") == 0 || strcmp(status, "FINALIZING") == 0 ||
           strcmp(status, "RECOVERING") == 0;
}

void sync_room_ready_flags_from_api(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS ||
        state->login->username[0] == '\0') return;
    const IntegralApiRoom *room = &state->common.current_room;
    int user_position = current_room_user_position(state);
    if (user_position != 1) state->link.room_link_mode_local_override = false;
    if (room->link_mode[0] != '\0') {
        IntegralRoomLinkMode server_mode;
        if (!room_link_mode_from_api(room->link_mode, &server_mode)) {
            copy_text(state->login->status, sizeof(state->login->status), "ROOM MODE INVALID");
            client_room_log(state, "room_mode_invalid", "value=%s", room->link_mode);
            return;
        }
        if (state->link.room_link_mode_local_override && server_mode == state->link.room_link_mode) {
            state->link.room_link_mode_local_override = false;
        }
        if (!state->link.room_link_mode_local_override || current_room_game_ended(state) ||
            room->link_session_id[0] != '\0') {
            state->link.room_link_mode = server_mode;
        }
    }
    if (user_position == 1) {
        state->common.room_ready_self = room->ready1 != 0;
        state->common.room_ready_peer = room->ready2 != 0;
    }
    else if (user_position == 2) {
        state->common.room_ready_self = room->ready2 != 0;
        state->common.room_ready_peer = room->ready1 != 0;
    }
    if (state->common.room_number >= 65 && state->common.room_number <= 128) {
        state->n64.n64_room_ready = state->common.room_ready_self;
    }
}

bool current_room_game_ended(const IntegralRoomContext *state)
{
    if (state->link.room_game_ended) return true;
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) return false;
    const IntegralApiRoom *room = &state->common.current_room;
    return room->game_started && room->link_session_id[0] == '\0';
}

void mark_room_game_ended_if_used(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) return;
    const IntegralApiRoom *room = &state->common.current_room;
    if ((*state->screen) == SCREEN_N64_ROOM || strcmp(room->room_type, "n64") == 0) return;
    if (!room->game_started || room->link_session_id[0] != '\0') return;
    if (state->link.room_link_session_id[0] != '\0') clear_room_link_session_id(state);
    if (!state->link.room_game_ended) {
        client_room_log(state, "room_mark_game_ended", "room=%u", state->common.room_number);
    }
    state->link.room_game_ended = true;
    state->link.room_start_requested = false;
    copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  USE ANOTHER ROOM");
}

bool gb_runtime_fixed_host_may_start_for_control_state(const char *role, const char *control_state)
{
    if (!role || !control_state) return false;
    if (strcmp(role, "host") == 0) return strcmp(control_state, "READY") == 0;
    if (strcmp(role, "remote") == 0) {
        return strcmp(control_state, "WAITING_PEER") == 0 ||
               strcmp(control_state, "PAUSED_REMOTE") == 0;
    }
    return false;
}

void room_link_sync_state(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS ||
        state->login->token[0] == '\0') return;
    char slot_label[32];
    if (room_registered_rom_slot_at(state, state->link.room_slot_index)) {
        snprintf(slot_label, sizeof(slot_label), "ROM%d", state->link.room_slot_index + 1);
    }
    else {
        slot_label[0] = '\0';
    }
    char error[160];
    if (integral_api_update_room_state(state->login->server, state->login->token,
            state->common.room_number, slot_label,
            state->common.room_ready_self ? 1 : 0,
            current_room_is_user1(state) ? room_link_mode_api_name(state->link.room_link_mode) : NULL,
            error, sizeof(error)) != 0) {
        snprintf(state->login->status, sizeof(state->login->status), "ROOM SYNC FAILED %s", error);
        client_room_log(state, "room_sync_failed", "room=%u slot=%s ready=%d mode=%s error=%s",
                        state->common.room_number, slot_label[0] ? slot_label : "-",
                        state->common.room_ready_self ? 1 : 0,
                        room_link_mode_api_name(state->link.room_link_mode), error);
        return;
    }
    client_room_log(state, "room_sync_ok", "room=%u slot=%s ready=%d mode=%s",
                    state->common.room_number, slot_label[0] ? slot_label : "-",
                    state->common.room_ready_self ? 1 : 0,
                    room_link_mode_api_name(state->link.room_link_mode));
    if ((*state->screen) != SCREEN_ROOM) refresh_room_quiet(state);
}

bool current_room_has_link_session(const IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) return false;
    return state->common.current_room.link_session_id[0] != '\0';
}

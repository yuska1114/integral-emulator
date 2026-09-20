/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ROOM_LINK_H
#define INTEGRAL_CLIENT_ROOM_LINK_H
#include <SDL.h>
#include "client_config.h"
#include "http_client.h"
#include "client_save_sync.h"
#include "room_poll_worker.h"
#include "media_relay_client.h"
#include "n64_runtime_media_stream.h"
#include "gb_runtime_fixed_host_result_ipc.h"
typedef enum IntegralRoomLinkMode {
    INTEGRAL_ROOM_MODE_BATTLE,
    INTEGRAL_ROOM_MODE_TRADE,
} IntegralRoomLinkMode;
typedef struct IntegralLinkRoomState {
    IntegralRoomLinkMode room_link_mode;
    bool room_link_mode_local_override;
    int room_slot_index;
    char room_link_session_id[INTEGRAL_API_ROOM_SESSION_MAX];
    char room_game_session_id[96];
    long long room_fencing_token;
    bool room_start_requested;
    bool room_client_started;
    bool room_game_ended;
    bool room_gb_runtime_save_preflight_blocked;
    char preflight_block_pending[32];
    IntegralChildProcess room_client_pid;
    bool room_gb_runtime_fixed_host_active;
    char room_gb_runtime_fixed_host_role[16];
    intptr_t room_gb_runtime_fixed_host_result_read;
    intptr_t room_gb_runtime_fixed_host_ticket_write;
    SDL_atomic_t room_gb_runtime_fixed_host_ticket_requested;
    SDL_Thread *room_gb_runtime_fixed_host_result_thread;
    IntegralGBRuntimeFixedHostResult room_gb_runtime_fixed_host_result;
    char room_gb_runtime_fixed_host_save_policy[32];
    Uint32 room_session_missing_since_ticks;
} IntegralLinkRoomState;
typedef struct IntegralRoomContext IntegralRoomContext;
const char *room_link_mode_label(IntegralRoomLinkMode mode);
void set_room_link_session_id(IntegralRoomContext *state, const char *session_id);
void clear_room_link_session_id(IntegralRoomContext *state);
void sync_room_ready_flags_from_api(IntegralRoomContext *state);
bool current_room_game_ended(const IntegralRoomContext *state);
void mark_room_game_ended_if_used(IntegralRoomContext *state);
bool gb_runtime_fixed_host_may_start_for_control_state(
    const char *role, const char *control_state);
bool current_room_has_link_session(const IntegralRoomContext *state);
bool current_room_is_ready_to_start(const IntegralRoomContext *state);
void monitor_room_gb_runtime_client_exit(IntegralRoomContext *state);
void discard_gb_runtime_fixed_host_result(IntegralRoomContext *state);
bool handle_gb_runtime_fixed_host_trade_result(IntegralRoomContext *state);
const char *gb_runtime_fixed_host_key_spec(
    const IntegralConfigKeys *keys);
void maybe_start_room_session(IntegralRoomContext *state);
void activate_link_room(IntegralRoomContext *state, const IntegralApiRoom *matched_room);
bool stop_room_client(IntegralRoomContext *state);
void handle_room_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key);
bool link_session_status_is_active(const char *status);
bool stop_current_game_session(IntegralRoomContext *state);
#endif

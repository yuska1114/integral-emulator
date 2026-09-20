/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ROOM_N64_H
#define INTEGRAL_CLIENT_ROOM_N64_H
#include <SDL.h>
#include "client_config.h"
#include "http_client.h"
#include "client_save_sync.h"
#include "room_poll_worker.h"
#include "media_relay_client.h"
#include "n64_runtime_media_stream.h"
#include "gb_runtime_fixed_host_result_ipc.h"
typedef struct N64RuntimeStopResult {
    bool request_created;
    bool graceful;
    bool forced;
    bool stopped;
} N64RuntimeStopResult;
typedef struct IntegralN64RoomState {
    IntegralRoomPollWorker *n64_room_poll_worker;
    bool n64_room_ready;
    int n64_room_n64_slot_index;
    int n64_room_user1_gb_slot_index;
    int n64_room_user2_gb_slot_index;
    char n64_runtime_media_session_id[96];
    char lifecycle_session_id[96];
    unsigned lifecycle_room_number;
    char lifecycle_room_code[INTEGRAL_API_ROOM_CODE_MAX];
    char n64_runtime_media_relay_host[128];
    unsigned n64_runtime_media_relay_port;
    char n64_runtime_media_relay_transport[8];
    char n64_runtime_media_role[16];
    char n64_runtime_media_scope[32];
    char n64_runtime_media_ticket[128];
    IntegralMediaRelayConnection *n64_runtime_media_connection;
    IntegralN64RuntimeMediaStream *n64_runtime_media_stream;
    bool n64_runtime_media_authenticated;
    bool n64_runtime_media_paired;
    bool host_finish_pending;
    bool terminal_pending;
    bool preflight_failed;
    Uint32 n64_runtime_media_retry_after_ticks;
    Uint32 n64_runtime_media_reconnect_deadline;
    uint64_t n64_runtime_media_remote_buttons;
    char n64_runtime_media_remote_input_path[INTEGRAL_CONFIG_PATH_MAX];
    char n64_runtime_media_ipc_path[INTEGRAL_CONFIG_PATH_MAX];
    char n64_runtime_stop_request_path[INTEGRAL_CONFIG_PATH_MAX];
    IntegralChildProcess n64_runtime_media_host_pid;
    char n64_runtime_media_launched_session_id[96];
    Uint32 n64_runtime_media_host_launch_retry_after_ticks;
    uint64_t n64_runtime_media_last_sent_buttons;
    uint32_t n64_runtime_media_input_sequence;
    Uint32 n64_runtime_media_last_input_send_ticks;
    Uint32 n64_runtime_media_last_input_receive_ticks;
    Uint32 n64_runtime_media_input_ipc_failure_since_ticks;
    unsigned n64_runtime_media_input_ipc_failures;
    uint64_t n64_runtime_media_input_interval_total_ms;
    uint32_t n64_runtime_media_input_interval_samples;
    uint32_t n64_runtime_media_input_interval_max_ms;
    bool n64_runtime_media_saw_positive_video;
    bool runtime_exit_confirming;
    bool runtime_exit_confirm_yes;
    bool runtime_exit_quit_client;
    bool util_escape_held, util_screenshot_held;
} IntegralN64RoomState;
typedef struct IntegralRoomContext IntegralRoomContext;
int n64_room_local_user_index(const IntegralRoomContext *state);
bool sync_n64_room_state(IntegralRoomContext *state, bool ready);
bool resolve_n64_room_local_outbox(IntegralRoomContext *state);
void cleanup_n64_room_session_files(const char *session_id);
void poll_n64_room_async(IntegralRoomContext *state, Uint32 now);
void activate_n64_room(IntegralRoomContext *state, const IntegralApiRoom *matched_room);
bool n64_room_runtime_active(const IntegralRoomContext *state);
void handle_n64_room_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key);
bool handle_n64_room_util_event(IntegralRoomContext *state, const SDL_Event *event);
void clear_secret(char *value, size_t value_size);
void request_runtime_exit_confirmation(IntegralRoomContext *state, bool quit_client);
bool handle_runtime_exit_confirmation_event(IntegralRoomContext *state,
                                                   const SDL_Event *event);
N64RuntimeStopResult stop_n64_runtime_media_host_process(IntegralRoomContext *state);
void reset_n64_runtime_media_connection(IntegralRoomContext *state);
void suspend_n64_runtime_media_connection(IntegralRoomContext *state);
bool poll_n64_runtime_media_transport(IntegralRoomContext *state, Uint32 now);
#endif

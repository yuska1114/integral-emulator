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
#include "rom_metadata.h"
#include "../runtimes/gb/src/common/key_config.h"
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
#include "gb_runtime_fixed_host_rom_resolver.h"
#include "gb_runtime_fixed_host_product_runtime.h"
#include "gb_runtime_fixed_host_snapshot_ipc.h"
#include "../runtimes/gb/src/server/gb_link_engine.h"
#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID "integral-gb-runtime-fixed-host-v2"
#endif
#ifdef _WIN32
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME "build/integral_gb_runtime_fixed_host.exe"
#else
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME "build/integral_gb_runtime_fixed_host"
#endif


#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif
static const char *integral_gb_runtime_fixed_host_runtime_path(void);
static const char *integral_gb_runtime_fixed_host_ca_file(void);
static const char *room_link_mode_api_name(IntegralRoomLinkMode mode);
static bool room_link_mode_from_api(const char *mode, IntegralRoomLinkMode *mode_out);
static void cycle_room_link_mode(IntegralRoomContext *state, int delta);
static void set_room_link_mode(IntegralRoomContext *state, IntegralRoomLinkMode mode);
static void mark_room_link_mode_local_override(IntegralRoomContext *state);
static void cycle_room_rom_slot(IntegralRoomContext *state, int delta);
static void sync_room_state(IntegralRoomContext *state);
static bool room_link_session_is_active_on_server(IntegralRoomContext *state, const char *session_id);
#ifndef _WIN32
static ptrdiff_t gb_runtime_fixed_host_fd_write(void *context, const uint8_t *source, size_t size);
#endif
#ifndef _WIN32
static ptrdiff_t gb_runtime_fixed_host_fd_read(void *context, uint8_t *destination, size_t size);
#endif
#ifndef _WIN32
#else
static ptrdiff_t gb_runtime_fixed_host_handle_write(void *context, const uint8_t *source, size_t size);
#endif
#ifndef _WIN32
#else
static ptrdiff_t gb_runtime_fixed_host_handle_read(void *context, uint8_t *destination, size_t size);
#endif
static int gb_runtime_fixed_host_result_reader_thread(void *opaque);
static bool take_gb_runtime_fixed_host_result(IntegralRoomContext *state, IntegralGBRuntimeFixedHostResult *result);
static void gb_runtime_fixed_host_set_child_environment(
    const char *role, const char *relay_host, unsigned relay_port,
    const char *relay_transport,
    const char *session_id, const char *ticket,
    const IntegralGBRuntimeFixedHostRomResolution *resolution,
    const IntegralConfigKeys *keys,
    unsigned window_width, unsigned window_height,
    intptr_t result_handle, const char *save_policy,
    const char *rtc_target_unix, unsigned ir_off_delay_ticks);
#ifdef _WIN32
static void gb_runtime_fixed_host_clear_parent_environment(void);
#endif
static bool start_room_gb_runtime_fixed_host_runtime(
    IntegralRoomContext *state, const char *role, const IntegralGBRuntimeFixedHostRomResolution *resolution);
static void maybe_advance_room_gb_runtime_fixed_host_preflight(IntegralRoomContext *state);
static void maybe_launch_room_host(IntegralRoomContext *state);
static void update_room_ready_status(IntegralRoomContext *state);
static void move_room_selection(IntegralRoomContext *state, int delta);
static const char *integral_gb_runtime_fixed_host_runtime_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME");
    return value && value[0] ? value : INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME;
}


static const char *integral_gb_runtime_fixed_host_ca_file(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA_FILE");
    return value && value[0] ? value : "";
}


static const char *room_link_mode_api_name(IntegralRoomLinkMode mode)
{
    switch (mode) {
        case INTEGRAL_ROOM_MODE_BATTLE:
            return "battle";
        case INTEGRAL_ROOM_MODE_TRADE:
            return "trade";
    }
    return "trade";
}


const char *room_link_mode_label(IntegralRoomLinkMode mode)
{
    switch (mode) {
        case INTEGRAL_ROOM_MODE_BATTLE:
            return "SAVE OFF";
        case INTEGRAL_ROOM_MODE_TRADE:
            return "SAVE ON";
    }
    return "SAVE ON";
}


static bool room_link_mode_from_api(const char *mode, IntegralRoomLinkMode *mode_out)
{
    if (!mode_out) {
        return false;
    }
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


static void cycle_room_link_mode(IntegralRoomContext *state, int delta)
{
    static const IntegralRoomLinkMode order[] = {
        INTEGRAL_ROOM_MODE_BATTLE,
        INTEGRAL_ROOM_MODE_TRADE,
    };
    int current = 0;
    for (int i = 0; i < 2; i++) {
        if (order[i] == state->link.room_link_mode) {
            current = i;
            break;
        }
    }
    int next = current + delta;
    while (next < 0) {
        next += 2;
    }
    next %= 2;
    state->link.room_link_mode = order[next];
    state->common.room_ready_self = false;
    state->common.room_ready_peer = false;
}


static void set_room_link_mode(IntegralRoomContext *state, IntegralRoomLinkMode mode)
{
    state->link.room_link_mode = mode;
    state->common.room_ready_self = false;
    state->common.room_ready_peer = false;
}


static void mark_room_link_mode_local_override(IntegralRoomContext *state)
{
    state->link.room_link_mode_local_override = true;
    state->common.room_ready_self = false;
    state->common.room_ready_peer = false;
}


void set_room_link_session_id(IntegralRoomContext *state, const char *session_id)
{
    if (!session_id || session_id[0] == '\0') {
        return;
    }
    if (strcmp(state->link.room_link_session_id, session_id) == 0) {
        return;
    }
    if (state->link.room_client_pid > 0 && !stop_room_client(state)) return;
    client_room_log(state,
               "room_session_set",
               "old_session=%s new_session=%s",
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
    if (state->link.room_link_session_id[0] == '\0') {
        return;
    }
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
           strcmp(status, "RUNNING") == 0 ||
           strcmp(status, "FINALIZING") == 0 || strcmp(status, "RECOVERING") == 0;
}


void sync_room_ready_flags_from_api(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS || state->login->username[0] == '\0') {
        return;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    int user_position = current_room_user_position(state);
    if (user_position != 1) {
        state->link.room_link_mode_local_override = false;
    }
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
        if (!state->link.room_link_mode_local_override || current_room_game_ended(state) || room->link_session_id[0] != '\0') {
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
    if (state->link.room_game_ended) {
        return true;
    }
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) {
        return false;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    return room->game_started && room->link_session_id[0] == '\0';
}


void mark_room_game_ended_if_used(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) {
        return;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    if ((*state->screen) == SCREEN_N64_ROOM || strcmp(room->room_type, "n64") == 0) {
        /* N64 media sessions do not use a Link Session ID. Their terminal
         * lifecycle is fenced by the media session ID in the heartbeat. */
        return;
    }
    if (!room->game_started || room->link_session_id[0] != '\0') {
        return;
    }
    if (state->link.room_link_session_id[0] != '\0') {
        clear_room_link_session_id(state);
    }
    if (!state->link.room_game_ended) {
        client_room_log(state, "room_mark_game_ended", "room=%u", state->common.room_number);
    }
    state->link.room_game_ended = true;
    state->link.room_start_requested = false;
    copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  USE ANOTHER ROOM");
}


static void cycle_room_rom_slot(IntegralRoomContext *state, int delta)
{
    integral_rom_cycle_room(state->rom_slots, &state->link.room_slot_index,
        state->login->status, sizeof(state->login->status), delta);
}


bool gb_runtime_fixed_host_may_start_for_control_state(
    const char *role, const char *control_state)
{
    if (!role || !control_state) return false;
    if (strcmp(role, "host") == 0) {
        return strcmp(control_state, "READY") == 0;
    }
    if (strcmp(role, "remote") == 0) {
        return strcmp(control_state, "WAITING_PEER") == 0 ||
               strcmp(control_state, "PAUSED_REMOTE") == 0;
    }
    return false;
}


static void sync_room_state(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS || state->login->token[0] == '\0') {
        return;
    }
    char slot_label[32];
    if (room_registered_rom_slot_at(state, state->link.room_slot_index)) {
        snprintf(slot_label, sizeof(slot_label), "ROM%d", state->link.room_slot_index + 1);
    }
    else {
        slot_label[0] = '\0';
    }
    char error[160];
    if (integral_api_update_room_state(state->login->server,
                                        state->login->token,
                                        state->common.room_number,
                                        slot_label,
                                        state->common.room_ready_self ? 1 : 0,
                                        current_room_is_user1(state)
                                            ? room_link_mode_api_name(state->link.room_link_mode)
                                            : NULL,
                                        error,
                                        sizeof(error)) != 0) {
        snprintf(state->login->status, sizeof(state->login->status), "ROOM SYNC FAILED %s", error);
        client_room_log(state,
                   "room_sync_failed",
                   "room=%u slot=%s ready=%d mode=%s error=%s",
                   state->common.room_number,
                   slot_label[0] ? slot_label : "-",
                   state->common.room_ready_self ? 1 : 0,
                   room_link_mode_api_name(state->link.room_link_mode),
                   error);
        return;
    }
    client_room_log(state,
               "room_sync_ok",
               "room=%u slot=%s ready=%d mode=%s",
               state->common.room_number,
               slot_label[0] ? slot_label : "-",
               state->common.room_ready_self ? 1 : 0,
               room_link_mode_api_name(state->link.room_link_mode));
    if ((*state->screen) != SCREEN_ROOM) {
        refresh_room_quiet(state);
    }
}


bool current_room_has_link_session(const IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) {
        return false;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    return room->link_session_id[0] != '\0';
}


static bool room_link_session_is_active_on_server(IntegralRoomContext *state, const char *session_id)
{
    if (!session_id || session_id[0] == '\0' || state->login->token[0] == '\0') {
        return false;
    }
    char status[32];
    char error[160];
    int session_room_number = 0;
    int rc = integral_api_get_link_session_info(state->login->server,
                                           state->login->token,
                                           session_id,
                                           status,
                                           sizeof(status),
                                           &session_room_number,
                                           error,
                                           sizeof(error));
    if (rc != 0) {
        client_room_log(state, "room_session_active_check_failed", "session=%s error=%s", session_id, error);
        return false;
    }
    bool active = link_session_status_is_active(status) && session_room_number == (int)state->common.room_number;
    client_room_log(state,
               "room_session_active_check",
               "session=%s status=%s session_room=%d current_room=%u active=%d",
               session_id,
               status,
               session_room_number,
               state->common.room_number,
               active ? 1 : 0);
    return active;
}


#ifndef _WIN32
static ptrdiff_t gb_runtime_fixed_host_fd_write(void *context, const uint8_t *source, size_t size)
{
    int descriptor = *(const int *)context;
    ssize_t amount;
    do { amount = write(descriptor, source, size); } while (amount < 0 && errno == EINTR);
    return (ptrdiff_t)amount;
}

#endif

#ifndef _WIN32
static ptrdiff_t gb_runtime_fixed_host_fd_read(void *context, uint8_t *destination, size_t size)
{
    int descriptor = *(const int *)context;
    ssize_t amount;
    do { amount = read(descriptor, destination, size); } while (amount < 0 && errno == EINTR);
    return (ptrdiff_t)amount;
}

#endif

#ifndef _WIN32
#else
static ptrdiff_t gb_runtime_fixed_host_handle_write(void *context, const uint8_t *source, size_t size)
{
    HANDLE handle = *(HANDLE *)context;
    DWORD amount = 0u;
    DWORD requested = size > UINT32_MAX ? UINT32_MAX : (DWORD)size;
    if (!WriteFile(handle, source, requested, &amount, NULL)) return -1;
    return (ptrdiff_t)amount;
}

#endif

#ifndef _WIN32
#else
static ptrdiff_t gb_runtime_fixed_host_handle_read(void *context, uint8_t *destination, size_t size)
{
    HANDLE handle = *(HANDLE *)context;
    DWORD amount = 0u;
    DWORD requested = size > UINT32_MAX ? UINT32_MAX : (DWORD)size;
    if (!ReadFile(handle, destination, requested, &amount, NULL)) return -1;
    return (ptrdiff_t)amount;
}

#endif

static int gb_runtime_fixed_host_result_reader_thread(void *opaque)
{
    IntegralRoomContext *state = opaque;
    bool received = false;
    if (!state || state->link.room_gb_runtime_fixed_host_result_read < 0) return 1;
#ifdef _WIN32
    HANDLE handle = (HANDLE)state->link.room_gb_runtime_fixed_host_result_read;
    int event;
    while ((event = integral_gb_runtime_fixed_host_ipc_receive_event(
        gb_runtime_fixed_host_handle_read, &handle, &state->link.room_gb_runtime_fixed_host_result)) == 2)
        SDL_AtomicSet(&state->link.room_gb_runtime_fixed_host_ticket_requested, 1);
    received = event == 1;
    CloseHandle(handle);
#else
    int descriptor = (int)state->link.room_gb_runtime_fixed_host_result_read;
    int event;
    while ((event = integral_gb_runtime_fixed_host_ipc_receive_event(
        gb_runtime_fixed_host_fd_read, &descriptor, &state->link.room_gb_runtime_fixed_host_result)) == 2)
        SDL_AtomicSet(&state->link.room_gb_runtime_fixed_host_ticket_requested, 1);
    received = event == 1;
    close(descriptor);
#endif
    state->link.room_gb_runtime_fixed_host_result_read = -1;
    return received ? 0 : 1;
}

static void service_gb_reconnect_ticket(IntegralRoomContext *state)
{
    if (!SDL_AtomicCAS(&state->link.room_gb_runtime_fixed_host_ticket_requested, 1, 0) ||
        state->link.room_gb_runtime_fixed_host_ticket_write <= 0) return;
    char ticket[INTEGRAL_GB_RECONNECT_TICKET_BYTES] = {0};
    char host[256], transport[16], role[16], scope[64], policy[32], error[192];
    unsigned port = 0;
    if (integral_api_gb_runtime_fixed_host_issue_relay_ticket(
        state->login->server, state->login->token, state->link.room_link_session_id, true,
        host, sizeof(host), &port, transport, sizeof(transport), role, sizeof(role),
        scope, sizeof(scope), ticket, sizeof(ticket), policy, sizeof(policy), error, sizeof(error)) != 0 ||
        strcmp(role, state->link.room_gb_runtime_fixed_host_role) ||
        strcmp(scope, "gb-runtime-fixed-host-media-v1") ||
        strcmp(policy, state->link.room_gb_runtime_fixed_host_save_policy))
        memset(ticket, 0, sizeof(ticket));
#ifdef _WIN32
    HANDLE handle = (HANDLE)state->link.room_gb_runtime_fixed_host_ticket_write;
    (void)gb_runtime_fixed_host_handle_write(&handle, (const uint8_t *)ticket, sizeof(ticket));
#else
    int descriptor = (int)state->link.room_gb_runtime_fixed_host_ticket_write;
    (void)gb_runtime_fixed_host_fd_write(&descriptor, (const uint8_t *)ticket, sizeof(ticket));
#endif
    clear_secret(ticket, sizeof(ticket));
}

static bool take_gb_runtime_fixed_host_result(IntegralRoomContext *state, IntegralGBRuntimeFixedHostResult *result)
{
    int thread_status = 1;
    if (!state || !result || !state->link.room_gb_runtime_fixed_host_result_thread) return false;
    SDL_WaitThread(state->link.room_gb_runtime_fixed_host_result_thread, &thread_status);
    state->link.room_gb_runtime_fixed_host_result_thread = NULL;
    if (state->link.room_gb_runtime_fixed_host_ticket_write > 0) {
#ifdef _WIN32
        CloseHandle((HANDLE)state->link.room_gb_runtime_fixed_host_ticket_write);
#else
        close((int)state->link.room_gb_runtime_fixed_host_ticket_write);
#endif
        state->link.room_gb_runtime_fixed_host_ticket_write = -1;
    }
    if (thread_status != 0) {
        integral_gb_runtime_fixed_host_result_release(&state->link.room_gb_runtime_fixed_host_result);
        return false;
    }
    *result = state->link.room_gb_runtime_fixed_host_result;
    memset(&state->link.room_gb_runtime_fixed_host_result, 0, sizeof(state->link.room_gb_runtime_fixed_host_result));
    return true;
}


void discard_gb_runtime_fixed_host_result(IntegralRoomContext *state)
{
    IntegralGBRuntimeFixedHostResult result = {0};
    if (!state) return;
    if (state->link.room_gb_runtime_fixed_host_ticket_write > 0) {
#ifdef _WIN32
        CloseHandle((HANDLE)state->link.room_gb_runtime_fixed_host_ticket_write);
#else
        close((int)state->link.room_gb_runtime_fixed_host_ticket_write);
#endif
        state->link.room_gb_runtime_fixed_host_ticket_write = -1;
    }
    if (state->link.room_gb_runtime_fixed_host_result_thread) {
        (void)take_gb_runtime_fixed_host_result(state, &result);
        integral_gb_runtime_fixed_host_result_release(&result);
    }
    else if (state->link.room_gb_runtime_fixed_host_result_read >= 0) {
#ifdef _WIN32
        CloseHandle((HANDLE)state->link.room_gb_runtime_fixed_host_result_read);
#else
        close((int)state->link.room_gb_runtime_fixed_host_result_read);
#endif
        state->link.room_gb_runtime_fixed_host_result_read = -1;
    }
    integral_gb_runtime_fixed_host_result_release(&state->link.room_gb_runtime_fixed_host_result);
}


bool handle_gb_runtime_fixed_host_trade_result(IntegralRoomContext *state)
{
    IntegralGBRuntimeFixedHostResult result = {0};
    char control_state[24] = {0}, error[192] = {0};
    if (!take_gb_runtime_fixed_host_result(state, &result) ||
        result.host != (strcmp(state->link.room_gb_runtime_fixed_host_role, "host") == 0)) {
        integral_gb_runtime_fixed_host_result_release(&result);
        return false;
    }
    int submitted = result.host
        ? integral_api_gb_runtime_fixed_host_submit_host_finish(
              state->login->server, state->login->token,
              state->link.room_link_session_id, state->link.room_game_session_id,
              state->link.room_fencing_token, result.final_frame,
              result.terminal_digest, result.candidates.host_data,
              result.candidates.host_size, result.candidates.remote_data,
              result.candidates.remote_size, control_state,
              sizeof(control_state), error, sizeof(error))
        : integral_api_gb_runtime_fixed_host_submit_terminal_receipt(
              state->login->server, state->login->token,
              state->link.room_link_session_id, result.final_frame,
              result.terminal_digest, control_state,
              sizeof(control_state), error, sizeof(error));
    integral_gb_runtime_fixed_host_result_release(&result);
    if (submitted != 0) {
        client_room_log(state, "gb_runtime_fixed_host_trade_finish_failed", "error=%s", error);
        return false;
    }
    for (unsigned attempt = 0u;
         strcmp(control_state, "FINISHED") != 0 && attempt < 50u; attempt++) {
        char role[16], digest[80], host_title[65], remote_title[65], build[128];
        char host_platform[4], remote_platform[4];
        char host_header[21], remote_header[21];
        unsigned pause = 0u;
        SDL_Delay(100u);
        if (integral_api_gb_runtime_fixed_host_get_manifest(
                state->login->server, state->login->token,
                state->link.room_link_session_id, role, sizeof(role), digest,
                sizeof(digest), host_title, sizeof(host_title), remote_title,
                sizeof(remote_title), host_platform, sizeof(host_platform),
                remote_platform, sizeof(remote_platform),
                host_header, sizeof(host_header), remote_header, sizeof(remote_header),
                build, sizeof(build), control_state,
                sizeof(control_state), &pause, NULL, 0, error, sizeof(error)) != 0) break;
    }
    if (strcmp(control_state, "FINISHED") != 0) {
        client_room_log(state, "gb_runtime_fixed_host_trade_commit_pending", "state=%s", control_state);
        copy_text(state->login->status, sizeof(state->login->status),
                  "TRADE FINALIZING - CHECK SERVER");
        return true;
    }
    client_room_log(state, "gb_runtime_fixed_host_trade_committed", "session=%s",
               state->link.room_link_session_id);
    copy_text(state->login->status, sizeof(state->login->status), "TRADE SAVED - BOTH PLAYERS");
    return true;
}


const char *gb_runtime_fixed_host_key_spec(
    const IntegralConfigKeys *keys)
{
    return keys ? keys->slot1 : "";
}


static void gb_runtime_fixed_host_set_child_environment(
    const char *role, const char *relay_host, unsigned relay_port,
    const char *relay_transport,
    const char *session_id, const char *ticket,
    const IntegralGBRuntimeFixedHostRomResolution *resolution,
    const IntegralConfigKeys *keys,
    unsigned window_width, unsigned window_height,
    intptr_t result_handle, const char *save_policy,
    const char *rtc_target_unix, unsigned ir_off_delay_ticks)
{
    char port[16];
    char ir_delay_text[16];
    snprintf(ir_delay_text, sizeof(ir_delay_text), "%u", ir_off_delay_ticks);
    char result_handle_text[32];
    char window_width_text[16];
    char window_height_text[16];
    const char *game_keys = gb_runtime_fixed_host_key_spec(keys);
    snprintf(port, sizeof(port), "%u", relay_port);
    snprintf(result_handle_text, sizeof(result_handle_text), "%lld",
             (long long)result_handle);
    snprintf(window_width_text, sizeof(window_width_text), "%u", window_width);
    snprintf(window_height_text, sizeof(window_height_text), "%u", window_height);
#ifdef _WIN32
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE", role);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_IR_OFF_DELAY_TICKS", ir_delay_text);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST", relay_host);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT", port);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_TRANSPORT", relay_transport);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION", session_id);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET", ticket);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA", integral_gb_runtime_fixed_host_ca_file());
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1",
                            resolution ? resolution->path_a : NULL);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2",
                            resolution ? resolution->path_b : NULL);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS", game_keys);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SCREENSHOT_KEY", keys->screenshot);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ESCAPE_KEY", keys->escape);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE", result_handle_text);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY", save_policy);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RTC_TARGET_UNIX",
                            rtc_target_unix);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_WINDOW_WIDTH", window_width_text);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_WINDOW_HEIGHT", window_height_text);
#else
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE", role, 1);
    setenv("INTEGRAL_EMULATOR_GB_IR_OFF_DELAY_TICKS", ir_delay_text, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST", relay_host, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT", port, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_TRANSPORT", relay_transport, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION", session_id, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET", ticket, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA", integral_gb_runtime_fixed_host_ca_file(), 1);
    if (resolution) {
        setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1", resolution->path_a, 1);
        setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2", resolution->path_b, 1);
    }
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS", game_keys, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SCREENSHOT_KEY", keys->screenshot, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ESCAPE_KEY", keys->escape, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE", result_handle_text, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY", save_policy, 1);
    if (rtc_target_unix) {
        setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RTC_TARGET_UNIX",
               rtc_target_unix, 1);
    }
    else {
        unsetenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RTC_TARGET_UNIX");
    }
    setenv("INTEGRAL_EMULATOR_GB_WINDOW_WIDTH", window_width_text, 1);
    setenv("INTEGRAL_EMULATOR_GB_WINDOW_HEIGHT", window_height_text, 1);
#endif
}


#ifdef _WIN32
static void gb_runtime_fixed_host_clear_parent_environment(void)
{
    static const char *names[] = {
        "INTEGRAL_EMULATOR_GB_IR_OFF_DELAY_TICKS",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_TRANSPORT",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SCREENSHOT_KEY",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ESCAPE_KEY",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RTC_TARGET_UNIX",
        "INTEGRAL_EMULATOR_GB_WINDOW_WIDTH",
        "INTEGRAL_EMULATOR_GB_WINDOW_HEIGHT",
    };
    for (size_t index = 0u; index < sizeof(names) / sizeof(names[0]); index++)
        SetEnvironmentVariableA(names[index], NULL);
}

#endif

static bool start_room_gb_runtime_fixed_host_runtime(
    IntegralRoomContext *state, const char *role, const IntegralGBRuntimeFixedHostRomResolution *resolution)
{
    char relay_host[128] = {0}, relay_transport[8] = {0};
    char ticket_role[16] = {0}, scope[48] = {0};
    char ticket[192] = {0}, error[192] = {0};
    char save_policy[32] = {0};
    unsigned relay_port = 0u;
    IntegralGBRuntimeFixedHostSnapshotPair snapshots = {0};
    unsigned window_width = 0u, window_height = 0u;
    long long rtc_target_unix = 0;
    char rtc_target_text[32] = {0};
    bool host = strcmp(role, "host") == 0;
    if (state->link.room_client_started) return true;
    client_room_window_size(state, &window_width, &window_height);
    if (access(integral_gb_runtime_fixed_host_runtime_path(), X_OK) != 0) {
        copy_text(state->login->status, sizeof(state->login->status),
                  "FIXED HOST RUNTIME NOT FOUND");
        return false;
    }
    if (host) {
        snapshots.host_data = malloc(INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX);
        snapshots.remote_data = malloc(INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX);
        if (!snapshots.host_data || !snapshots.remote_data ||
            integral_api_gb_runtime_fixed_host_download_snapshots(
                state->login->server, state->login->token,
                state->link.room_link_session_id,
                snapshots.host_data, INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX,
                &snapshots.host_size,
                snapshots.remote_data, INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX,
                &snapshots.remote_size, error, sizeof(error)) != 0) {
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            snprintf(state->login->status, sizeof(state->login->status),
                     "FIXED HOST SNAPSHOT FAILED %.96s", error);
            return false;
        }
        IntegralGBRuntimeLinkSavePreflightStatus host_save_status =
            integral_gb_runtime_link_save_preflight(
                resolution->path_a, snapshots.host_size);
        IntegralGBRuntimeLinkSavePreflightStatus remote_save_status =
            integral_gb_runtime_link_save_preflight(
                resolution->path_b, snapshots.remote_size);
        if (host_save_status != INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK ||
            remote_save_status != INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK) {
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            state->link.room_gb_runtime_save_preflight_blocked = true;
            const char *blocked_reason =
                (host_save_status == INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_ROM_ERROR ||
                 remote_save_status == INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_ROM_ERROR)
                ? "rom_unreadable" : "rtc_save_required";
            if (integral_api_gb_runtime_fixed_host_block(state->login->server, state->login->token,
                    state->link.room_link_session_id, blocked_reason, error, sizeof(error)) != 0)
                copy_text(state->link.preflight_block_pending, sizeof(state->link.preflight_block_pending), blocked_reason);
            if (host_save_status == INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_ROM_ERROR ||
                remote_save_status == INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_ROM_ERROR) {
                copy_text(state->login->status, sizeof(state->login->status),
                          "ROM CHECK FAILED - LEAVE ROOM AND RETRY");
            }
            else {
                copy_text(state->login->status, sizeof(state->login->status),
                          "RTC SAV NOT READY - RUN LOCAL ONCE, EXIT NORMALLY, THEN RETRY");
            }
            client_room_log(state, "gb_runtime_fixed_host_save_preflight_failed",
                       "host_status=%d remote_status=%d",
                       (int)host_save_status, (int)remote_save_status);
            return false;
        }
        if (integral_api_get_server_time(
                state->login->server, &rtc_target_unix,
                error, sizeof(error)) != 0 || rtc_target_unix <= 0) {
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            snprintf(state->login->status, sizeof(state->login->status),
                     "FIXED HOST SERVER TIME FAILED %.88s", error);
            client_room_log(state, "gb_runtime_fixed_host_server_time_failed",
                       "error=%s", error);
            return false;
        }
        snprintf(rtc_target_text, sizeof(rtc_target_text), "%lld", rtc_target_unix);
    }
    if (integral_api_gb_runtime_fixed_host_issue_relay_ticket(
            state->login->server, state->login->token,
            state->link.room_link_session_id, false, relay_host, sizeof(relay_host),
            &relay_port, relay_transport, sizeof(relay_transport),
            ticket_role, sizeof(ticket_role), scope, sizeof(scope),
            ticket, sizeof(ticket),
            save_policy, sizeof(save_policy),
            error, sizeof(error)) != 0 ||
        strcmp(ticket_role, role) != 0 ||
        strcmp(scope, "gb-runtime-fixed-host-media-v1") != 0) {
        integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
        snprintf(state->login->status, sizeof(state->login->status),
                 "FIXED HOST TICKET FAILED %.96s", error);
        memset(ticket, 0, sizeof(ticket));
        return false;
    }
#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
        HANDLE child_input = NULL, parent_write = NULL;
        HANDLE parent_result_read = NULL, child_result_write = NULL;
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        char command[INTEGRAL_CONFIG_PATH_MAX + 64];
        bool created;
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        if (!CreatePipe(&parent_result_read, &child_result_write, &security, 0u) ||
            !SetHandleInformation(parent_result_read, HANDLE_FLAG_INHERIT, 0u) ||
            !CreatePipe(&child_input, &parent_write, &security, 0u) ||
            !SetHandleInformation(parent_write, HANDLE_FLAG_INHERIT, 0u)) {
            if (parent_result_read) CloseHandle(parent_result_read);
            if (child_result_write) CloseHandle(child_result_write);
            if (child_input) CloseHandle(child_input);
            if (parent_write) CloseHandle(parent_write);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = child_input;
            startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
        gb_runtime_fixed_host_set_child_environment(role, relay_host, relay_port,
                                         relay_transport,
                                         state->link.room_link_session_id, ticket,
                                         resolution, state->keys,
                                         window_width, window_height,
                                         (intptr_t)child_result_write, save_policy,
                                         host ? rtc_target_text : NULL,
                                         integral_config_ir_off_delay(state->config_path));
        snprintf(command, sizeof(command), "\"%s\"%s",
                 integral_gb_runtime_fixed_host_runtime_path(),
                 host ? " --snapshot-stdin" : "");
        created = CreateProcessA(integral_gb_runtime_fixed_host_runtime_path(), command,
                                 NULL, NULL, TRUE, 0u, NULL, NULL,
                                 &startup, &process) != 0;
        gb_runtime_fixed_host_clear_parent_environment();
        if (child_input) CloseHandle(child_input);
        if (child_result_write) CloseHandle(child_result_write);
        if (!created || (host && !integral_gb_runtime_fixed_host_snapshot_ipc_send(
                gb_runtime_fixed_host_handle_write, &parent_write, &snapshots))) {
            if (created) { TerminateProcess(process.hProcess, 1u); CloseHandle(process.hProcess); }
            if (process.hThread) CloseHandle(process.hThread);
            if (parent_write) CloseHandle(parent_write);
            if (parent_result_read) CloseHandle(parent_result_read);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        state->link.room_gb_runtime_fixed_host_ticket_write = (intptr_t)parent_write;
        CloseHandle(process.hThread);
        state->link.room_client_pid = (IntegralChildProcess)(intptr_t)process.hProcess;
        state->link.room_gb_runtime_fixed_host_result_read = (intptr_t)parent_result_read;
    }
#else
    {
        int descriptors[2] = {-1, -1};
        int result_descriptors[2] = {-1, -1};
        signal(SIGPIPE, SIG_IGN);
        pid_t child;
        if (pipe(result_descriptors) != 0 || pipe(descriptors) != 0) {
            if (result_descriptors[0] >= 0) close(result_descriptors[0]);
            if (result_descriptors[1] >= 0) close(result_descriptors[1]);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        child = fork();
        if (child == 0) {
            close(result_descriptors[0]);
            {
                close(descriptors[1]);
                dup2(descriptors[0], STDIN_FILENO);
                close(descriptors[0]);
            }
            gb_runtime_fixed_host_set_child_environment(role, relay_host, relay_port,
                                             relay_transport,
                                             state->link.room_link_session_id, ticket,
                                             resolution, state->keys,
                                             window_width, window_height,
                                             (intptr_t)result_descriptors[1], save_policy,
                                             host ? rtc_target_text : NULL,
                                             integral_config_ir_off_delay(state->config_path));
            redirect_child_output_to_client_log();
            if (host)
                execl(integral_gb_runtime_fixed_host_runtime_path(),
                      integral_gb_runtime_fixed_host_runtime_path(),
                      "--snapshot-stdin", (char *)NULL);
            else
                execl(integral_gb_runtime_fixed_host_runtime_path(),
                      integral_gb_runtime_fixed_host_runtime_path(), (char *)NULL);
            _exit(127);
        }
        if (child < 0) {
            close(result_descriptors[0]); close(result_descriptors[1]);
            close(descriptors[0]); close(descriptors[1]);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        close(result_descriptors[1]);
        close(descriptors[0]);
        if (host) {
            if (!integral_gb_runtime_fixed_host_snapshot_ipc_send(
                    gb_runtime_fixed_host_fd_write, &descriptors[1], &snapshots)) {
                close(descriptors[1]);
                close(result_descriptors[0]);
                kill(child, SIGTERM);
                (void)waitpid(child, NULL, 0);
                memset(ticket, 0, sizeof(ticket));
                return false;
            }
        }
        state->link.room_gb_runtime_fixed_host_ticket_write = descriptors[1];
        state->link.room_client_pid = child;
        state->link.room_gb_runtime_fixed_host_result_read = result_descriptors[0];
    }
#endif
    memset(ticket, 0, sizeof(ticket));
    memset(&state->link.room_gb_runtime_fixed_host_result, 0, sizeof(state->link.room_gb_runtime_fixed_host_result));
    state->link.room_gb_runtime_fixed_host_result_thread = SDL_CreateThread(
        gb_runtime_fixed_host_result_reader_thread, "gb-runtime-fixed-host-result", state);
    if (!state->link.room_gb_runtime_fixed_host_result_thread) {
#ifdef _WIN32
        TerminateProcess((HANDLE)state->link.room_client_pid, 1u);
        WaitForSingleObject((HANDLE)state->link.room_client_pid, 5000u);
        CloseHandle((HANDLE)state->link.room_client_pid);
#else
        kill(state->link.room_client_pid, SIGTERM);
        (void)waitpid(state->link.room_client_pid, NULL, 0);
#endif
        discard_gb_runtime_fixed_host_result(state);
        state->link.room_client_pid = 0;
        copy_text(state->login->status, sizeof(state->login->status),
                  "FIXED HOST RESULT READER FAILED");
        return false;
    }
    state->link.room_client_started = true;
    state->link.room_gb_runtime_fixed_host_active = true;
    copy_text(state->link.room_gb_runtime_fixed_host_role, sizeof(state->link.room_gb_runtime_fixed_host_role), role);
    copy_text(state->link.room_gb_runtime_fixed_host_save_policy,
              sizeof(state->link.room_gb_runtime_fixed_host_save_policy), save_policy);
    copy_text(state->login->status, sizeof(state->login->status),
              strcmp(relay_transport, "plain") == 0
                  ? "WARNING PLAIN LOCAL NETWORK  FIXED HOST RUNNING"
                  : (host ? "FIXED HOST BATTLE RUNNING" : "FIXED HOST REMOTE RUNNING"));
    client_room_log(state, "gb_runtime_fixed_host_runtime_started",
               "role=%s session=%s transport=%s rtc_target_unix=%lld",
               role, state->link.room_link_session_id, relay_transport,
               rtc_target_unix);
    return true;
}


static void maybe_advance_room_gb_runtime_fixed_host_preflight(IntegralRoomContext *state)
{
    char role[16] = {0};
    char digest[80] = {0};
    char host_game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX] = {0};
    char remote_game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX] = {0};
    char host_platform[INTEGRAL_API_ROM_PLATFORM_MAX] = {0};
    char remote_platform[INTEGRAL_API_ROM_PLATFORM_MAX] = {0};
    char host_header_title[INTEGRAL_API_ROM_HEADER_TITLE_MAX] = {0};
    char remote_header_title[INTEGRAL_API_ROM_HEADER_TITLE_MAX] = {0};
    char runtime_build_id[128] = {0};
    char control_state[24] = {0};
    unsigned pause_remaining_seconds = 0u;
    char blocked_reason[64] = {0};
    char error[160] = {0};
    IntegralGBRuntimeFixedHostRomResolution resolution;
    IntegralGBRuntimeFixedHostRomResolveStatus rom_status;
    if (integral_api_gb_runtime_fixed_host_get_manifest(
            state->login->server, state->login->token,
            state->link.room_link_session_id, role, sizeof(role), digest,
            sizeof(digest), host_game_type, sizeof(host_game_type), remote_game_type,
            sizeof(remote_game_type), host_platform, sizeof(host_platform),
            remote_platform, sizeof(remote_platform),
            host_header_title, sizeof(host_header_title),
            remote_header_title, sizeof(remote_header_title),
            runtime_build_id, sizeof(runtime_build_id),
            control_state, sizeof(control_state),
            &pause_remaining_seconds,
            blocked_reason, sizeof(blocked_reason),
            error, sizeof(error)) != 0) {
        snprintf(state->login->status, sizeof(state->login->status),
                 "FIXED HOST MANIFEST FAILED %.96s", error);
        return;
    }
    if (strcmp(control_state, "BLOCKED") == 0) {
        state->link.room_gb_runtime_save_preflight_blocked = true;
        copy_text(state->login->status, sizeof(state->login->status),
            strcmp(blocked_reason, "rtc_save_required") == 0
                ? "LEAVE ROOM - RUN LOCAL AND EXIT NORMALLY FIRST"
                : "ROM CHECK FAILED - LEAVE ROOM AND RETRY");
        client_room_log(state, "preflight_blocked", "session=%s role=%s reason=%s",
            state->link.room_link_session_id, role, blocked_reason);
        return;
    }
    if (strcmp(role, "host") == 0) {
        rom_status = integral_gb_runtime_fixed_host_rom_resolve_local(
            state->rom_slots, state->server_rom_slots, INTEGRAL_ROM_SLOTS,
            host_game_type, host_platform, host_header_title,
            remote_game_type, remote_platform, remote_header_title,
            &resolution);
    }
    else if (strcmp(role, "remote") == 0) {
        memset(&resolution, 0, sizeof(resolution));
        rom_status = INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK;
    }
    else {
        copy_text(state->login->status, sizeof(state->login->status),
                  "FIXED HOST ROLE INVALID");
        return;
    }
    if (rom_status != INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK) {
        snprintf(state->login->status, sizeof(state->login->status),
                 "FIXED HOST ROM CHECK %.96s",
                 integral_gb_runtime_fixed_host_rom_resolve_status_text(rom_status));
        return;
    }
    if (strcmp(runtime_build_id, INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID) != 0) {
        copy_text(state->login->status, sizeof(state->login->status),
                  "FIXED HOST CLIENT UPDATE REQUIRED");
        client_room_log(state, "gb_runtime_fixed_host_runtime_build_mismatch",
                   "server=%s client=%s", runtime_build_id,
                   INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID);
        return;
    }
    if (strcmp(control_state, "PREFLIGHT") == 0 ||
        strcmp(control_state, "READY") == 0) {
        if (integral_api_gb_runtime_fixed_host_submit_preflight(
                state->login->server, state->login->token,
                state->link.room_link_session_id, digest,
                INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID,
                strcmp(role, "host") == 0 ? host_game_type : "",
                strcmp(role, "host") == 0 ? host_platform : "",
                strcmp(role, "host") == 0 ? host_header_title : "",
                strcmp(role, "host") == 0 ? remote_game_type : "",
                strcmp(role, "host") == 0 ? remote_platform : "",
                strcmp(role, "host") == 0 ? remote_header_title : "",
                control_state, sizeof(control_state),
                error, sizeof(error)) != 0) {
            snprintf(state->login->status, sizeof(state->login->status),
                     "FIXED HOST PREFLIGHT FAILED %.96s", error);
            return;
        }
    }
    /* The Host downloads and validates both SAVs before issuing the first
       relay ticket.  Remote therefore waits for WAITING_PEER rather than
       acquiring a ticket while the Host is still validating READY. */
    if (gb_runtime_fixed_host_may_start_for_control_state(role, control_state)) {
        if (integral_api_get_link_game_fence(
                state->login->server, state->login->token,
                state->link.room_link_session_id,
                state->link.room_game_session_id,
                sizeof(state->link.room_game_session_id),
                &state->link.room_fencing_token,
                error, sizeof(error)) != 0) {
            snprintf(state->login->status, sizeof(state->login->status),
                     "FIXED HOST FENCE FAILED %.96s", error);
            client_room_log(state, "gb_runtime_fixed_host_fence_failed",
                       "session=%s error=%s", state->link.room_link_session_id,
                       error);
            return;
        }
        (void)start_room_gb_runtime_fixed_host_runtime(
            state, role, strcmp(role, "host") == 0 ? &resolution : NULL);
        return;
    }
    if (strcmp(control_state, "PAUSED_REMOTE") == 0) {
        snprintf(state->login->status, sizeof(state->login->status),
                 "REMOTE RECONNECTING - %u SEC", pause_remaining_seconds);
        return;
    }
    if (strcmp(control_state, "RUNNING") == 0 && state->link.room_client_started) {
        return;
    }
    copy_text(state->login->status, sizeof(state->login->status),
              "FIXED HOST WAITING PEER");
}


static void maybe_launch_room_host(IntegralRoomContext *state)
{
    if (state->link.room_link_session_id[0] == '\0' || state->login->token[0] == '\0') {
        client_room_log(state,
                   "room_launch_skip",
                   "reason=missing_session_or_token session=%s token=%s",
                   state->link.room_link_session_id[0] ? state->link.room_link_session_id : "-",
                   state->login->token[0] ? "present" : "missing");
        return;
    }
    if (state->link.room_gb_runtime_save_preflight_blocked) {
        if (state->link.preflight_block_pending[0]) {
            char error[160];
            if (integral_api_gb_runtime_fixed_host_block(state->login->server, state->login->token,
                    state->link.room_link_session_id, state->link.preflight_block_pending,
                    error, sizeof(error)) == 0)
                state->link.preflight_block_pending[0] = '\0';
        }
        return;
    }
    {
        char protocol_id[32];
        char protocol_error[160];
        if (integral_api_get_link_session_protocol(
                state->login->server, state->login->token,
                state->link.room_link_session_id, protocol_id,
                sizeof(protocol_id), protocol_error,
                sizeof(protocol_error)) != 0) {
            snprintf(state->login->status, sizeof(state->login->status),
                     "PROTOCOL CHECK FAILED %s", protocol_error);
            client_room_log(state, "room_protocol_check_failed",
                       "session=%s error=%s",
                       state->link.room_link_session_id, protocol_error);
            return;
        }
        if (strcmp(protocol_id, "gb_runtime_fixed_host_v1") != 0) {
            copy_text(state->login->status, sizeof(state->login->status),
                      "CLIENT CAPABILITY MISMATCH");
            client_room_log(state, "room_protocol_rejected",
                       "session=%s protocol=%s",
                       state->link.room_link_session_id, protocol_id);
            return;
        }
        maybe_advance_room_gb_runtime_fixed_host_preflight(state);
        return;
    }
}


void maybe_start_room_session(IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS || state->login->token[0] == '\0') {
        return;
    }
    if (state->link.room_game_ended) {
        return;
    }
    if (state->link.room_link_session_id[0] != '\0') {
        if (!room_link_session_is_active_on_server(state, state->link.room_link_session_id)) {
            client_room_log(state, "room_local_session_stale_before_retry", "session=%s", state->link.room_link_session_id);
            clear_room_link_session_id(state);
        }
        else {
            client_room_log(state,
                       "room_local_session_retry",
                       "session=%s ready_self=%d ready_peer=%d status=%s",
                       state->link.room_link_session_id,
                       state->common.room_ready_self ? 1 : 0,
                       state->common.room_ready_peer ? 1 : 0,
                       state->login->status);
            maybe_launch_room_host(state);
            return;
        }
    }
    const IntegralApiRoom *room = &state->common.current_room;
    if (room->link_session_id[0] != '\0') {
        if (!room_link_session_is_active_on_server(state, room->link_session_id)) {
            client_room_log(state, "room_server_session_stale_before_reuse", "session=%s", room->link_session_id);
        }
        else {
            client_room_log(state,
                       "room_existing_session_seen",
                       "session=%s ready_self=%d ready_peer=%d",
                       room->link_session_id,
                       state->common.room_ready_self ? 1 : 0,
                       state->common.room_ready_peer ? 1 : 0);
            set_room_link_session_id(state, room->link_session_id);
            maybe_launch_room_host(state);
            if (state->login->status[0] == '\0') {
                copy_text(state->login->status, sizeof(state->login->status), "LINK SESSION READY");
            }
            return;
        }
    }
    if (!current_room_is_ready_to_start(state) || state->link.room_start_requested) {
        client_room_log(state,
                   "room_start_skip",
                   "ready_to_start=%d start_requested=%d ready_self=%d ready_peer=%d",
                   current_room_is_ready_to_start(state) ? 1 : 0,
                   state->link.room_start_requested ? 1 : 0,
                   state->common.room_ready_self ? 1 : 0,
                   state->common.room_ready_peer ? 1 : 0);
        return;
    }

    state->link.room_start_requested = true;
    client_room_log(state, "room_start_request", "room=%u", state->common.room_number);
    char session_id[INTEGRAL_API_ROOM_SESSION_MAX];
    char error[160];
    if (integral_api_start_room(state->login->server,
                                 state->login->token,
                                 state->common.room_number,
                                 room_link_mode_api_name(state->link.room_link_mode),
                                 session_id,
                                 sizeof(session_id),
                                 error,
                                 sizeof(error)) != 0) {
        state->link.room_start_requested = false;
        snprintf(state->login->status, sizeof(state->login->status), "ROOM START FAILED %s", error);
        client_room_log(state, "room_start_failed", "room=%u error=%s", state->common.room_number, error);
        return;
    }
    client_room_log(state, "room_start_ok", "room=%u session=%s", state->common.room_number, session_id);
    set_room_link_session_id(state, session_id);
    maybe_launch_room_host(state);
    refresh_room_quiet(state);
    if (state->login->status[0] == '\0') {
        copy_text(state->login->status, sizeof(state->login->status), "LINK SESSION READY");
    }
}


void activate_link_room(IntegralRoomContext *state, const IntegralApiRoom *matched_room)
{
    if (state->link.room_client_pid > 0 && !stop_room_client(state)) return;
    IntegralApiRoom room_copy = *matched_room;
    unsigned room_number = room_copy.room_number;
    IntegralRoomLinkMode server_mode;
    if (!room_link_mode_from_api(room_copy.link_mode, &server_mode)) {
        copy_text(state->login->status, sizeof(state->login->status), "ROOM MODE INVALID");
        client_room_log(state, "room_mode_invalid", "value=%s", room_copy.link_mode);
        return;
    }
    memset(&state->common.current_room, 0, sizeof(state->common.current_room));
    state->common.current_room = room_copy;
    (*state->screen) = SCREEN_ROOM;
    state->common.room_number = room_number;
    state->common.room_selected = 0;
    state->common.room_chat_editing = false;
    state->common.room_chat_scroll = 0;
    state->common.room_chat_input[0] = '\0';
    state->common.room_chat_composition[0] = '\0';
    memset(state->common.room_chat_log, 0, sizeof(state->common.room_chat_log));
    state->link.room_link_mode = server_mode;
    state->link.room_link_mode_local_override = false;
    state->link.room_slot_index = room_registered_rom_slot_at(state, state->local_slot_indices[0]) ? state->local_slot_indices[0] : -1;
    state->common.room_ready_self = false;
    state->common.room_ready_peer = false;
    state->common.room_heartbeat_failures = 0;
    state->common.room_heartbeat_attempted = false;
    state->common.room_lifecycle_status[0] = '\0';
    state->common.room_termination_reason[0] = '\0';
    state->common.room_remaining_seconds = -1;
    state->link.room_link_session_id[0] = '\0';
    state->link.room_game_session_id[0] = '\0';
    state->link.room_fencing_token = 0;
    state->link.room_start_requested = false;
    state->link.room_client_started = false;
    state->link.room_gb_runtime_save_preflight_blocked = false;
    state->link.room_game_ended = false;
    state->link.preflight_block_pending[0] = '\0';
    state->link.room_link_mode_local_override = false;
    state->link.room_client_pid = 0;
    state->link.room_session_missing_since_ticks = 0;
    sync_room_state(state);
    mark_room_game_ended_if_used(state);
    if (!current_room_game_ended(state) && strncmp(state->login->status, "ROOM SYNC FAILED", 16) != 0) {
        copy_text(state->login->status, sizeof(state->login->status), "ENTERED ROOM");
    }
    client_room_log(state,
               "room_activate",
               "room=%u slot_index=%d status=%s",
               room_number,
               state->link.room_slot_index,
               state->login->status);
}


static void update_room_ready_status(IntegralRoomContext *state)
{
    if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
        return;
    }
    if (current_room_has_link_session(state)) {
        maybe_start_room_session(state);
        return;
    }
    if (current_room_is_ready_to_start(state)) {
        maybe_start_room_session(state);
        if (state->link.room_link_session_id[0] == '\0' && state->link.room_start_requested) {
            copy_text(state->login->status, sizeof(state->login->status), "BOTH READY  SERVER START PENDING");
        }
        return;
    }
    if (state->common.room_ready_self && state->common.room_ready_peer) {
        copy_text(state->login->status, sizeof(state->login->status), "BOTH READY  EMULATOR START");
    }
    else if (state->common.room_ready_self) {
        copy_text(state->login->status, sizeof(state->login->status), "READY  WAITING USER2");
    }
    else {
        copy_text(state->login->status, sizeof(state->login->status), "ROOM");
    }
}


bool stop_room_client(IntegralRoomContext *state)
{
    if (state->link.room_client_pid <= 0) {
        return true;
    }

    client_room_log(state, "room_client_stop_request", "pid=%ld session=%s", (long)state->link.room_client_pid, state->link.room_link_session_id);
#ifdef _WIN32
    HANDLE process = (HANDLE)(intptr_t)state->link.room_client_pid;
    DWORD wait_result = WaitForSingleObject(process, 5000);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process, 1);
        wait_result = WaitForSingleObject(process, 5000);
    }
    if (wait_result != WAIT_OBJECT_0) {
        client_room_log(state, "room_client_stop_failed", "pid=%ld", (long)state->link.room_client_pid);
        copy_text(state->login->status, sizeof(state->login->status), "GAME STOP FAILED");
        return false;
    }
    CloseHandle(process);
#else
    errno = 0;
    int sent = kill(state->link.room_client_pid, SIGTERM);
    int signal_error = sent < 0 ? errno : 0;
    client_room_log(state, "room_client_signal", "pid=%ld signal=TERM result=%d errno=%d",
                    (long)state->link.room_client_pid, sent, signal_error);
    bool exited = false;
    for (unsigned attempt = 0; attempt < 50; attempt++) {
        int status = 0;
        IntegralChildProcess result = waitpid(state->link.room_client_pid, &status, WNOHANG);
        if (result == state->link.room_client_pid || (result < 0 && errno == ECHILD)) {
            exited = true;
            break;
        }
        SDL_Delay(100);
    }
    if (!exited) {
        errno = 0;
        sent = kill(state->link.room_client_pid, SIGKILL);
        signal_error = sent < 0 ? errno : 0;
        client_room_log(state, "room_client_signal", "pid=%ld signal=KILL result=%d errno=%d",
                        (long)state->link.room_client_pid, sent, signal_error);
        if (sent < 0 && signal_error != ESRCH) return false;
        int status = 0;
        pid_t result;
        do { result = waitpid(state->link.room_client_pid, &status, 0); }
        while (result < 0 && errno == EINTR);
        if (result != state->link.room_client_pid && !(result < 0 && errno == ECHILD)) return false;
    }
#endif
    client_room_log(state, "room_client_reaped", "pid=%ld", (long)state->link.room_client_pid);
    state->link.room_client_started = false;
    state->link.room_client_pid = 0;
    state->link.room_game_ended = true;
    if (state->link.room_gb_runtime_fixed_host_active) {
        state->link.room_gb_runtime_fixed_host_active = false;
        discard_gb_runtime_fixed_host_result(state);
        (void)stop_current_game_session(state);
        copy_text(state->login->status, sizeof(state->login->status),
                  "SESSION ABORTED - SAVE NOT UPDATED");
        return true;
    }
    client_room_log(state, "room_client_stop_unexpected_runtime",
               "session=%s save=not_updated", state->link.room_link_session_id);
    (void)stop_current_game_session(state);
    copy_text(state->login->status, sizeof(state->login->status),
              "SESSION ABORTED - SAVE NOT UPDATED");
    return true;
}


bool stop_current_game_session(IntegralRoomContext *state)
{
    if (state->login->token[0] == '\0' || state->link.room_link_session_id[0] == '\0') {
        return true;
    }
    char error[160];
    if (integral_api_stop_game(state->login->server,
                          state->login->token,
                          state->link.room_game_session_id,
                          state->link.room_fencing_token,
                          error,
                          sizeof(error)) != 0) {
        if (strstr(error, "active game session not found") != NULL ||
            strstr(error, "link session is already ended") != NULL) {
            client_room_log(state, "game_stop_already_done", "session=%s message=%s", state->link.room_link_session_id, error);
            return true;
        }
        client_room_log(state, "game_stop_failed", "session=%s error=%s", state->link.room_link_session_id, error);
        snprintf(state->login->status, sizeof(state->login->status), "GAME STOP API FAILED %s", error);
        return false;
    }
    client_room_log(state, "game_stop_ok", "session=%s", state->link.room_link_session_id);
    return true;
}


static void move_room_selection(IntegralRoomContext *state, int delta)
{
    int selected = (int)state->common.room_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_ROOM_ROWS - 1;
    }
    if (selected >= INTEGRAL_ROOM_ROWS) {
        selected = 0;
    }
    state->common.room_selected = (unsigned)selected;
    if (state->common.room_selected != 4 && state->common.room_chat_editing) {
        state->common.room_chat_editing = false;
        state->common.room_chat_composition[0] = '\0';
        SDL_StopTextInput();
    }
    if (state->common.room_selected == 4) {
        copy_text(state->login->status, sizeof(state->login->status), "ENTER CHAT INPUT");
    }
}


void handle_room_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            if (state->common.room_chat_editing) {
                state->common.room_chat_editing = false;
                state->common.room_chat_composition[0] = '\0';
                SDL_StopTextInput();
                copy_text(state->login->status, sizeof(state->login->status), "CHAT INPUT SAVED");
            }
            else {
                leave_current_room(state, true);
                if (state->link.room_client_pid > 0 || state->common.room_number) return;
                (*state->screen) = SCREEN_MAIN_MENU;
                copy_text(state->login->status, sizeof(state->login->status), "MAIN MENU");
            }
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_room_selection(state, 1);
            break;
        case SDLK_UP:
            move_room_selection(state, -1);
            break;
        case SDLK_RIGHT:
            if (state->common.room_selected == 0) {
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    client_room_log(state, "room_slot_blocked", "reason=game_ended direction=right");
                    break;
                }
                cycle_room_rom_slot(state, 1);
                client_room_log(state, "room_slot_changed", "direction=right slot_index=%d", state->link.room_slot_index);
                sync_room_state(state);
            }
            else if (state->common.room_selected == 1 || state->common.room_selected == 2) {
                if (!current_room_is_user1(state)) {
                    copy_text(state->login->status, sizeof(state->login->status), "MODE SELECTED BY USER1");
                    client_room_log(state, "room_mode_blocked", "reason=user2 direction=right");
                    break;
                }
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    break;
                }
                if (state->link.room_link_session_id[0] != '\0') {
                    copy_text(state->login->status, sizeof(state->login->status), "MODE LOCKED AFTER START");
                    client_room_log(state, "room_mode_blocked", "reason=session_active session=%s", state->link.room_link_session_id);
                    break;
                }
                cycle_room_link_mode(state, 1);
                mark_room_link_mode_local_override(state);
                client_room_log(state, "room_mode_changed", "direction=right mode=%s", room_link_mode_api_name(state->link.room_link_mode));
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->common.room_selected == 3 && state->common.room_chat_scroll > 0) {
                state->common.room_chat_scroll--;
                copy_text(state->login->status, sizeof(state->login->status), "CHAT LOG NEWER");
            }
            break;
        case SDLK_LEFT:
            if (state->common.room_selected == 0) {
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    client_room_log(state, "room_slot_blocked", "reason=game_ended direction=left");
                    break;
                }
                cycle_room_rom_slot(state, -1);
                client_room_log(state, "room_slot_changed", "direction=left slot_index=%d", state->link.room_slot_index);
                sync_room_state(state);
            }
            else if (state->common.room_selected == 1 || state->common.room_selected == 2) {
                if (!current_room_is_user1(state)) {
                    copy_text(state->login->status, sizeof(state->login->status), "MODE SELECTED BY USER1");
                    client_room_log(state, "room_mode_blocked", "reason=user2 direction=left");
                    break;
                }
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    break;
                }
                if (state->link.room_link_session_id[0] != '\0') {
                    copy_text(state->login->status, sizeof(state->login->status), "MODE LOCKED AFTER START");
                    client_room_log(state, "room_mode_blocked", "reason=session_active session=%s", state->link.room_link_session_id);
                    break;
                }
                cycle_room_link_mode(state, -1);
                mark_room_link_mode_local_override(state);
                client_room_log(state, "room_mode_changed", "direction=left mode=%s", room_link_mode_api_name(state->link.room_link_mode));
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->common.room_selected == 3) {
                unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->common.room_chat_log);
                unsigned max_scroll = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
                if (state->common.room_chat_scroll < max_scroll) {
                    state->common.room_chat_scroll++;
                    copy_text(state->login->status, sizeof(state->login->status), "CHAT LOG OLDER");
                }
            }
            break;
        case SDLK_BACKSPACE:
            if (state->common.room_selected == 4 && state->common.room_chat_editing) {
                remove_last_utf8_char(state->common.room_chat_input);
            }
            break;
        case SDLK_F5:
            if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                client_room_log(state, "room_ready_debug_blocked", "reason=game_ended");
                break;
            }
            state->common.room_ready_peer = !state->common.room_ready_peer;
            update_room_ready_status(state);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->common.room_selected == 0) {
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    client_room_log(state, "room_slot_blocked", "reason=game_ended direction=enter");
                    break;
                }
                cycle_room_rom_slot(state, 1);
                client_room_log(state, "room_slot_changed", "direction=enter slot_index=%d", state->link.room_slot_index);
            }
            else if (state->common.room_selected == 1) {
                if (!current_room_is_user1(state)) {
                    copy_text(state->login->status, sizeof(state->login->status), "MODE SELECTED BY USER1");
                    client_room_log(state, "room_mode_blocked", "reason=user2 direction=enter");
                    break;
                }
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    break;
                }
                if (state->link.room_link_session_id[0] != '\0') {
                    copy_text(state->login->status, sizeof(state->login->status), "MODE LOCKED AFTER START");
                    client_room_log(state, "room_mode_blocked", "reason=session_active session=%s", state->link.room_link_session_id);
                    break;
                }
                cycle_room_link_mode(state, 1);
                mark_room_link_mode_local_override(state);
                client_room_log(state, "room_mode_changed", "direction=enter mode=%s", room_link_mode_api_name(state->link.room_link_mode));
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->common.room_selected == 2) {
                if (current_room_game_ended(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "GAME ENDED  ESC MAIN MENU");
                    client_room_log(state, "room_ready_blocked", "reason=game_instance_used");
                    break;
                }
                if (!room_registered_rom_slot_at(state, state->link.room_slot_index)) {
                    copy_text(state->login->status, sizeof(state->login->status), "ROOM SLOT REQUIRED");
                    client_room_log(state, "room_ready_blocked", "reason=slot_required slot_index=%d", state->link.room_slot_index);
                    break;
                }
                const IntegralConfigRomSlot *room_slot = room_registered_rom_slot_at(state, state->link.room_slot_index);
                if (!state->common.room_ready_self &&
                    (!room_slot || !client_room_recover(state, room_slot->save_id))) {
                    client_room_log(state,
                               "room_ready_blocked",
                               "reason=outbox_recovery slot_index=%d",
                               state->link.room_slot_index);
                    break;
                }
                state->common.room_ready_self = !state->common.room_ready_self;
                client_room_log(state,
                           "room_ready_toggle",
                           "ready_self=%d slot_index=%d",
                           state->common.room_ready_self ? 1 : 0,
                           state->link.room_slot_index);
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->common.room_selected == 4) {
                if (state->common.room_chat_editing) {
                    if (state->common.room_chat_input[0] != '\0') {
                        char error[160];
                        if (integral_api_send_room_chat(state->login->server,
                                                         state->login->token,
                                                         state->common.room_number,
                                                         state->common.room_chat_input,
                                                         error,
                                                         sizeof(error)) != 0) {
                            snprintf(state->login->status, sizeof(state->login->status), "CHAT SEND FAILED %s", error);
                            break;
                        }
                        state->common.room_chat_input[0] = '\0';
                        state->common.room_chat_composition[0] = '\0';
                        refresh_room(state);
                        copy_text(state->login->status, sizeof(state->login->status), "CHAT SENT LOCAL");
                    }
                    else {
                        state->common.room_chat_editing = false;
                        state->common.room_chat_composition[0] = '\0';
                        SDL_StopTextInput();
                        copy_text(state->login->status, sizeof(state->login->status), "CHAT INPUT SAVED");
                    }
                }
                else {
                    state->common.room_chat_editing = true;
                    state->common.room_chat_composition[0] = '\0';
                    SDL_Rect input_rect = {.x = 154, .y = 386, .w = INTEGRAL_WINDOW_WIDTH - 202, .h = 30};
                    SDL_SetTextInputRect(&input_rect);
                    SDL_StartTextInput();
                    copy_text(state->login->status, sizeof(state->login->status), "CHAT INPUT ACTIVE");
                }
            }
            break;
        default:
            break;
    }
}

bool current_room_is_ready_to_start(const IntegralRoomContext *state)
{
    if (state->common.room_number < 1 || state->common.room_number > INTEGRAL_API_ROOMS) {
        return false;
    }
    if (current_room_game_ended(state)) {
        return false;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    return room->user1[0] != '\0' && room->user2[0] != '\0' && room->slot1[0] != '\0' && room->slot2[0] != '\0' &&
           room->ready1 && room->ready2;
}


void monitor_room_gb_runtime_client_exit(IntegralRoomContext *state)
{
    if (state->link.room_client_pid <= 0) {
        return;
    }
    int status = 0;
    IntegralChildProcess result = waitpid(state->link.room_client_pid, &status, WNOHANG);
    if (result == 0) {
        service_gb_reconnect_ticket(state);
        return;
    }
    if (result < 0 && errno != ECHILD) {
        client_room_log(state, "gb_runtime_client_wait_failed", "pid=%ld errno=%d", (long)state->link.room_client_pid, errno);
        return;
    }
    bool remote = strcmp(state->link.room_gb_runtime_fixed_host_role, "remote") == 0;
    int exit_code = result < 0 ? -1 : child_process_exit_code(status);
    client_room_log(state, "gb_runtime_fixed_host_runtime_exited",
               "role=%s wait_status=%d session=%s",
               state->link.room_gb_runtime_fixed_host_role, status,
               state->link.room_link_session_id);
    client_room_log(state, "room_client_reaped", "pid=%ld wait_status=%d errno=%d",
                    (long)state->link.room_client_pid, status, result < 0 ? errno : 0);
    state->link.room_client_started = false;
    state->link.room_client_pid = 0;
    if (state->link.room_gb_runtime_fixed_host_active &&
        strcmp(state->link.room_gb_runtime_fixed_host_save_policy, "commit_pair") == 0 &&
        exit_code == 0 &&
        handle_gb_runtime_fixed_host_trade_result(state)) {
        state->link.room_gb_runtime_fixed_host_active = false;
        state->link.room_game_ended = true;
        return;
    }
    discard_gb_runtime_fixed_host_result(state);
    if (remote &&
        exit_code == INTEGRAL_GB_RUNTIME_FIXED_HOST_REMOTE_LEAVE_EXIT_CODE) {
        state->link.room_gb_runtime_fixed_host_active = false;
        state->link.room_game_ended = true;
        (void)stop_current_game_session(state);
        copy_text(state->login->status, sizeof(state->login->status),
                  "SESSION ENDED - SAVE NOT UPDATED");
        return;
    }
    if (exit_code == 0 &&
        strcmp(state->link.room_gb_runtime_fixed_host_save_policy, "discard") == 0) {
        state->link.room_gb_runtime_fixed_host_active = false;
        state->link.room_game_ended = true;
        (void)stop_current_game_session(state);
        copy_text(state->login->status, sizeof(state->login->status),
                  "SESSION ENDED - SAVE NOT UPDATED");
        return;
    }
    state->link.room_gb_runtime_fixed_host_active = false;
    state->link.room_game_ended = true;
    (void)stop_current_game_session(state);
    copy_text(state->login->status, sizeof(state->login->status),
              "SESSION ABORTED - SAVE NOT UPDATED");
}

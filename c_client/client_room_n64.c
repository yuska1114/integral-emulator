/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_room_common.h"
#include "windows_process.h"
#include "../runtimes/gb/src/common/utf8_file.h"
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
#include "../runtimes/n64/src/remote_media_ipc.h"
#include "../runtimes/common/transfer_sav_ipc.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
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
#define INTEGRAL_N64_RUNTIME_MEDIA_DIR "runtime/n64_runtime_media"
#define INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE 28u
#define INTEGRAL_N64_RUNTIME_INPUT_IPC_FAILURE_LIMIT_MS 1000u
#define INTEGRAL_MAX_ROM_BYTES (16u * 1024u * 1024u)


#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif
static bool n64_room_gb_game_type(const IntegralRoomContext *state, int index, char *out, size_t out_size);
static const IntegralConfigRomSlot *find_n64_room_host_slot2_rom(const IntegralRoomContext *state, int *out_index);
static void init_n64_room_selection(IntegralRoomContext *state);
static void set_n64_room_ready_error(IntegralRoomContext *state, const char *error);
static bool prepare_n64_remote_input_path(IntegralRoomContext *state);
static bool write_n64_remote_input_state(IntegralRoomContext *state,
                                         uint32_t sequence,
                                         uint64_t buttons);
static void cycle_n64_room_slot(IntegralRoomContext *state, unsigned row, int delta);
static uint64_t n64_remote_keyboard_buttons(const IntegralRoomContext *state);
static bool n64_remote_controller_scancode(const IntegralRoomContext *state, SDL_Scancode scancode);
static void poll_n64_room_host_process(IntegralRoomContext *state);
static bool start_n64_room_host_n64_runtime(IntegralRoomContext *state);
static bool maybe_start_n64_room_host_n64_runtime(IntegralRoomContext *state, Uint32 now);
static void n64_write_be32(unsigned char *data, uint32_t value);
static void n64_write_be64(unsigned char *data, uint64_t value);
static uint64_t n64_wall_clock_ms(void);
static uint32_t media_rate_x100(uint32_t frames, uint32_t window_ms);
static const char *n64_runtime_media_ca_file(void);
static bool request_n64_runtime_media_session(IntegralRoomContext *state);
static void log_n64_runtime_media_metrics(IntegralRoomContext *state, Uint32 now);
static bool download_n64_runtime_save_to_file(IntegralRoomContext *state,
                                              const char *kind,
                                              const char *path);
static bool n64_room_gb_game_type(const IntegralRoomContext *state, int index, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = room_registered_rom_slot_at(state, index);
    const IntegralApiRomSlot *server_slot =
        index >= 0 && index < INTEGRAL_ROM_SLOTS ? &state->server_rom_slots[index] : NULL;
    IntegralRomMetadata header;
    if (!slot || !server_slot || !server_slot->game_type[0] ||
        strcmp(server_slot->platform, "gb") != 0 ||
        !server_slot->rom_header_title[0] ||
        integral_rom_metadata_read(slot->rom_path, &header) != 0 ||
        strcmp(header.platform, server_slot->platform) != 0 ||
        strcmp(header.header_title, server_slot->rom_header_title) != 0) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return false;
    }
    copy_text(out, out_size, server_slot->game_type);
    return true;
}


static const IntegralConfigRomSlot *find_n64_room_host_slot2_rom(const IntegralRoomContext *state, int *out_index)
{
    if (out_index) *out_index = -1;
    if (!state || state->common.room_number < 65 || state->common.room_number > 128) return NULL;
    const char *remote_game_type = state->common.current_room.slot_game_type2;
    const char *remote_header = state->common.current_room.slot_header_title2;
    if (!remote_game_type[0] || !remote_header[0]) return NULL;
    for (int index = 0; index < INTEGRAL_ROM_SLOTS; index++) {
        char local_game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
        const IntegralConfigRomSlot *slot = room_registered_rom_slot_at(state, index);
        bool local_metadata_valid =
            slot && n64_room_gb_game_type(state, index, local_game_type, sizeof(local_game_type));
        bool game_type_matches =
            local_metadata_valid && strcmp(local_game_type, remote_game_type) == 0;
        bool header_matches =
            local_metadata_valid &&
            strcmp(state->server_rom_slots[index].rom_header_title, remote_header) == 0;
        if (slot && game_type_matches && header_matches) {
            if (out_index) *out_index = index;
            return slot;
        }
    }
    return NULL;
}


static void init_n64_room_selection(IntegralRoomContext *state)
{
    state->n64.n64_room_n64_slot_index = -1;
    state->n64.n64_room_user1_gb_slot_index = -1;
    state->n64.n64_room_user2_gb_slot_index = -1;
    for (int i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (state->n64.n64_room_n64_slot_index < 0 && room_registered_rom_slot_at(state, i) &&
            slot_is_supported_n64(&state->rom_slots[i])) {
            state->n64.n64_room_n64_slot_index = i;
        }
        if (state->n64.n64_room_user1_gb_slot_index < 0 && room_registered_rom_slot_at(state, i) &&
            slot_is_supported_gb(&state->rom_slots[i])) {
            state->n64.n64_room_user1_gb_slot_index = i;
            state->n64.n64_room_user2_gb_slot_index = i;
        }
    }
}


int n64_room_local_user_index(const IntegralRoomContext *state)
{
    if (state->common.room_number < 65 || state->common.room_number > 128 || state->login->username[0] == '\0') {
        return -1;
    }
    const IntegralApiRoom *room = &state->common.current_room;
    if (room->user1[0] && strcasecmp(state->login->username, room->user1) == 0) {
        return 0;
    }
    if (room->user2[0] && strcasecmp(state->login->username, room->user2) == 0) {
        return 1;
    }
    return -1;
}


static void set_n64_room_ready_error(IntegralRoomContext *state, const char *error)
{
    snprintf(state->login->status,
             sizeof(state->login->status),
             "READY FAILED: %s",
             error && error[0] ? error : "ROOM SYNC ERROR");
}


bool sync_n64_room_state(IntegralRoomContext *state, bool ready)
{
    int user_index = n64_room_local_user_index(state);
    if (user_index < 0) {
        refresh_room_quiet(state);
        user_index = n64_room_local_user_index(state);
    }
    int gb_index = user_index == 0 ? state->n64.n64_room_user1_gb_slot_index
                                  : state->n64.n64_room_user2_gb_slot_index;
    if (user_index >= 0 &&
        (!room_registered_rom_slot_at(state, gb_index) ||
         !slot_is_supported_gb(room_registered_rom_slot_at(state, gb_index)))) {
        init_n64_room_selection(state);
        gb_index = user_index == 0 ? state->n64.n64_room_user1_gb_slot_index
                                  : state->n64.n64_room_user2_gb_slot_index;
    }
    if (user_index < 0 || gb_index < 0 || (user_index == 0 && state->n64.n64_room_n64_slot_index < 0)) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 ROOM ROM SELECTION REQUIRED");
        client_room_log(state,
                   "n64_room_sync_blocked",
                   "ready=%d user_index=%d gb_index=%d n64_index=%d",
                   ready ? 1 : 0,
                   user_index,
                   gb_index,
                   state->n64.n64_room_n64_slot_index);
        return false;
    }
    char slot[16];
    char n64_slot[16] = "";
    snprintf(slot, sizeof(slot), "ROM%d", gb_index + 1);
    if (user_index == 0) {
        snprintf(n64_slot, sizeof(n64_slot), "ROM%d", state->n64.n64_room_n64_slot_index + 1);
    }
    char error[160];
    if (integral_api_update_n64_room_state(state->login->server,
                                            state->login->token,
                                            state->common.room_number,
                                            slot,
                                            n64_slot,
                                            ready ? 1 : 0,
                                            error,
                                            sizeof(error)) != 0) {
        set_n64_room_ready_error(state, error);
        client_room_log(state,
                   "n64_room_sync_failed",
                   "ready=%d user_index=%d slot=%s n64_slot=%s error=%s",
                   ready ? 1 : 0,
                   user_index,
                   slot,
                   n64_slot[0] ? n64_slot : "-",
                   error);
        return false;
    }
    refresh_room_quiet(state);
    copy_text(state->login->status,
              sizeof(state->login->status),
              ready ? "N64 ROOM READY" : "N64 ROOM SELECTION UPDATED");
    client_room_log(state,
               "n64_room_sync_ok",
               "ready=%d user_index=%d slot=%s n64_slot=%s ready_self=%d ready_peer=%d",
               ready ? 1 : 0,
               user_index,
               slot,
               n64_slot[0] ? n64_slot : "-",
               state->common.room_ready_self ? 1 : 0,
               state->common.room_ready_peer ? 1 : 0);
    return true;
}


bool resolve_n64_room_local_outbox(IntegralRoomContext *state)
{
    int user_index = n64_room_local_user_index(state);
    int gb_index = user_index == 0 ? state->n64.n64_room_user1_gb_slot_index
                                  : state->n64.n64_room_user2_gb_slot_index;
    const IntegralConfigRomSlot *gb_slot = room_registered_rom_slot_at(state, gb_index);
    if (user_index < 0 || !gb_slot || !client_room_recover(state, gb_slot->save_id)) {
        return false;
    }
    if (user_index == 0) {
        const IntegralConfigRomSlot *n64_slot = room_registered_rom_slot_at(state, state->n64.n64_room_n64_slot_index);
        if (!n64_slot || !client_room_recover(state, n64_slot->save_id)) return false;
    }
    return true;
}


void clear_secret(char *value, size_t value_size)
{
    volatile unsigned char *cursor = (volatile unsigned char *)value;
    while (value_size-- > 0) {
        *cursor++ = 0;
    }
}


static const char *n64_runtime_media_ca_file(void)
{
    const char *configured = getenv("INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CA_FILE");
    return configured && configured[0] ? configured : NULL;
}


static void n64_write_be32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24u);
    data[1] = (unsigned char)(value >> 16u);
    data[2] = (unsigned char)(value >> 8u);
    data[3] = (unsigned char)value;
}


static void n64_write_be64(unsigned char *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) {
        data[index] = (unsigned char)value;
        value >>= 8u;
    }
}


static uint64_t n64_wall_clock_ms(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}


static bool prepare_n64_remote_input_path(IntegralRoomContext *state)
{
    if (!runtime_session_id_is_path_safe(state->n64.n64_runtime_media_session_id) ||
        ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_N64_RUNTIME_MEDIA_DIR) != 0) {
        return false;
    }
    char session_dir[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(session_dir,
                                     sizeof(session_dir),
                                     INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id,
                                     NULL)) {
        return false;
    }
    if (ensure_private_runtime_directory(session_dir) != 0) return false;
    return format_runtime_session_path(state->n64.n64_runtime_media_remote_input_path,
                                       sizeof(state->n64.n64_runtime_media_remote_input_path),
                                       INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                       state->n64.n64_runtime_media_session_id,
                                       "controller2.bin");
}


static bool write_n64_remote_input_state(IntegralRoomContext *state,
                                         uint32_t sequence,
                                         uint64_t buttons)
{
    unsigned char record[INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE] = {0};
    uint64_t written_ms = n64_wall_clock_ms();
    if (!state->n64.n64_runtime_media_remote_input_path[0] || sequence == 0 ||
        written_ms == 0 || (buttons & ~UINT64_C(0x3ffff)) != 0) {
        return false;
    }
    memcpy(record, "S64C", 4);
    record[4] = 1;
    n64_write_be32(record + 8, sequence);
    n64_write_be64(record + 12, written_ms);
    n64_write_be64(record + 20, buttons);
    return atomic_replace_binary_file(state->n64.n64_runtime_media_remote_input_path,
                                      record,
                                      sizeof(record)) == 0;
}


N64RuntimeStopResult stop_n64_runtime_media_host_process(IntegralRoomContext *state)
{
    N64RuntimeStopResult result = {false, false, false, false};
    if (!state || state->n64.n64_runtime_media_host_pid == 0) return result;
    IntegralChildProcess pid = state->n64.n64_runtime_media_host_pid;
    static const unsigned char stop_record[] = "S64STOP1\n";
    if (state->n64.n64_runtime_stop_request_path[0] != '\0') {
        if (atomic_replace_binary_file(state->n64.n64_runtime_stop_request_path,
                                       stop_record,
                                       sizeof(stop_record) - 1u) == 0) {
            result.request_created = true;
            client_room_log(state,
                       "n64_room_host_stop_requested",
                       "session=%s pid=%ld timeout_ms=%u",
                       state->n64.n64_runtime_media_launched_session_id,
                       (long)pid,
                       INTEGRAL_N64_RUNTIME_STOP_WAIT_MS);
        }
        else {
            client_room_log(state,
                       "n64_room_host_stop_request_failed",
                       "session=%s pid=%ld errno=%d",
                       state->n64.n64_runtime_media_launched_session_id,
                       (long)pid,
                       errno);
        }
    }
#ifdef _WIN32
    HANDLE process = (HANDLE)(intptr_t)pid;
    if (process && process != INVALID_HANDLE_VALUE) {
        DWORD wait_result = WaitForSingleObject(process,
                                                INTEGRAL_N64_RUNTIME_STOP_WAIT_MS);
        result.graceful = wait_result == WAIT_OBJECT_0;
        result.stopped = result.graceful;
        if (!result.graceful) {
            if (TerminateProcess(process, 1) != 0) {
                result.forced = true;
                result.stopped = WaitForSingleObject(process, 2000u) == WAIT_OBJECT_0;
            }
        }
        if (result.stopped) CloseHandle(process);
    }
#else
    for (unsigned attempt = 0;
         attempt < INTEGRAL_N64_RUNTIME_STOP_WAIT_MS / 100u;
         attempt++) {
        int status = 0;
        IntegralChildProcess waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            result.graceful = true;
            result.stopped = true;
            break;
        }
        usleep(100000);
    }
    if (!result.graceful && kill(pid, 0) == 0) {
        if (kill(pid, SIGKILL) == 0 && waitpid(pid, NULL, 0) == pid) {
            result.forced = true;
            result.stopped = true;
        }
    }
#endif
    client_room_log(state,
               "n64_room_host_stopped",
               "session=%s pid=%ld graceful=%d forced=%d stopped=%d",
               state->n64.n64_runtime_media_launched_session_id,
               (long)pid,
               result.graceful ? 1 : 0,
               result.forced ? 1 : 0,
               result.stopped ? 1 : 0);
    if (!result.stopped) {
        return result;
    }
    if (state->n64.n64_runtime_stop_request_path[0] != '\0') {
        (void)remove(state->n64.n64_runtime_stop_request_path);
    }
    state->n64.n64_runtime_media_host_pid = 0;
    state->n64.n64_runtime_media_launched_session_id[0] = '\0';
    state->n64.n64_runtime_stop_request_path[0] = '\0';
    return result;
}


void cleanup_n64_room_session_files(const char *session_id)
{
    if (!runtime_session_id_is_path_safe(session_id)) {
        return;
    }
    static const char *relative_paths[] = {
        "transfer/slot1.gbc",
        "transfer/slot2.gbc",
        "transfer/slot1.sav",
        "transfer/slot1.sav.rtc",
        "transfer/slot2.sav",
        "transfer/slot2.sav.rtc",
        "n64-save/n64.sav",
        "controller2.bin",
        "remote-media.ipc",
        "stop.request",
    };
    char path[INTEGRAL_CONFIG_PATH_MAX];
    for (size_t i = 0; i < sizeof(relative_paths) / sizeof(relative_paths[0]); ++i) {
        if (format_runtime_session_path(path,
                                        sizeof(path),
                                        INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                        session_id,
                                        relative_paths[i])) {
            (void)remove(path);
        }
    }
    static const char *relative_directories[] = {
        "transfer",
        "n64-save",
        "config",
        "screenshots",
    };
    for (size_t i = 0; i < sizeof(relative_directories) / sizeof(relative_directories[0]); ++i) {
        if (format_runtime_session_path(path,
                                        sizeof(path),
                                        INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                        session_id,
                                        relative_directories[i])) {
            (void)rmdir(path);
        }
    }
    if (format_runtime_session_path(path,
                                    sizeof(path),
                                    INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                    session_id,
                                    NULL)) {
        (void)rmdir(path);
    }
}


void suspend_n64_runtime_media_connection(IntegralRoomContext *state)
{
    if (state->n64.n64_runtime_media_session_id[0]) {
        copy_text(state->n64.lifecycle_session_id, sizeof(state->n64.lifecycle_session_id),
                  state->n64.n64_runtime_media_session_id);
        state->n64.lifecycle_room_number = state->common.room_number;
    }
    if (state->n64.n64_runtime_media_connection && state->n64.n64_runtime_media_paired &&
        strcmp(state->n64.n64_runtime_media_role, "remote") == 0) {
        char ignored[32];
        (void)integral_media_relay_send_controller(state->n64.n64_runtime_media_connection,
                                              ++state->n64.n64_runtime_media_input_sequence,
                                              0,
                                              ignored,
                                              sizeof(ignored));
    }
    if (state->n64.n64_runtime_media_remote_input_path[0]) {
        (void)write_n64_remote_input_state(state,
                                           state->n64.n64_runtime_media_input_sequence + 1u,
                                           0);
    }
    if (state->n64.n64_runtime_media_connection) {
        integral_media_relay_close(state->n64.n64_runtime_media_connection);
        state->n64.n64_runtime_media_connection = NULL;
    }
    integral_n64_runtime_media_stream_reset(state->n64.n64_runtime_media_stream);
    (void)stop_n64_runtime_media_host_process(state);
    state->n64.n64_runtime_media_authenticated = false;
    state->n64.n64_runtime_media_paired = false;
    state->n64.terminal_pending = true;
}

void reset_n64_runtime_media_connection(IntegralRoomContext *state)
{
    suspend_n64_runtime_media_connection(state);
    if (state->n64.n64_runtime_media_host_pid != 0) {
        client_room_log(state,
                   "n64_room_cleanup_deferred",
                   "session=%s reason=process_not_stopped",
                   state->n64.n64_runtime_media_session_id);
        return;
    }
    cleanup_n64_room_session_files(state->n64.n64_runtime_media_session_id);
    clear_secret(state->n64.n64_runtime_media_ticket, sizeof(state->n64.n64_runtime_media_ticket));
    state->n64.n64_runtime_media_session_id[0] = '\0';
    state->n64.lifecycle_session_id[0] = '\0';
    state->n64.lifecycle_room_number = 0;
    state->n64.lifecycle_room_code[0] = '\0';
    state->n64.n64_runtime_media_relay_host[0] = '\0';
    state->n64.n64_runtime_media_relay_port = 0;
    state->n64.n64_runtime_media_relay_transport[0] = '\0';
    state->n64.n64_runtime_media_role[0] = '\0';
    state->n64.n64_runtime_media_scope[0] = '\0';
    state->n64.n64_runtime_media_authenticated = false;
    state->n64.n64_runtime_media_paired = false;
    state->n64.host_finish_pending = false;
    state->n64.terminal_pending = false;
    state->n64.preflight_failed = false;
    state->n64.n64_runtime_media_retry_after_ticks = 0;
    state->n64.n64_runtime_media_reconnect_deadline = 0;
    state->n64.n64_runtime_media_remote_buttons = 0;
    state->n64.n64_runtime_media_remote_input_path[0] = '\0';
    state->n64.n64_runtime_media_ipc_path[0] = '\0';
    state->n64.n64_runtime_stop_request_path[0] = '\0';
    state->n64.n64_runtime_media_last_sent_buttons = 0;
    state->n64.n64_runtime_media_input_sequence = 0;
    state->n64.n64_runtime_media_last_input_send_ticks = 0;
    state->n64.n64_runtime_media_last_input_receive_ticks = 0;
    state->n64.n64_runtime_media_input_ipc_failure_since_ticks = 0;
    state->n64.n64_runtime_media_input_ipc_failures = 0;
    state->n64.n64_runtime_media_input_interval_total_ms = 0;
    state->n64.n64_runtime_media_input_interval_samples = 0;
    state->n64.n64_runtime_media_input_interval_max_ms = 0;
    state->n64.n64_runtime_media_host_launch_retry_after_ticks = 0;
}


static bool request_n64_runtime_media_session(IntegralRoomContext *state)
{
    if (state->n64.host_finish_pending || state->n64.terminal_pending || state->n64.preflight_failed) return true;
    if (state->n64.n64_runtime_media_connection) {
        return true;
    }
    char error[160];
    char previous_session[sizeof(state->n64.n64_runtime_media_session_id)];
    copy_text(previous_session, sizeof(previous_session), state->n64.n64_runtime_media_session_id);
    if (previous_session[0]) {
        IntegralApiHeartbeatStatus lifecycle;
        if (integral_api_n64_media_state(state->login->server, state->login->token,
                previous_session, 0, &lifecycle, error, sizeof(error)) == 0) {
            handle_room_heartbeat_result(state, &lifecycle, true, NULL);
            if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
        }
    }
    if (integral_api_start_n64_room(state->login->server,
                                     state->login->token,
                                     state->common.room_number,
                                     state->n64.n64_runtime_media_reconnect_deadline ? previous_session : NULL,
                                     state->n64.n64_runtime_media_session_id,
                                     sizeof(state->n64.n64_runtime_media_session_id),
                                     state->n64.n64_runtime_media_relay_host,
                                     sizeof(state->n64.n64_runtime_media_relay_host),
                                     &state->n64.n64_runtime_media_relay_port,
                                     state->n64.n64_runtime_media_relay_transport,
                                     sizeof(state->n64.n64_runtime_media_relay_transport),
                                     state->n64.n64_runtime_media_role,
                                     sizeof(state->n64.n64_runtime_media_role),
                                     state->n64.n64_runtime_media_scope,
                                     sizeof(state->n64.n64_runtime_media_scope),
                                     state->n64.n64_runtime_media_ticket,
                                     sizeof(state->n64.n64_runtime_media_ticket),
                                     error,
                                     sizeof(error)) != 0) {
        copy_text(state->n64.n64_runtime_media_session_id,
                  sizeof(state->n64.n64_runtime_media_session_id), previous_session);
        if (previous_session[0]) {
            IntegralApiHeartbeatStatus lifecycle;
            char state_error[160];
            if (integral_api_n64_media_state(state->login->server, state->login->token,
                    previous_session, 0, &lifecycle, state_error, sizeof(state_error)) == 0) {
                handle_room_heartbeat_result(state, &lifecycle, true, NULL);
                if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
            }
        }
        snprintf(state->login->status, sizeof(state->login->status), "N64 MEDIA AUTH FAILED %s", error);
        state->n64.n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (state->n64.n64_runtime_media_reconnect_deadline &&
        strcmp(previous_session, state->n64.n64_runtime_media_session_id) != 0) {
        copy_text(state->n64.n64_runtime_media_session_id,
                  sizeof(state->n64.n64_runtime_media_session_id), previous_session);
        suspend_n64_runtime_media_connection(state);
        copy_text(state->login->status, sizeof(state->login->status), "N64 SESSION CHANGED - ESC EXIT");
        return false;
    }
    copy_text(state->n64.lifecycle_session_id, sizeof(state->n64.lifecycle_session_id),
              state->n64.n64_runtime_media_session_id);
    state->n64.lifecycle_room_number = state->common.room_number;
    copy_text(state->n64.lifecycle_room_code, sizeof(state->n64.lifecycle_room_code),
              state->common.current_room.room_code);
    if (!runtime_session_id_is_path_safe(state->n64.n64_runtime_media_session_id)) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 MEDIA SESSION ID INVALID");
        clear_secret(state->n64.n64_runtime_media_ticket, sizeof(state->n64.n64_runtime_media_ticket));
        if (!state->n64.n64_runtime_media_reconnect_deadline)
            state->n64.n64_runtime_media_session_id[0] = '\0';
        state->n64.n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (strcmp(state->n64.n64_runtime_media_scope, "n64_runtime_media") != 0) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 MEDIA SCOPE INVALID");
        clear_secret(state->n64.n64_runtime_media_ticket, sizeof(state->n64.n64_runtime_media_ticket));
        state->n64.n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (!state->n64.n64_runtime_media_reconnect_deadline &&
        strcmp(state->n64.n64_runtime_media_role, "host") == 0 &&
        (!prepare_n64_remote_input_path(state) ||
         !write_n64_remote_input_state(state, 1u, 0))) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 INPUT IPC CREATE FAILED");
        clear_secret(state->n64.n64_runtime_media_ticket, sizeof(state->n64.n64_runtime_media_ticket));
        state->n64.n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (integral_media_relay_connect_timeout(state->n64.n64_runtime_media_relay_host,
                                state->n64.n64_runtime_media_relay_port,
                                state->n64.n64_runtime_media_relay_transport,
                                state->n64.n64_runtime_media_session_id,
                                state->n64.n64_runtime_media_role,
                                state->n64.n64_runtime_media_scope,
                                state->n64.n64_runtime_media_ticket,
                                n64_runtime_media_ca_file(),
                                &state->n64.n64_runtime_media_connection,
                                error,
                                sizeof(error), state->n64.n64_runtime_media_reconnect_deadline ? 2 : 10) != 0) {
        if (state->n64.n64_runtime_media_remote_input_path[0]) {
            (void)write_n64_remote_input_state(state,
                ++state->n64.n64_runtime_media_input_sequence, 0);
            if (!state->n64.n64_runtime_media_reconnect_deadline)
                state->n64.n64_runtime_media_remote_input_path[0] = '\0';
        }
        clear_secret(state->n64.n64_runtime_media_ticket, sizeof(state->n64.n64_runtime_media_ticket));
        if (!state->n64.n64_runtime_media_reconnect_deadline)
            state->n64.n64_runtime_media_session_id[0] = '\0';
        state->n64.n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        snprintf(state->login->status, sizeof(state->login->status), "N64 MEDIA RELAY FAILED %s", error);
        return false;
    }
    clear_secret(state->n64.n64_runtime_media_ticket, sizeof(state->n64.n64_runtime_media_ticket));
    state->n64.n64_runtime_media_authenticated = true;
    state->n64.n64_runtime_media_paired = false;
    state->n64.n64_runtime_media_retry_after_ticks = 0;
    client_room_log(state,
               "n64_runtime_media_authenticated",
               "session=%s role=%s transport=%s",
               state->n64.n64_runtime_media_session_id,
               state->n64.n64_runtime_media_role,
               state->n64.n64_runtime_media_relay_transport);
    if (strcmp(state->n64.n64_runtime_media_relay_transport, "plain") == 0) {
        copy_text(state->login->status, sizeof(state->login->status),
                  "WARNING PLAIN LOCAL NETWORK  WAITING PEER");
    }
    else {
        snprintf(state->login->status,
                 sizeof(state->login->status),
                 "MEDIA TLS AUTH %s  WAITING PEER  NO SAV OVERWRITE",
                 state->n64.n64_runtime_media_role);
    }
    return true;
}


static uint32_t media_rate_x100(uint32_t frames, uint32_t window_ms)
{
    if (!window_ms) return 0;
    uint64_t rate = (uint64_t)frames * 100000u / window_ms;
    return rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
}


static void log_n64_runtime_media_metrics(IntegralRoomContext *state, Uint32 now)
{
    IntegralN64RuntimeMediaMetrics metrics;
    if (!integral_n64_runtime_media_stream_take_metrics(state->n64.n64_runtime_media_stream,
                                                  (uint64_t)now * 1000u,
                                                  &metrics)) return;
    if (strcmp(state->n64.n64_runtime_media_role, "host") == 0) {
        uint32_t capture_rate = media_rate_x100(metrics.capture_frames, metrics.window_ms);
        uint32_t encode_rate = media_rate_x100(metrics.encoded_frames, metrics.window_ms);
        uint32_t core_callback_rate = media_rate_x100(
            metrics.source_core_callbacks, metrics.window_ms);
        uint32_t source_due_rate = media_rate_x100(
            metrics.source_capture_due, metrics.window_ms);
        uint32_t source_success_rate = media_rate_x100(
            metrics.source_capture_success, metrics.window_ms);
        uint32_t audio_source_rate = media_rate_x100(
            metrics.audio_source_frames, metrics.window_ms);
        uint32_t audio_sent_rate = media_rate_x100(
            metrics.audio_sent_frames, metrics.window_ms);
        uint32_t input_age_ms = state->n64.n64_runtime_media_last_input_receive_ticks
                                    ? now - state->n64.n64_runtime_media_last_input_receive_ticks
                                    : 0;
        if (metrics.capture_frames > 0u) state->n64.n64_runtime_media_saw_positive_video = true;
        client_room_log(state,
                   "n64_runtime_media_metrics",
                   "role=host window_ms=%u capture_fps=%u.%02u capture_age_avg_ms=%u.%03u "
                   "capture_age_max_ms=%u.%03u encode_fps=%u.%02u encode_avg_ms=%u.%03u "
                   "encode_max_ms=%u.%03u relay_pending_ms=%u relay_pending_max_ms=%u "
                   "core_callback_fps=%u.%02u source_capture_due_fps=%u.%02u "
                   "source_capture_success_fps=%u.%02u source_capture_failures=%u "
                   "source_callback_interval_avg_ms=%u.%03u "
                   "source_callback_interval_max_session_ms=%u.%03u "
                   "source_readback_avg_ms=%u.%03u source_readback_max_session_ms=%u.%03u "
                   "source_ipc_write_avg_ms=%u.%03u source_ipc_write_max_session_ms=%u.%03u "
                   "audio_source_fps=%u.%02u audio_sent_fps=%u.%02u "
                   "controller_input_age_ms=%u room_poll_ms=%u room_poll_max_ms=%u",
                   metrics.window_ms,
                   capture_rate / 100u,
                   capture_rate % 100u,
                   metrics.capture_age_avg_us / 1000u,
                   metrics.capture_age_avg_us % 1000u,
                   metrics.capture_age_max_us / 1000u,
                   metrics.capture_age_max_us % 1000u,
                   encode_rate / 100u,
                   encode_rate % 100u,
                   metrics.encode_avg_us / 1000u,
                   metrics.encode_avg_us % 1000u,
                   metrics.encode_max_us / 1000u,
                   metrics.encode_max_us % 1000u,
                   metrics.relay_pending_ms,
                   metrics.relay_pending_max_ms,
                   core_callback_rate / 100u,
                   core_callback_rate % 100u,
                   source_due_rate / 100u,
                   source_due_rate % 100u,
                   source_success_rate / 100u,
                   source_success_rate % 100u,
                   metrics.source_capture_failures,
                   metrics.source_callback_interval_avg_us / 1000u,
                   metrics.source_callback_interval_avg_us % 1000u,
                   metrics.source_callback_interval_max_us / 1000u,
                   metrics.source_callback_interval_max_us % 1000u,
                   metrics.source_readback_avg_us / 1000u,
                   metrics.source_readback_avg_us % 1000u,
                   metrics.source_readback_max_us / 1000u,
                   metrics.source_readback_max_us % 1000u,
                   metrics.source_ipc_write_avg_us / 1000u,
                   metrics.source_ipc_write_avg_us % 1000u,
                   metrics.source_ipc_write_max_us / 1000u,
                   metrics.source_ipc_write_max_us % 1000u,
                   audio_source_rate / 100u,
                   audio_source_rate % 100u,
                   audio_sent_rate / 100u,
                   audio_sent_rate % 100u,
                   input_age_ms,
                   state->common.room_poll_last_ms,
                   state->common.room_poll_max_ms);
    }
    else {
        uint32_t receive_rate = media_rate_x100(metrics.received_frames, metrics.window_ms);
        uint32_t decode_rate = media_rate_x100(metrics.decoded_frames, metrics.window_ms);
        uint32_t present_rate = media_rate_x100(metrics.presented_frames, metrics.window_ms);
        uint32_t audio_receive_rate = media_rate_x100(
            metrics.audio_received_frames, metrics.window_ms);
        uint32_t input_interval_avg_ms = state->n64.n64_runtime_media_input_interval_samples
                                             ? (uint32_t)(state->n64.n64_runtime_media_input_interval_total_ms /
                                                          state->n64.n64_runtime_media_input_interval_samples)
                                             : 0;
        if (metrics.received_frames > 0u) state->n64.n64_runtime_media_saw_positive_video = true;
        client_room_log(state,
                   "n64_runtime_media_metrics",
                   "role=remote window_ms=%u receive_fps=%u.%02u receive_age_avg_ms=%u.%03u "
                   "receive_age_max_ms=%u.%03u receive_age_invalid=%u decode_fps=%u.%02u "
                   "decode_avg_ms=%u.%03u decode_max_ms=%u.%03u decode_queue_peak=%u "
                   "display_overwrites=%u present_fps=%u.%02u "
                   "present_p50_ms=%u.%03u present_p95_ms=%u.%03u present_max_ms=%u.%03u "
                   "present_call_avg_ms=%u.%03u present_call_max_ms=%u.%03u "
                   "video_refresh_hz=%u "
                   "audio_jitter=%u audio_jitter_peak=%u audio_conceals=%u "
                   "audio_receive_fps=%u.%02u "
                   "audio_queue_ms=%u audio_queue_max_ms=%u audio_queue_clears=%u "
                   "controller_send_avg_ms=%u controller_send_max_ms=%u "
                   "room_poll_ms=%u room_poll_max_ms=%u video_vsync=%u video_renderer=%s",
                   metrics.window_ms,
                   receive_rate / 100u,
                   receive_rate % 100u,
                   metrics.receive_age_avg_us / 1000u,
                   metrics.receive_age_avg_us % 1000u,
                   metrics.receive_age_max_us / 1000u,
                   metrics.receive_age_max_us % 1000u,
                   metrics.receive_age_invalid,
                   decode_rate / 100u,
                   decode_rate % 100u,
                   metrics.decode_avg_us / 1000u,
                   metrics.decode_avg_us % 1000u,
                   metrics.decode_max_us / 1000u,
                   metrics.decode_max_us % 1000u,
                   metrics.decode_queue_peak,
                   metrics.display_overwrites,
                   present_rate / 100u,
                   present_rate % 100u,
                   metrics.present_p50_us / 1000u,
                   metrics.present_p50_us % 1000u,
                   metrics.present_p95_us / 1000u,
                   metrics.present_p95_us % 1000u,
                   metrics.present_max_us / 1000u,
                   metrics.present_max_us % 1000u,
                   metrics.present_call_avg_us / 1000u,
                   metrics.present_call_avg_us % 1000u,
                   metrics.present_call_max_us / 1000u,
                   metrics.present_call_max_us % 1000u,
                   metrics.video_refresh_hz,
                   metrics.audio_jitter_packets,
                   metrics.audio_jitter_peak_packets,
                   metrics.audio_conceals,
                   audio_receive_rate / 100u,
                   audio_receive_rate % 100u,
                   metrics.audio_queue_ms,
                   metrics.audio_queue_max_ms,
                   metrics.audio_queue_clears,
                   input_interval_avg_ms,
                   state->n64.n64_runtime_media_input_interval_max_ms,
                   state->common.room_poll_last_ms,
                   state->common.room_poll_max_ms,
                   integral_n64_runtime_media_stream_is_video_vsync_paced(
                       state->n64.n64_runtime_media_stream) ? 1u : 0u,
                   integral_n64_runtime_media_stream_video_renderer_driver(
                       state->n64.n64_runtime_media_stream));
    }
    state->n64.n64_runtime_media_input_interval_total_ms = 0;
    state->n64.n64_runtime_media_input_interval_samples = 0;
    state->n64.n64_runtime_media_input_interval_max_ms = 0;
    state->common.room_poll_max_ms = 0;
}


static void reconnect_n64_transport(IntegralRoomContext *state, Uint32 now)
{
    IntegralApiHeartbeatStatus lifecycle;
    char error[160];
    if (integral_api_n64_media_state(state->login->server, state->login->token,
            state->n64.n64_runtime_media_session_id, 1, &lifecycle, error, sizeof(error)) == 0) {
        handle_room_heartbeat_result(state, &lifecycle, true, NULL);
        if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return;
    }
    if (!state->n64.n64_runtime_media_reconnect_deadline)
        state->n64.n64_runtime_media_reconnect_deadline = 1u;
    integral_media_relay_close(state->n64.n64_runtime_media_connection);
    state->n64.n64_runtime_media_connection = NULL;
    state->n64.n64_runtime_media_paired = false;
    state->n64.n64_runtime_media_authenticated = false;
    state->n64.n64_runtime_media_retry_after_ticks = now + 1000u;
    integral_n64_runtime_media_stream_transport_lost(state->n64.n64_runtime_media_stream);
    state->n64.n64_runtime_media_remote_buttons = 0;
    state->n64.n64_runtime_media_last_sent_buttons = UINT64_MAX;
    if (state->n64.n64_runtime_media_remote_input_path[0])
        (void)write_n64_remote_input_state(state, ++state->n64.n64_runtime_media_input_sequence, 0);
    client_room_log(state, "n64_media_reconnecting", "session=%s runtime_pid=%ld",
                    state->n64.n64_runtime_media_session_id, (long)state->n64.n64_runtime_media_host_pid);
}

bool poll_n64_runtime_media_transport(IntegralRoomContext *state, Uint32 now)
{
    if (state->n64.preflight_failed) return true;
    char error[160];
    if (state->n64.n64_runtime_media_host_pid) {
        int captured = integral_n64_runtime_remote_media_screenshot_result();
        if (captured)
            copy_text(state->login->status, sizeof(state->login->status),
                      captured > 0 ? "SCREENSHOT SAVED" : "SCREENSHOT FAILED");
    }
    poll_n64_room_host_process(state);
    if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
    if (state->n64.host_finish_pending) return true;
    if (state->n64.terminal_pending) {
        /* Existing background ROOM/heartbeat polling delivers terminal state.
         * Do not block the UI with a second synchronous request while offline. */
        return true;
    }
    if ((!state->common.room_ready_self || !state->common.room_ready_peer) && state->n64.n64_runtime_media_connection) {
        suspend_n64_runtime_media_connection(state);
        copy_text(state->login->status, sizeof(state->login->status), "N64 MEDIA PEER NOT READY");
        return true;
    }
    if (!state->n64.n64_runtime_media_connection) {
        if (state->common.room_ready_self && state->common.room_ready_peer &&
            (state->n64.n64_runtime_media_retry_after_ticks == 0 ||
             (Sint32)(now - state->n64.n64_runtime_media_retry_after_ticks) >= 0)) {
            return request_n64_runtime_media_session(state);
        }
        return true;
    }
    if (!state->n64.n64_runtime_media_paired) {
        int media_status = integral_media_relay_poll(state->n64.n64_runtime_media_connection, error, sizeof(error));
        if (media_status > 0) {
            state->n64.n64_runtime_media_reconnect_deadline = 0;
            state->n64.n64_runtime_media_paired = true;
            state->n64.n64_runtime_media_last_input_receive_ticks = now;
            client_room_log(state,
                       "n64_runtime_media_paired",
                       "session=%s role=%s",
                       state->n64.n64_runtime_media_session_id,
                       state->n64.n64_runtime_media_role);
            snprintf(state->login->status,
                     sizeof(state->login->status),
                     "MEDIA TLS PAIRED %s  NO SAV OVERWRITE",
                     state->n64.n64_runtime_media_role);
            if (strcmp(state->n64.n64_runtime_media_role, "host") == 0) {
                (void)maybe_start_n64_room_host_n64_runtime(state, now);
            }
            return true;
        }
        if (media_status == 0) return true;
        client_room_log(state,
                   "n64_runtime_media_transport_error",
                   "stage=pair role=%s error=%s",
                   state->n64.n64_runtime_media_role,
                   error);
        reconnect_n64_transport(state, now);
        if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
        state->n64.n64_runtime_media_retry_after_ticks = now + 5000u;
        snprintf(state->login->status, sizeof(state->login->status), "N64 MEDIA LOST %s", error);
        return false;
    }
    if (strcmp(state->n64.n64_runtime_media_role, "remote") == 0) {
        uint64_t buttons = state->n64.runtime_exit_confirming
                               ? 0
                               : n64_remote_keyboard_buttons(state);
        if (buttons != state->n64.n64_runtime_media_last_sent_buttons ||
            now - state->n64.n64_runtime_media_last_input_send_ticks >= 50u) {
            int sent = integral_media_relay_send_controller(state->n64.n64_runtime_media_connection,
                                                       ++state->n64.n64_runtime_media_input_sequence,
                                                       buttons,
                                                       error,
                                                       sizeof(error));
            if (sent < 0) {
                client_room_log(state,
                           "n64_runtime_media_transport_error",
                           "stage=remote-input role=remote error=%s",
                           error);
                reconnect_n64_transport(state, now);
                if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
                state->n64.n64_runtime_media_retry_after_ticks = now + 5000u;
                snprintf(state->login->status, sizeof(state->login->status), "N64 INPUT LOST %s", error);
                return false;
            }
            if (sent > 0) {
                if (state->n64.n64_runtime_media_last_input_send_ticks) {
                    uint32_t interval_ms = now - state->n64.n64_runtime_media_last_input_send_ticks;
                    state->n64.n64_runtime_media_input_interval_total_ms += interval_ms;
                    state->n64.n64_runtime_media_input_interval_samples++;
                    if (interval_ms > state->n64.n64_runtime_media_input_interval_max_ms) {
                        state->n64.n64_runtime_media_input_interval_max_ms = interval_ms;
                    }
                }
                state->n64.n64_runtime_media_last_sent_buttons = buttons;
                state->n64.n64_runtime_media_last_input_send_ticks = now;
            }
        }
        int media_result = integral_n64_runtime_media_stream_pump_remote(
            state->n64.n64_runtime_media_stream,
            state->n64.n64_runtime_media_connection,
            (uint64_t)now * 1000u,
            error,
            sizeof(error));
        Uint32 received_at = (Uint32)(integral_n64_runtime_media_stream_last_receive_us(
            state->n64.n64_runtime_media_stream) / 1000u);
        if (received_at && (Sint32)(received_at - state->n64.n64_runtime_media_last_input_receive_ticks) > 0)
            state->n64.n64_runtime_media_last_input_receive_ticks = received_at;
        if (media_result >= 0 && now - state->n64.n64_runtime_media_last_input_receive_ticks >= 5000u) {
            client_room_log(state, "n64_media_stalled", "role=remote session=%s", state->n64.n64_runtime_media_session_id);
            reconnect_n64_transport(state, now);
            return true;
        }
        if (media_result < 0) {
            client_room_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=remote-decode role=remote error=%s",
                       error);
            reconnect_n64_transport(state, now);
            if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
            state->n64.n64_runtime_media_retry_after_ticks = now + 5000u;
            snprintf(state->login->status, sizeof(state->login->status),
                     "N64 MEDIA DECODE LOST %s", error);
            return false;
        }
        log_n64_runtime_media_metrics(state, now);
        return true;
    }
    (void)maybe_start_n64_room_host_n64_runtime(state, now);
    if (state->n64.preflight_failed) return true;
    for (unsigned count = 0; count < 8; count++) {
        uint32_t sequence = 0;
        uint64_t buttons = 0;
        int received = integral_media_relay_poll_controller(state->n64.n64_runtime_media_connection,
                                                       &sequence,
                                                       &buttons,
                                                       error,
                                                       sizeof(error));
        if (received < 0) {
            client_room_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=host-input role=host error=%s",
                       error);
            reconnect_n64_transport(state, now);
            if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
            state->n64.n64_runtime_media_retry_after_ticks = now + 5000u;
            snprintf(state->login->status, sizeof(state->login->status), "N64 INPUT LOST %s", error);
            return false;
        }
        if (received == 2) {
            client_room_log(state,
                       "n64_controller_frame_dropped",
                       "role=host error=%s",
                       error);
            continue;
        }
        if (received == 0) break;
        if (!write_n64_remote_input_state(state, sequence, buttons)) {
#ifdef _WIN32
            unsigned long os_error = (unsigned long)GetLastError();
#else
            unsigned long os_error = (unsigned long)errno;
#endif
            if (state->n64.n64_runtime_media_input_ipc_failure_since_ticks == 0) {
                state->n64.n64_runtime_media_input_ipc_failure_since_ticks = now ? now : 1u;
                state->n64.n64_runtime_media_input_ipc_failures = 1u;
                client_room_log(state,
                           "n64_input_ipc_retry",
                           "role=host os_error=%lu limit_ms=%u",
                           os_error,
                           INTEGRAL_N64_RUNTIME_INPUT_IPC_FAILURE_LIMIT_MS);
            }
            else {
                state->n64.n64_runtime_media_input_ipc_failures++;
            }
            Uint32 failure_ms = now - state->n64.n64_runtime_media_input_ipc_failure_since_ticks;
            if (failure_ms < INTEGRAL_N64_RUNTIME_INPUT_IPC_FAILURE_LIMIT_MS) {
                copy_text(state->login->status,
                          sizeof(state->login->status),
                          "N64 INPUT IPC RETRYING");
                break;
            }
            client_room_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=host-input-ipc role=host error=write-failed "
                       "os_error=%lu failures=%u duration_ms=%u",
                       os_error,
                       state->n64.n64_runtime_media_input_ipc_failures,
                       failure_ms);
            reset_n64_runtime_media_connection(state);
            state->n64.n64_runtime_media_retry_after_ticks = now + 5000u;
            copy_text(state->login->status, sizeof(state->login->status), "N64 INPUT IPC FAILED");
            return false;
        }
        if (state->n64.n64_runtime_media_input_ipc_failure_since_ticks != 0) {
            client_room_log(state,
                       "n64_input_ipc_recovered",
                       "role=host failures=%u duration_ms=%u",
                       state->n64.n64_runtime_media_input_ipc_failures,
                       now - state->n64.n64_runtime_media_input_ipc_failure_since_ticks);
            state->n64.n64_runtime_media_input_ipc_failure_since_ticks = 0;
            state->n64.n64_runtime_media_input_ipc_failures = 0;
        }
        uint64_t previous_buttons = state->n64.n64_runtime_media_remote_buttons;
        state->n64.n64_runtime_media_remote_buttons = buttons;
        state->n64.n64_runtime_media_last_input_receive_ticks = now;
        if (buttons != previous_buttons) {
            client_room_log(state,
                       "n64_remote_input",
                       "session=%s sequence=%u buttons=0x%04llx",
                       state->n64.n64_runtime_media_session_id,
                       sequence,
                       (unsigned long long)buttons);
        }
    }
    if (state->n64.n64_runtime_media_remote_buttons != 0 &&
        now - state->n64.n64_runtime_media_last_input_receive_ticks >= 500u) {
        state->n64.n64_runtime_media_remote_buttons = 0;
        (void)write_n64_remote_input_state(state,
                                           state->n64.n64_runtime_media_input_sequence + 1u,
                                           0);
        client_room_log(state, "n64_remote_input_neutral", "reason=timeout");
    }
    if (state->n64.n64_runtime_media_ipc_path[0]) {
        int opened = integral_n64_runtime_media_stream_open_host(state->n64.n64_runtime_media_stream,
                                                          state->n64.n64_runtime_media_ipc_path,
                                                          error,
                                                          sizeof(error));
        if (opened < 0 ||
            (opened == 0 &&
             integral_n64_runtime_media_stream_pump_host(state->n64.n64_runtime_media_stream,
                                                   state->n64.n64_runtime_media_connection,
                                                   (uint64_t)now * 1000u,
                                                   error,
                                                   sizeof(error)) < 0)) {
            client_room_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=host-encode role=host opened=%d error=%s",
                       opened,
                       error);
            reconnect_n64_transport(state, now);
            if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return true;
            state->n64.n64_runtime_media_retry_after_ticks = now + 5000u;
            snprintf(state->login->status, sizeof(state->login->status),
                     "N64 MEDIA ENCODE LOST %s", error);
            return false;
        }
    }
    log_n64_runtime_media_metrics(state, now);
    return true;
}


static void cycle_n64_room_slot(IntegralRoomContext *state, unsigned row, int delta)
{
    int candidates[INTEGRAL_ROM_SLOTS];
    unsigned count = 0;
    for (int i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        const IntegralConfigRomSlot *slot = room_registered_rom_slot_at(state, i);
        if (!slot) {
            continue;
        }
        if (row == 0) {
            if (slot_is_supported_n64(slot)) {
                candidates[count++] = i;
            }
            continue;
        }
        char game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
        if (!n64_room_gb_game_type(state, i, game_type, sizeof(game_type))) {
            continue;
        }
        candidates[count++] = i;
    }
    if (count == 0) {
        copy_text(state->login->status,
                  sizeof(state->login->status),
                  row == 0 ? "USER1 N64 ROM REQUIRED" : "GB ROM REQUIRED");
        return;
    }

    int *selected_index = row == 0 ? &state->n64.n64_room_n64_slot_index
                                   : (row == 1 ? &state->n64.n64_room_user1_gb_slot_index
                                               : &state->n64.n64_room_user2_gb_slot_index);
    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if (*selected_index == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current < 0 ? (delta < 0 ? (int)count - 1 : 0) : current + delta;
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    *selected_index = candidates[next];

    state->n64.n64_room_ready = false;
    reset_n64_runtime_media_connection(state);
    snprintf(state->login->status,
             sizeof(state->login->status),
             "%s SELECTED ROM%d",
             row == 0 ? "USER1 N64" : (row == 1 ? "USER1 GB" : "USER2 GB"),
             *selected_index + 1);
}


void poll_n64_room_async(IntegralRoomContext *state, Uint32 now)
{
    if (!state->n64.n64_room_poll_worker) return;
    IntegralRoomPollResult result;
    int taken = integral_room_poll_worker_take(state->n64.n64_room_poll_worker, &result);
    if (taken < 0) {
        client_room_log(state, "room_poll_async_error", "stage=take");
    }
    else if (taken > 0) {
        bool current = (*state->screen) == SCREEN_N64_ROOM &&
                       state->common.room_number == result.room_number &&
                       state->common.room_poll_epoch == result.request_id &&
                       strcmp(state->login->server, result.server) == 0 &&
                       strcmp(state->login->token, result.token) == 0;
        if (current) {
            state->common.room_poll_last_ms = result.elapsed_ms;
            if (result.elapsed_ms > state->common.room_poll_max_ms) {
                state->common.room_poll_max_ms = result.elapsed_ms;
            }
            if (result.heartbeat_attempted && result.heartbeat_succeeded) {
                state->common.last_room_heartbeat_ticks = now;
            }
            if (result.heartbeat_attempted) {
                handle_room_heartbeat_result(state,
                                              &result.heartbeat_status,
                                              result.heartbeat_succeeded,
                                              result.error);
            }
            if ((*state->screen) != SCREEN_N64_ROOM ||
                state->common.room_poll_epoch != result.request_id) return;
            if (result.room_result == 0) {
                state->common.current_room = result.room;
                load_room_chat_from_api(state);
                sync_room_ready_flags_from_api(state);
            }
            else {
                client_room_log(state,
                           "room_poll_async_error",
                           "stage=room elapsed_ms=%u error=%s",
                           result.elapsed_ms,
                           result.error);
            }
            if (result.heartbeat_attempted && !result.heartbeat_succeeded) {
                client_room_log(state,
                           "room_poll_async_error",
                           "stage=heartbeat elapsed_ms=%u error=%s",
                           result.elapsed_ms,
                           result.error);
            }
        }
    }
    if ((*state->screen) != SCREEN_N64_ROOM || state->login->token[0] == '\0' ||
        now - state->common.last_room_poll_ticks < 1000u ||
        integral_room_poll_worker_is_busy(state->n64.n64_room_poll_worker)) return;
    bool heartbeat_due = integral_room_heartbeat_attempt_due(
        state->common.last_room_heartbeat_attempt_ticks,
        state->common.room_heartbeat_attempted,
        now,
        INTEGRAL_ROOM_HEARTBEAT_MS);
    int started = integral_room_poll_worker_start(state->n64.n64_room_poll_worker,
                                              state->login->server,
                                              state->login->token,
                                              state->common.room_number,
                                              state->common.room_poll_epoch,
                                              heartbeat_due);
    if (started == 0) {
        state->common.last_room_poll_ticks = now;
        if (heartbeat_due) {
            state->common.last_room_heartbeat_attempt_ticks = now;
            state->common.room_heartbeat_attempted = true;
        }
    }
    else if (started < 0) {
        client_room_log(state, "room_poll_async_error", "stage=start error=%s", SDL_GetError());
    }
}


void activate_n64_room(IntegralRoomContext *state, const IntegralApiRoom *matched_room)
{
    IntegralApiRoom room_copy = *matched_room;
    state->common.room_number = room_copy.room_number;
    memset(&state->common.current_room, 0, sizeof(state->common.current_room));
    state->common.current_room = room_copy;
    state->common.room_selected = 0;
    state->n64.n64_room_ready = false;
    state->common.room_heartbeat_failures = 0;
    state->common.room_heartbeat_attempted = false;
    state->common.room_chat_editing = false;
    state->common.room_chat_scroll = 0;
    state->common.room_chat_input[0] = '\0';
    state->common.room_chat_composition[0] = '\0';
    memset(state->common.room_chat_log, 0, sizeof(state->common.room_chat_log));
    reset_n64_runtime_media_connection(state);
    init_n64_room_selection(state);
    (*state->screen) = SCREEN_N64_ROOM;
    refresh_room_quiet(state);
    if (sync_n64_room_state(state, false)) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 ROOM SELECTION SYNCED");
    }
    client_room_log(state, "room_activate", "room=%u mode=n64", state->common.room_number);
}


void handle_n64_room_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key)
{
    if (state->n64.host_finish_pending || state->n64.terminal_pending) {
        if (!key->repeat && key->keysym.sym == SDLK_ESCAPE &&
            !state->n64.n64_runtime_media_host_pid && !n64_room_runtime_active(state)) {
            char error[160];
            const char *session = state->n64.lifecycle_session_id[0]
                ? state->n64.lifecycle_session_id : state->n64.n64_runtime_media_session_id;
            int result = state->n64.host_finish_pending
                ? integral_api_finish_n64_room(state->login->server, state->login->token,
                                              session, error, sizeof(error))
                : integral_api_terminate_n64_room(state->login->server, state->login->token,
                    session, state->n64.lifecycle_room_code, error, sizeof(error));
            if (result != 0) {
                copy_text(state->login->status, sizeof(state->login->status),
                          "ROOM EXIT UNCONFIRMED - ESC RETRY");
                return;
            }
            finish_n64_room_locally(state);
            copy_text(state->login->status, sizeof(state->login->status), "N64 GAME ENDED");
        }
        return;
    }
    if (key->repeat) {
        return;
    }
    if (n64_room_runtime_active(state)) {
        if (key->keysym.sym == integral_gb_runtime_key_config_key_from_name(state->keys->escape))
            request_runtime_exit_confirmation(state, false);
        else if (key->keysym.sym == integral_gb_runtime_key_config_key_from_name(state->keys->screenshot))
            capture_n64_room(state);
        return;
    }
    if (state->n64.n64_runtime_media_paired && strcmp(state->n64.n64_runtime_media_role, "remote") == 0 &&
        key->keysym.sym != SDLK_ESCAPE && n64_remote_controller_scancode(state, key->keysym.scancode)) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            if (n64_room_runtime_active(state)) {
                request_runtime_exit_confirmation(state, false);
                break;
            }
            leave_current_room(state, false);
            if (state->common.room_number) return;
            (*state->screen) = SCREEN_MAIN_MENU;
            copy_text(state->login->status, sizeof(state->login->status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            state->common.room_selected = (state->common.room_selected + 1) % INTEGRAL_N64_RUNTIME_ROOM_ROWS;
            break;
        case SDLK_UP:
            state->common.room_selected = state->common.room_selected == 0 ? INTEGRAL_N64_RUNTIME_ROOM_ROWS - 1 : state->common.room_selected - 1;
            break;
        case SDLK_LEFT:
            if (state->common.room_selected <= 2) {
                int local_user = n64_room_local_user_index(state);
                if ((local_user == 0 && state->common.room_selected <= 1) ||
                    (local_user == 1 && state->common.room_selected == 2)) {
                    cycle_n64_room_slot(state, state->common.room_selected, -1);
                    (void)sync_n64_room_state(state, false);
                }
            }
            else if (state->common.room_selected == 4) {
                unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->common.room_chat_log);
                unsigned max_scroll = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
                if (state->common.room_chat_scroll < max_scroll) {
                    state->common.room_chat_scroll++;
                }
            }
            break;
        case SDLK_RIGHT:
            if (state->common.room_selected <= 2) {
                int local_user = n64_room_local_user_index(state);
                if ((local_user == 0 && state->common.room_selected <= 1) ||
                    (local_user == 1 && state->common.room_selected == 2)) {
                    cycle_n64_room_slot(state, state->common.room_selected, 1);
                    (void)sync_n64_room_state(state, false);
                }
            }
            else if (state->common.room_selected == 4 && state->common.room_chat_scroll > 0) {
                state->common.room_chat_scroll--;
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->common.room_selected <= 2) {
                int local_user = n64_room_local_user_index(state);
                if ((local_user == 0 && state->common.room_selected <= 1) ||
                    (local_user == 1 && state->common.room_selected == 2)) {
                    cycle_n64_room_slot(state, state->common.room_selected, 1);
                    (void)sync_n64_room_state(state, false);
                }
            }
            else if (state->common.room_selected == 3) {
                bool next_ready = !state->n64.n64_room_ready;
                if (next_ready && !resolve_n64_room_local_outbox(state)) {
                    state->n64.n64_room_ready = false;
                    reset_n64_runtime_media_connection(state);
                    break;
                }
                if (!sync_n64_room_state(state, next_ready)) {
                    state->n64.n64_room_ready = false;
                    reset_n64_runtime_media_connection(state);
                    break;
                }
                state->n64.n64_room_ready = next_ready;
                state->n64.preflight_failed = false; /* Explicit READY is a new attempt. */
                if (!next_ready) {
                    reset_n64_runtime_media_connection(state);
                }
                copy_text(state->login->status,
                          sizeof(state->login->status),
                          state->n64.n64_room_ready ? "N64 ROOM READY  NO SAV OVERWRITE"
                                                : "N64 ROOM READY CANCELLED");
                if (state->n64.n64_room_ready && state->common.room_ready_self && state->common.room_ready_peer) {
                    (void)request_n64_runtime_media_session(state);
                }
            }
            break;
        default:
            break;
    }
}


static uint64_t n64_remote_keyboard_buttons(const IntegralRoomContext *state)
{
    static const unsigned protocol_bits[INTEGRAL_N64_RUNTIME_KEY_BUTTONS] = {
        0, 1, 2, 3, 6, 7, 5, 4, 10, 11, 12, 13, 9, 8, 14, 15, 16, 17,
    };
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    const Uint8 *pressed = SDL_GetKeyboardState(NULL);
    uint64_t buttons = 0;
    key_spec_to_names_count(state->keys->n64_p1, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    for (unsigned index = 0; index < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; index++) {
        if (configured_controller_binding(names[index])) {
            SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(names[index]);
            if (binding != SDLK_UNKNOWN && integral_gb_runtime_key_config_binding_pressed(binding)) {
                buttons |= 1ULL << protocol_bits[index];
            }
            continue;
        }
        int scancode = n64_key_name_to_scancode(names[index]);
        if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES && pressed[scancode]) {
            buttons |= 1ULL << protocol_bits[index];
        }
    }
    return buttons;
}


static bool n64_remote_controller_scancode(const IntegralRoomContext *state, SDL_Scancode scancode)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES) return false;
    key_spec_to_names_count(state->keys->n64_p1, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    for (unsigned index = 0; index < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; index++) {
        if (n64_key_name_to_scancode(names[index]) == (int)scancode) return true;
    }
    return false;
}


static void poll_n64_room_host_process(IntegralRoomContext *state)
{
    if (!state) return;
    if (state->n64.host_finish_pending) {
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(now - state->n64.n64_runtime_media_retry_after_ticks) < 0) return;
        char finish_error[160] = {0};
        int finish_result = integral_api_finish_n64_room(state->login->server, state->login->token,
                state->n64.n64_runtime_media_session_id, finish_error, sizeof(finish_error));
        if (finish_result != 0) {
            IntegralApiHeartbeatStatus lifecycle;
            if (integral_api_n64_media_state(state->login->server, state->login->token,
                    state->n64.n64_runtime_media_session_id, 0, &lifecycle,
                    finish_error, sizeof(finish_error)) == 0) {
                handle_room_heartbeat_result(state, &lifecycle, true, NULL);
                if ((*state->screen) != SCREEN_N64_ROOM || state->n64.preflight_failed) return;
            }
        }
        if (finish_result != 0) {
            state->n64.n64_runtime_media_retry_after_ticks = now + 2000u;
            copy_text(state->login->status, sizeof(state->login->status), "N64 FINISH PENDING - RETRYING");
            client_room_log(state, "n64_finish_pending", "session=%s", state->n64.n64_runtime_media_session_id);
            return;
        }
        IntegralApiHeartbeatStatus lifecycle = {0};
        copy_text(lifecycle.lifecycle_kind, sizeof(lifecycle.lifecycle_kind), "media");
        copy_text(lifecycle.lifecycle_session_id, sizeof(lifecycle.lifecycle_session_id),
                  state->n64.n64_runtime_media_session_id);
        copy_text(lifecycle.lifecycle_status, sizeof(lifecycle.lifecycle_status), "COMPLETED");
        copy_text(lifecycle.termination_reason, sizeof(lifecycle.termination_reason), "host_finished");
        handle_room_heartbeat_result(state, &lifecycle, true, NULL);
        return;
    }
    if (state->n64.n64_runtime_media_host_pid == 0) return;
    int status = 0;
    IntegralChildProcess result = waitpid(state->n64.n64_runtime_media_host_pid, &status, WNOHANG);
    if (result == state->n64.n64_runtime_media_host_pid || (result < 0 && errno == ECHILD)) {
        client_room_log(state,
                   "n64_room_host_exit",
                   "session=%s status=%d wait_result=%ld no_save=1",
                   state->n64.n64_runtime_media_launched_session_id,
                   status,
                   (long)result);
        if (state->n64.n64_runtime_stop_request_path[0] != '\0') {
            (void)remove(state->n64.n64_runtime_stop_request_path);
        }
        state->n64.n64_runtime_media_host_pid = 0;
        state->n64.n64_runtime_media_launched_session_id[0] = '\0';
        state->n64.n64_runtime_stop_request_path[0] = '\0';
        if (result > 0 && child_process_exit_code(status) == 0) {
            state->n64.host_finish_pending = true;
            state->n64.n64_runtime_media_retry_after_ticks = 0;
            poll_n64_room_host_process(state);
            return;
        }
        leave_current_room(state, true);
        if (state->common.room_number) return;
        (*state->screen) = SCREEN_MAIN_MENU;
        copy_text(state->login->status, sizeof(state->login->status), "N64 GAME STOPPED UNEXPECTEDLY");
    }
}


static bool download_n64_runtime_save_to_file(IntegralRoomContext *state,
                                              const char *kind,
                                              const char *path)
{
    unsigned char *save_data = malloc(INTEGRAL_MAX_SAVE_BYTES);
    size_t save_size = 0;
    int revision = 0;
    char error[160];
    if (!save_data) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 NO-SAVE OUT OF MEMORY");
        return false;
    }
    int rc = integral_api_download_n64_runtime_save(state->login->server,
                                               state->login->token,
                                               state->n64.n64_runtime_media_session_id,
                                               kind,
                                               save_data,
                                               INTEGRAL_MAX_SAVE_BYTES,
                                               &save_size,
                                               &revision,
                                               error,
                                               sizeof(error));
    if (rc != 0 || save_size == 0 ||
        write_private_runtime_file(path, save_data, save_size) != 0) {
        free(save_data);
        snprintf(state->login->status,
                 sizeof(state->login->status),
                 "N64 NO-SAVE %s FAILED %.80s",
                 kind,
                 rc != 0 ? error : "LOCAL STAGE");
        return false;
    }
    free(save_data);
    client_room_log(state,
               "n64_runtime_save_staged",
               "session=%s kind=%s revision=%d bytes=%zu no_save=1",
               state->n64.n64_runtime_media_session_id,
               kind,
               revision,
               save_size);
    return true;
}


typedef struct N64TransferSend {
    TransferSavPair pair;
    char session[192];
    int fd;
} N64TransferSend;

static int send_n64_transfer_saves(void *opaque)
{
    N64TransferSend *send = opaque;
    int result = transfer_sav_send(transfer_sav_pipe_write, &send->fd,
                                   send->session, &send->pair);
    transfer_sav_close(send->fd);
    transfer_sav_pair_clear(&send->pair);
    free(send);
    return result;
}

static N64TransferSend *prepare_n64_transfer_saves(IntegralRoomContext *state, const char *roms[2])
{
    N64TransferSend *send = calloc(1, sizeof(*send));
    const char *kinds[2] = {"host-gb", "remote-gb"};
    if (!send) return NULL;
    send->fd = -1;
    copy_text(send->session, sizeof(send->session), state->n64.n64_runtime_media_session_id);
    for (int i = 0; i < 2; ++i) {
        char error[160];
        int revision = 0;
        if (!integral_gb_runtime_secure_buffer_init(&send->pair.saves[i], TRANSFER_SAV_MAX,
                INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT)) {
            transfer_sav_pair_clear(&send->pair);
            free(send);
            return NULL;
        }
        int result = integral_api_download_n64_runtime_save(state->login->server, state->login->token,
                send->session, kinds[i], send->pair.saves[i].data, TRANSFER_SAV_MAX,
                &send->pair.lengths[i], &revision, error, sizeof(error));
        if (result != 0 || !send->pair.lengths[i] || revision < 0) {
            /* HTTP validation/not-found and invalid payload are not transport loss. */
            state->n64.preflight_failed = result == -2 || result == 400 ||
                result == 404 || result == 422 || (result == 0);
            if (state->n64.preflight_failed)
                snprintf(state->login->status, sizeof(state->login->status),
                         "SLOT%d SAV INVALID: RUN IN LOCAL, EXIT NORMALLY, THEN RETRY.", i+1);
            transfer_sav_pair_clear(&send->pair);
            free(send);
            return NULL;
        }
        send->pair.revisions[i] = (uint32_t)revision;
        unsigned char header[0x150];
        FILE *rom = integral_fopen(roms[i], "rb");
        size_t count = rom ? fread(header, 1, sizeof(header), rom) : 0;
        if (rom) fclose(rom);
        if (count != sizeof(header) || !transfer_sav_ready(header,
                send->pair.saves[i].data, send->pair.lengths[i])) {
            state->n64.preflight_failed = true;
            snprintf(state->login->status, sizeof(state->login->status),
                     "SLOT%d: RUN IN LOCAL, EXIT NORMALLY, THEN RETRY.", i+1);
            transfer_sav_pair_clear(&send->pair);
            free(send);
            return NULL;
        }
    }
    return send;
}

/* Only retired ROOM Transfer Pak files; never ROMs, N64 saves or outbox data. */
static const char *const n64_retired_transfer_files[] = {
    "slot1.sav", "slot1.sav.part", "slot1.sav.rtc", "slot1.sav.rtc.part",
    "slot1.sav.mupen", "slot1.sav.mupen.part",
    "slot1.sav.mupen.rtc", "slot1.sav.mupen.rtc.part",
    "slot2.sav", "slot2.sav.part", "slot2.sav.rtc", "slot2.sav.rtc.part",
    "slot2.sav.mupen", "slot2.sav.mupen.part",
    "slot2.sav.mupen.rtc", "slot2.sav.mupen.rtc.part",
};

static int n64_transfer_path_type(const char *path, bool directory)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 0 : -1;
    }
    if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return -1;
    return !!(attributes & FILE_ATTRIBUTE_DIRECTORY) == directory ? 1 : -1;
#else
    struct stat info;
    if (lstat(path, &info) != 0) return errno == ENOENT ? 0 : -1;
    if (info.st_uid != geteuid()) return -1;
    return (directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode)) ? 1 : -1;
#endif
}

static bool remove_retired_n64_transfer_files(IntegralRoomContext *state)
{
    const char *id = state->n64.n64_runtime_media_session_id;
    if (state->n64.n64_runtime_media_host_pid != 0 ||
        !runtime_session_id_is_path_safe(id) ||
        (state->n64.n64_runtime_media_launched_session_id[0] &&
         strcmp(id, state->n64.n64_runtime_media_launched_session_id))) return false;
    char session[INTEGRAL_CONFIG_PATH_MAX], transfer[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(session, sizeof(session), INTEGRAL_N64_RUNTIME_MEDIA_DIR, id, NULL) ||
        !format_runtime_session_path(transfer, sizeof(transfer), INTEGRAL_N64_RUNTIME_MEDIA_DIR, id, "transfer")) return false;
    const char *directories[] = {"runtime", INTEGRAL_N64_RUNTIME_MEDIA_DIR, session, transfer};
    for (size_t i=0; i<sizeof(directories)/sizeof(*directories); ++i) {
        int type = n64_transfer_path_type(directories[i], true);
        if (type < 0) return false;
        if (!type) return true; /* No session data can exist below an absent parent. */
    }
    char paths[sizeof(n64_retired_transfer_files)/sizeof(*n64_retired_transfer_files)][INTEGRAL_CONFIG_PATH_MAX];
    /* Validate every exact target before deleting any of them. No link traversal. */
    for (size_t i=0; i<sizeof(paths)/sizeof(*paths); ++i) {
        int length = snprintf(paths[i], sizeof(paths[i]), "%s/%s", transfer, n64_retired_transfer_files[i]);
        if (length < 0 || (size_t)length >= sizeof(paths[i]) ||
            n64_transfer_path_type(paths[i], false) < 0) return false;
    }
    for (size_t i=0; i<sizeof(paths)/sizeof(*paths); ++i) {
        if (remove(paths[i]) != 0 && errno != ENOENT) return false;
    }
    return true;
}

static bool start_n64_room_host_n64_runtime(IntegralRoomContext *state)
{
    const IntegralConfigRomSlot *n64_slot = room_registered_rom_slot_at(state, state->n64.n64_room_n64_slot_index);
    const IntegralConfigRomSlot *host_gb_slot = room_registered_rom_slot_at(state, state->n64.n64_room_user1_gb_slot_index);
    int remote_rom_index = -1;
    const IntegralConfigRomSlot *remote_gb_rom_slot = find_n64_room_host_slot2_rom(state, &remote_rom_index);
    if (n64_room_local_user_index(state) != 0 || !n64_slot || !host_gb_slot ||
        !slot_is_supported_n64(n64_slot) || !slot_is_supported_gb(host_gb_slot) ||
        !state->n64.n64_runtime_media_remote_input_path[0]) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 HOST SELECTION INVALID");
        return false;
    }
    if (!remote_gb_rom_slot) {
        copy_text(state->login->status, sizeof(state->login->status), "USER1 NEEDS USER2 GB VERSION IN ROM1-ROM8");
        return false;
    }

    char frontend[INTEGRAL_CONFIG_PATH_MAX];
    char core[INTEGRAL_CONFIG_PATH_MAX];
    char video[INTEGRAL_CONFIG_PATH_MAX];
    char audio[INTEGRAL_CONFIG_PATH_MAX];
    char input[INTEGRAL_CONFIG_PATH_MAX];
    char rsp[INTEGRAL_CONFIG_PATH_MAX];
    char data[INTEGRAL_CONFIG_PATH_MAX];
    bool runtime_paths_ok = integral_n64_runtime_paths(frontend,
                                 sizeof(frontend),
                                 core,
                                 sizeof(core),
                                 video,
                                 sizeof(video),
                                 audio,
                                 sizeof(audio),
                                 input,
                                 sizeof(input),
                                 rsp,
                                 sizeof(rsp),
                                 data,
                                 sizeof(data));
    int frontend_access = runtime_paths_ok ? integral_runtime_frontend_access(frontend) : -1;
    if (!runtime_paths_ok || frontend_access != 0) {
        client_room_log(state,
                   "n64_runtime_probe_failed",
                   "home=%s frontend=%s paths_ok=%d access=%d errno=%d",
                   integral_n64_runtime_home_path(),
                   frontend,
                   runtime_paths_ok ? 1 : 0,
                   frontend_access,
                   errno);
        copy_text(state->login->status, sizeof(state->login->status), "N64_RUNTIME NOT FOUND");
        return false;
    }

    char session_dir[INTEGRAL_CONFIG_PATH_MAX];
    char transfer_dir[INTEGRAL_CONFIG_PATH_MAX];
    char n64_save_dir[INTEGRAL_CONFIG_PATH_MAX];
    char config_dir[INTEGRAL_CONFIG_PATH_MAX];
    char screenshot_dir[INTEGRAL_CONFIG_PATH_MAX] = "screenshot";
    char slot1_rom[INTEGRAL_CONFIG_PATH_MAX];
    char slot2_rom[INTEGRAL_CONFIG_PATH_MAX];
    char n64_save[INTEGRAL_CONFIG_PATH_MAX];
    char remote_media_ipc[INTEGRAL_CONFIG_PATH_MAX];
    char stop_request_file[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(session_dir, sizeof(session_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, NULL) ||
        !format_runtime_session_path(transfer_dir, sizeof(transfer_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "transfer") ||
        !format_runtime_session_path(n64_save_dir, sizeof(n64_save_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "n64-save") ||
        !format_runtime_session_path(config_dir, sizeof(config_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "config") ||
        !format_runtime_session_path(slot1_rom, sizeof(slot1_rom), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "transfer/slot1.gbc") ||
        !format_runtime_session_path(slot2_rom, sizeof(slot2_rom), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "transfer/slot2.gbc") ||
        !format_runtime_session_path(n64_save, sizeof(n64_save), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "n64-save/n64.sav") ||
        !format_runtime_session_path(remote_media_ipc, sizeof(remote_media_ipc), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "remote-media.ipc") ||
        !format_runtime_session_path(stop_request_file, sizeof(stop_request_file), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64.n64_runtime_media_session_id, "stop.request")) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 SESSION PATH INVALID");
        return false;
    }
    if (!remove_retired_n64_transfer_files(state)) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 TRANSFER CLEANUP FAILED");
        return false;
    }
    if (ensure_private_runtime_directory(transfer_dir) != 0 ||
        ensure_private_runtime_directory(n64_save_dir) != 0 ||
        ensure_private_runtime_directory(config_dir) != 0 ||
        ensure_private_runtime_directory(screenshot_dir) != 0) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 NO-SAVE RUNTIME CREATE FAILED");
        return false;
    }
    if (remove(stop_request_file) != 0 && errno != ENOENT) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 STOP IPC PREP FAILED");
        return false;
    }
    if (copy_binary_file_limited(host_gb_slot->rom_path, slot1_rom, INTEGRAL_MAX_ROM_BYTES) != 0 ||
        copy_binary_file_limited(remote_gb_rom_slot->rom_path, slot2_rom, INTEGRAL_MAX_ROM_BYTES) != 0) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 HOST GB ROM STAGE FAILED");
        return false;
    }
    client_room_log(state,
               "n64_transfer_roms_staged",
               "session=%s slot1=ROM%d slot2=ROM%d remote_game_type=%s rom_transfer=disabled",
               state->n64.n64_runtime_media_session_id,
               state->n64.n64_room_user1_gb_slot_index + 1,
               remote_rom_index + 1,
               state->common.current_room.slot_game_type2);
    if (!download_n64_runtime_save_to_file(state, "n64", n64_save)) {
        return false;
    }

    char hotkeys[640];
    if (!make_n64_runtime_util_hotkeys(state->keys, false, hotkeys, sizeof(hotkeys))) return false;
    char controller_map[512];
    if (!make_n64_runtime_keymap_spec(state->keys->n64_p1,
                                    controller_map,
                                    sizeof(controller_map))) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 KEY CONFIG INVALID");
        return false;
    }

    const char *transfer_roms[2] = {slot1_rom, slot2_rom};
    copy_text(state->login->status, sizeof(state->login->status), "N64 TRANSFER SAV PREP FAILED");
    N64TransferSend *send = prepare_n64_transfer_saves(state, transfer_roms);
    if (!send) return false;
    int sav_pipe[2];
    if (!send || transfer_sav_pipe(sav_pipe) != 0) {
        if (send) { transfer_sav_pair_clear(&send->pair); free(send); }
        copy_text(state->login->status, sizeof(state->login->status), "N64 TRANSFER SAV PREP FAILED");
        return false;
    }
    send->fd = sav_pipe[1];
    char sav_fd[32];
    snprintf(sav_fd, sizeof(sav_fd), "%d", sav_pipe[0]);
#ifdef _WIN32
    const char *runtime_argv[] = {
                                      frontend,
                                      "--transfer-sav-fd", sav_fd,
                                      "--transfer-sav-session", send->session,
                                      "--rom",
                                      n64_slot->rom_path,
                                      "--core",
                                      core,
                                      "--config-dir",
                                      config_dir,
                                      "--data-dir",
                                      data,
                                      "--hotkeys", hotkeys,
              "--screenshot-dir",
                                      screenshot_dir,
                                      "--save-dir",
                                      n64_save_dir,
                                      "--save-name",
                                      "n64",
                                      "--video",
                                      video,
                                      "--audio",
                                      audio,
                                      "--input",
                                      input,
                                      "--rsp",
                                      rsp,
                                      "--transfer-storage",
                                      transfer_dir,
                                      "--controller1",
                                      "keyboard",
                                      "--controller-map1",
                                      controller_map,
                                      "--remote-input-file",
                                      state->n64.n64_runtime_media_remote_input_path,
                                      "--remote-media-file",
                                      remote_media_ipc,
                                      "--stop-request-file",
                                      stop_request_file,
                                      "--interactive",
                                      NULL};
    IntegralChildProcess spawned = integral_windows_spawnv(_P_NOWAIT, frontend, runtime_argv);
    if (spawned == -1) {
        transfer_sav_close(sav_pipe[0]); transfer_sav_close(sav_pipe[1]);
        transfer_sav_pair_clear(&send->pair); free(send);
        copy_text(state->login->status, sizeof(state->login->status), "N64 HOST START FAILED");
        return false;
    }
#else
    pid_t spawned = fork();
    if (spawned < 0) {
        transfer_sav_close(sav_pipe[0]); transfer_sav_close(sav_pipe[1]);
        transfer_sav_pair_clear(&send->pair); free(send);
        copy_text(state->login->status, sizeof(state->login->status), "N64 HOST START FAILED");
        return false;
    }
    if (spawned == 0) {
        transfer_sav_close(sav_pipe[1]);
        redirect_child_output_to_client_log();
        execl(frontend,
              frontend,
              "--transfer-sav-fd", sav_fd,
              "--transfer-sav-session", send->session,
              "--rom",
              n64_slot->rom_path,
              "--core",
              core,
              "--config-dir",
              config_dir,
              "--data-dir",
              data,
              "--hotkeys", hotkeys,
              "--screenshot-dir",
              screenshot_dir,
              "--save-dir",
              n64_save_dir,
              "--save-name",
              "n64",
              "--video",
              video,
              "--audio",
              audio,
              "--input",
              input,
              "--rsp",
              rsp,
              "--transfer-storage",
              transfer_dir,
              "--controller1",
              "keyboard",
              "--controller-map1",
              controller_map,
              "--remote-input-file",
              state->n64.n64_runtime_media_remote_input_path,
              "--remote-media-file",
              remote_media_ipc,
              "--stop-request-file",
              stop_request_file,
              "--interactive",
              (char *)NULL);
        _exit(127);
    }
#endif
    transfer_sav_close(sav_pipe[0]);
    state->n64.n64_runtime_media_host_pid = spawned;
    copy_text(state->n64.n64_runtime_media_ipc_path,
              sizeof(state->n64.n64_runtime_media_ipc_path),
              remote_media_ipc);
    copy_text(state->n64.n64_runtime_stop_request_path,
              sizeof(state->n64.n64_runtime_stop_request_path),
              stop_request_file);
    copy_text(state->n64.n64_runtime_media_launched_session_id,
              sizeof(state->n64.n64_runtime_media_launched_session_id),
              state->n64.n64_runtime_media_session_id);
    SDL_Thread *sender = SDL_CreateThread(send_n64_transfer_saves, "n64-sav-ipc", send);
    if (!sender) {
        transfer_sav_close(send->fd); transfer_sav_pair_clear(&send->pair); free(send);
        (void)stop_n64_runtime_media_host_process(state);
        copy_text(state->login->status, sizeof(state->login->status), "N64 TRANSFER SAV IPC FAILED");
        return false;
    }
    SDL_DetachThread(sender);
    copy_text(state->login->status,
              sizeof(state->login->status),
              "N64 HOST STARTED  NO SAV OVERWRITE");
    client_room_log(state,
               "n64_room_host_started",
               "session=%s pid=%ld transfer_slots=2 no_save=1",
               state->n64.n64_runtime_media_session_id,
               (long)spawned);
    return true;
}


static void reject_n64_preflight(IntegralRoomContext *state)
{
    char notice[sizeof(state->login->status)], error[160];
    copy_text(notice, sizeof(notice), state->login->status);
    int result = integral_api_reject_n64_preflight(state->login->server, state->login->token,
        state->n64.n64_runtime_media_session_id, state->n64.lifecycle_room_code,
        error, sizeof(error));
    suspend_n64_runtime_media_connection(state);
    state->n64.terminal_pending = false;
    state->common.room_ready_self = state->common.room_ready_peer = false;
    state->n64.n64_room_ready = false;
    state->common.room_poll_epoch++;
    if (result == 0) reset_n64_runtime_media_connection(state);
    state->common.current_room.ready1 = state->common.current_room.ready2 = 0;
    state->n64.preflight_failed = true;
    /* Keep identity on failed server stop for explicit Esc/leave, not launch retries. */
    copy_text(state->login->status, sizeof(state->login->status),
              result == 0 ? notice : "SAV INVALID; SERVER STOP UNCONFIRMED - ESC TO EXIT");
}

static bool maybe_start_n64_room_host_n64_runtime(IntegralRoomContext *state, Uint32 now)
{
    if (state->n64.preflight_failed) return false;
    if (!state || !state->n64.n64_runtime_media_paired || strcmp(state->n64.n64_runtime_media_role, "host") != 0) {
        return true;
    }
    if (strcmp(state->n64.n64_runtime_media_launched_session_id, state->n64.n64_runtime_media_session_id) == 0) {
        if (!state->n64.n64_runtime_media_ipc_path[0] &&
            runtime_session_id_is_path_safe(state->n64.n64_runtime_media_session_id)) {
            (void)format_runtime_session_path(state->n64.n64_runtime_media_ipc_path,
                                              sizeof(state->n64.n64_runtime_media_ipc_path),
                                              INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                              state->n64.n64_runtime_media_session_id,
                                              "remote-media.ipc");
        }
        return true;
    }
    if (state->n64.n64_runtime_media_host_pid != 0) {
        copy_text(state->login->status, sizeof(state->login->status), "N64 HOST PROCESS ALREADY RUNNING");
        return false;
    }
    if (state->n64.n64_runtime_media_host_launch_retry_after_ticks != 0 &&
        (Sint32)(now - state->n64.n64_runtime_media_host_launch_retry_after_ticks) < 0) {
        return false;
    }
    if (!start_n64_room_host_n64_runtime(state)) {
        client_room_log(state,
                   "n64_room_host_start_failed",
                   "session=%s status=%s",
                   state->n64.n64_runtime_media_session_id,
                   state->login->status);
        if (state->n64.preflight_failed) {
            reject_n64_preflight(state);
            return false;
        }
        state->n64.n64_runtime_media_host_launch_retry_after_ticks = now + 5000u;
        return false;
    }
    state->n64.n64_runtime_media_host_launch_retry_after_ticks = 0;
    return true;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_gb_local.h"
#include "windows_process.h"
#include "http_client.h"
#include "client_file_io.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#include <io.h>
#ifndef X_OK
#define X_OK 0
#endif
#else
#include <unistd.h>
#endif

void integral_gb_local_pair_window_size(unsigned client_width, unsigned client_height,
                                       unsigned available_width, unsigned available_height,
                                       unsigned *width, unsigned *height)
{
    uint64_t w = (uint64_t)client_width * 2u;
    uint64_t h = client_height;
    unsigned aw = available_width, ah = available_height;
    if (aw == 0u || aw > INTEGRAL_CONFIG_WINDOW_MAX_SIZE) aw = INTEGRAL_CONFIG_WINDOW_MAX_SIZE;
    if (ah == 0u || ah > INTEGRAL_CONFIG_WINDOW_MAX_SIZE) ah = INTEGRAL_CONFIG_WINDOW_MAX_SIZE;
    if (w < 320u) w = 320u;
    if (h < 144u) h = 144u;
    if (w > aw || h > ah) {
        if (w * ah > h * aw) {
            h = h * aw / w;
            w = aw;
        }
        else {
            w = w * ah / h;
            h = ah;
        }
    }
    *width = w < 320u ? 320u : (unsigned)w;
    *height = h < 144u ? 144u : (unsigned)h;
}

void integral_gb_local_run(const IntegralGbLocalRequest *request)
{
    const IntegralConfigRomSlot *selected_slot1 = request->slot1;
    const IntegralConfigRomSlot *selected_slot2 = request->slot2;
    if (!selected_slot1) {
        snprintf(request->status, request->status_size, "SLOT1 ROM REQUIRED");
        return;
    }
    if (access(request->runtime, X_OK) != 0) {
        snprintf(request->status, request->status_size, "GB_RUNTIME SERVER NOT FOUND");
        return;
    }
    if (request->token[0] == '\0') {
        snprintf(request->status, request->status_size, "LOGIN TOKEN REQUIRED");
        return;
    }
    if (selected_slot1->save_id[0] == '\0') {
        snprintf(request->status, request->status_size, "REGISTER SLOT1 FIRST");
        return;
    }
    if (selected_slot2 && selected_slot2->save_id[0] == '\0') {
        snprintf(request->status, request->status_size, "REGISTER SLOT2 FIRST");
        return;
    }
    if (!request->recover(request->context, selected_slot1->save_id) ||
        (selected_slot2 && !request->recover(request->context, selected_slot2->save_id))) {
        if (request->log) request->log(request->context, "local_start_blocked", "reason=outbox_recovery");
        return;
    }
    char error[160];
    char game_session_id[96];
    long long fencing_token = 0;
    if (integral_api_start_local_game(request->server,
                                 request->token,
                                 selected_slot1->save_id,
                                 selected_slot2 ? selected_slot2->save_id : NULL,
                                 game_session_id,
                                 sizeof(game_session_id),
                                 &fencing_token,
                                 error,
                                 sizeof(error)) != 0) {
        snprintf(request->status, request->status_size, "LOCAL GAME LOCK FAILED %s", error);
        return;
    }
    if (!runtime_session_id_is_path_safe(game_session_id)) {
        (void)integral_api_stop_local_game(request->server, request->token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        snprintf(request->status, request->status_size, "GB_RUNTIME SESSION ID INVALID");
        return;
    }
    IntegralConfigRomSlot slot1 = *selected_slot1;
    IntegralConfigRomSlot slot2;
    memset(&slot2, 0, sizeof(slot2));
    if (selected_slot2) {
        slot2 = *selected_slot2;
    }
    char runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char slot1_save_path[INTEGRAL_CONFIG_PATH_MAX];
    char slot2_save_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(runtime_dir, sizeof(runtime_dir),
                                     "runtime/gb-sessions", game_session_id, NULL) ||
        !format_runtime_session_path(slot1_save_path, sizeof(slot1_save_path),
                                     "runtime/gb-sessions", game_session_id, "slot1.sav") ||
        !format_runtime_session_path(slot2_save_path, sizeof(slot2_save_path),
                                     "runtime/gb-sessions", game_session_id, "slot2.sav")) {
        (void)integral_api_stop_local_game(request->server, request->token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        snprintf(request->status, request->status_size, "GB_RUNTIME SESSION PATH INVALID");
        return;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/gb-sessions") != 0 ||
        ensure_private_runtime_directory(runtime_dir) != 0) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        snprintf(request->status, request->status_size, "GB_RUNTIME SESSION CREATE FAILED");
        return;
    }

    LocalSyncSlot sync_slots[2];
    memset(sync_slots, 0, sizeof(sync_slots));
    unsigned sync_count = 0;
    if (request->download(request->context, &slot1, slot1_save_path, &sync_slots[sync_count]) != 0) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        return;
    }
    sync_count++;
    if (selected_slot2) {
        if (request->download(request->context, &slot2, slot2_save_path, &sync_slots[sync_count]) != 0) {
            integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
            return;
        }
        sync_count++;
    }
    char rtc_offset_text[32];
    char ir_off_delay_text[16];
    snprintf(ir_off_delay_text, sizeof(ir_off_delay_text), "%u",
             integral_config_ir_off_delay(request->config_path));
    if (!request->rtc(request->context, rtc_offset_text, sizeof(rtc_offset_text))) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        return;
    }
    unsigned gb_window_width = 0u, gb_window_height = 0u;
    char gb_window_width_text[16], gb_window_height_text[16];
    request->window_size(request->context, &gb_window_width, &gb_window_height);
    snprintf(gb_window_width_text, sizeof(gb_window_width_text), "%u", gb_window_width);
    snprintf(gb_window_height_text, sizeof(gb_window_height_text), "%u", gb_window_height);

    const IntegralGbLocalLaunch launch = {
        .runtime = request->runtime,
        .rom1 = slot1.rom_path, .save1 = slot1_save_path,
        .rom2 = selected_slot2 ? slot2.rom_path : NULL,
        .save2 = slot2_save_path, .port = request->port,
        .rtc_offset = rtc_offset_text,
        .ir_off_delay_ticks = ir_off_delay_text,
        .sgb = integral_config_sgb_enabled(request->config_path) ? "enable" : "disable",
        .window_width = gb_window_width_text, .window_height = gb_window_height_text,
        .keys = request->keys,
    };
    const IntegralGbLocalSession session = {
        .monitor_out = request->monitor_out,
        .server = request->server, .token = request->token,
        .game_session_id = game_session_id, .fencing_token = fencing_token,
        .slots = sync_slots, .count = sync_count,
    };
    integral_gb_local_start(&launch, &session, request->status,
                            request->status_size, request->log, request->context);
}

void integral_gb_local_arguments(const IntegralGbLocalLaunch *launch,
                                const char *argv[INTEGRAL_GB_LOCAL_ARGV_CAPACITY])
{
    unsigned n = 0;
    argv[n++] = launch->runtime;
    argv[n++] = "--rom1";
    argv[n++] = launch->rom1;
    argv[n++] = "--save1";
    argv[n++] = launch->save1;
    argv[n++] = "--sgb";
    argv[n++] = launch->sgb ? launch->sgb : "enable";
    if (launch->rom2) {
        argv[n++] = "--rom2";
        argv[n++] = launch->rom2;
        argv[n++] = "--save2";
        argv[n++] = launch->save2;
        argv[n++] = "--ir-off-delay-ticks";
        argv[n++] = launch->ir_off_delay_ticks ? launch->ir_off_delay_ticks : "32";
    }
    else {
        argv[n++] = "--self";
    }
    argv[n++] = "--rtc-offset-seconds";
    argv[n++] = launch->rtc_offset;
    argv[n++] = "--display";
    argv[n++] = "--display-slots";
    argv[n++] = launch->rom2 ? "2" : "1";
    argv[n++] = "--window-width";
    argv[n++] = launch->window_width;
    argv[n++] = "--window-height";
    argv[n++] = launch->window_height;
    argv[n++] = "--slot1-keys";
    argv[n++] = launch->keys->slot1;
    if (launch->rom2) {
        argv[n++] = "--slot2-keys";
        argv[n++] = launch->keys->slot2;
    }
    argv[n++] = "--fast-key";
    argv[n++] = launch->keys->fast;
    argv[n++] = "--screenshot-key";
    argv[n++] = launch->keys->screenshot;
    argv[n++] = "--escape-key";
    argv[n++] = launch->keys->escape;
    argv[n++] = "--turbo-hold-key";
    argv[n++] = launch->keys->turbo_hold;
    argv[n++] = "--reset-key";
    argv[n++] = launch->keys->reset;
    argv[n++] = "--audio";
    argv[n] = NULL;
}

void integral_gb_local_start(const IntegralGbLocalLaunch *launch,
                             const IntegralGbLocalSession *session,
                             char *status, size_t status_size,
                             void (*log)(void *, const char *, const char *),
                             void *log_context)
{
    char error[160];
    char detail[64];
    const char *runtime_argv[INTEGRAL_GB_LOCAL_ARGV_CAPACITY];
    integral_gb_local_arguments(launch, runtime_argv);

#ifdef _WIN32
    intptr_t spawned = integral_windows_spawnv(_P_NOWAIT, launch->runtime, runtime_argv);
    if (spawned == -1) {
        integral_api_stop_local_game(session->server, session->token, session->game_session_id, session->fencing_token, error, sizeof(error));
        snprintf(status, status_size, "GB_RUNTIME START FAILED");
        snprintf(detail, sizeof(detail), "errno=%d", errno);
        if (log) log(log_context, "gb_runtime_server_spawn_failed", detail);
        return;
    }
    if (session->monitor_out) {
        HANDLE copy = NULL;
        if (DuplicateHandle(GetCurrentProcess(), (HANDLE)spawned, GetCurrentProcess(),
                            &copy, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
            *session->monitor_out = (IntegralChildProcess)copy;
        }
    }
    snprintf(status,
              status_size,
              launch->rom2 ? "GB_RUNTIME SERVER2 STARTED" : "GB_RUNTIME LOCAL STARTED");
    snprintf(detail, sizeof(detail), "pid=%ld", (long)spawned);
    if (log) log(log_context, "gb_runtime_server_started", detail);
    start_save_sync_thread(spawned, session->server, session->token, session->game_session_id, session->fencing_token, session->slots, session->count, true);
    return;
#else
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        integral_api_stop_local_game(session->server, session->token, session->game_session_id, session->fencing_token, error, sizeof(error));
        snprintf(status, status_size, "GB_RUNTIME START FAILED");
        return;
    }
    if (monitor_pid == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            integral_api_stop_local_game(session->server,
                                    session->token,
                                    session->game_session_id,
                                    session->fencing_token,
                                    error,
                                    sizeof(error));
            _exit(127);
        }
        if (pid == 0) {
            execv(launch->runtime, (char *const *)runtime_argv);
            _exit(127);
        }
        monitor_save_sync_process(pid, session->server, session->token, session->game_session_id, session->fencing_token, session->slots, session->count, true);
        _exit(0);
    }
    if (session->monitor_out) *session->monitor_out = monitor_pid;
    snprintf(status,
              status_size,
              launch->rom2 ? "GB_RUNTIME SERVER2 STARTED" : "GB_RUNTIME LOCAL STARTED");
    snprintf(detail, sizeof(detail), "pid=%ld", (long)monitor_pid);
    if (log) log(log_context, "gb_runtime_server_monitor_started", detail);
#endif
}

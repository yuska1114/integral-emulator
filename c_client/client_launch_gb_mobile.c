/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_gb_mobile.h"
#include "windows_process.h"
#include "http_client.h"
#include <stdio.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

void integral_gb_mobile_arguments(const IntegralGbMobileLaunch *launch,
                                 const char *argv[INTEGRAL_GB_MOBILE_ARGV_CAPACITY])
{
    argv[0] = launch->runtime;
    argv[1] = "--rom";
    argv[2] = launch->rom;
    argv[3] = "--save";
    argv[4] = launch->save;
    argv[5] = "--config";
    argv[6] = launch->adapter_config;
    argv[7] = "--session-manifest";
    argv[8] = launch->manifest;
    argv[9] = "--runtime-result";
    argv[10] = launch->result;
    argv[11] = "--rtc-offset-seconds";
    argv[12] = launch->rtc_offset;
    argv[13] = "--window-width";
    argv[14] = launch->window_width;
    argv[15] = "--window-height";
    argv[16] = launch->window_height;
    argv[17] = "--slot1-keys";
    argv[18] = launch->keys->slot1;
    argv[19] = "--fast-key";
    argv[20] = launch->keys->fast;
    argv[21] = "--screenshot-key";
    argv[22] = launch->keys->screenshot;
    argv[23] = "--escape-key";
    argv[24] = launch->keys->escape;
    argv[25] = "--turbo-hold-key";
    argv[26] = launch->keys->turbo_hold;
    argv[27] = "--reset-key";
    argv[28] = launch->keys->reset;
    argv[29] = NULL;
}

void integral_gb_mobile_start(const IntegralGbMobileLaunch *launch,
                              const IntegralGbMobileSession *session,
                              char *status, size_t status_size,
                              bool (*cleanup)(const char *),
                              void (*redirect_output)(void),
                              void (*log)(void *, const char *, const char *),
                              void *log_context)
{
    char error[160];
#ifdef _WIN32
    (void)redirect_output;
#endif
    const char *runtime_argv[INTEGRAL_GB_MOBILE_ARGV_CAPACITY];
    integral_gb_mobile_arguments(launch, runtime_argv);
#ifdef _WIN32
    intptr_t spawned = integral_windows_spawnv(_P_NOWAIT, launch->runtime, runtime_argv);
    if (spawned == -1) {
        integral_api_cancel_mobile_session(session->server, session->token, session->mobile_session_id, session->game_session_id, session->fencing_token, "runtime start failed", error, sizeof(error));
        (void)cleanup(session->runtime_dir);
        snprintf(status, status_size, "MOBILE START FAILED");
        return;
    }
    if (session->monitor_out) {
        HANDLE copy = NULL;
        if (DuplicateHandle(GetCurrentProcess(), (HANDLE)spawned, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
            *session->monitor_out = (IntegralChildProcess)copy;
    }
    start_save_sync_thread(spawned, session->server, session->token, session->game_session_id, session->fencing_token, session->sync_slot, 1, true);
#else
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        integral_api_cancel_mobile_session(session->server, session->token, session->mobile_session_id, session->game_session_id, session->fencing_token, "monitor start failed", error, sizeof(error));
        (void)cleanup(session->runtime_dir);
        snprintf(status, status_size, "MOBILE START FAILED");
        return;
    }
    if (monitor_pid == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            integral_api_cancel_mobile_session(session->server, session->token, session->mobile_session_id, session->game_session_id, session->fencing_token, "runtime fork failed", error, sizeof(error));
            _exit(127);
        }
        if (pid == 0) {
            redirect_output();
            execv(launch->runtime, (char *const *)runtime_argv);
            _exit(127);
        }
        monitor_save_sync_process(pid, session->server, session->token, session->game_session_id, session->fencing_token, session->sync_slot, 1, true);
        _exit(0);
    }
    if (session->monitor_out) *session->monitor_out = monitor_pid;
#endif
    snprintf(status, status_size, "MOBILE MODE STARTED");
    char detail[384];
    snprintf(detail, sizeof(detail), "mobile_session=%s game_session=%s scenario=%s",
             session->mobile_session_id, session->game_session_id, session->scenario_id);
    if (log) log(log_context, "gb_mobile_started", detail);
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_n64_local.h"
#include "windows_process.h"
#include "client_file_io.h"
#include "http_client.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

void integral_n64_local_arguments(const IntegralN64LocalLaunch *launch,
                                  const char *argv[INTEGRAL_N64_LOCAL_ARGV_CAPACITY])
{
    argv[0] = launch->frontend;
    argv[1] = "--rom";
    argv[2] = launch->rom;
    argv[3] = "--core";
    argv[4] = launch->core;
    argv[5] = "--config-dir";
    argv[6] = launch->config_dir;
    argv[7] = "--data-dir";
    argv[8] = launch->data;
    argv[9] = "--screenshot-dir";
    argv[10] = launch->screenshot_dir;
    argv[11] = "--save-dir";
    argv[12] = launch->save_dir;
    argv[13] = "--save-name";
    argv[14] = launch->save_name;
    argv[15] = "--video";
    argv[16] = launch->video;
    argv[17] = "--audio";
    argv[18] = launch->audio;
    argv[19] = "--input";
    argv[20] = launch->input;
    argv[21] = "--rsp";
    argv[22] = launch->rsp;
    argv[23] = "--transfer-storage";
    argv[24] = launch->transfer_dir;
    argv[25] = "--controller1";
    argv[26] = "keyboard";
    argv[27] = "--controller-map1";
    argv[28] = launch->controller_map;
    argv[29] = "--interactive";
    argv[30] = "--hotkeys";
    argv[31] = launch->hotkeys;
    static const char *modes[] = {"--controller2", "--controller3", "--controller4"};
    static const char *maps[] = {"--controller-map2", "--controller-map3", "--controller-map4"};
    unsigned count = 32;
    for (unsigned i = 0; i < 3; i++) {
        argv[count++] = modes[i];
        argv[count++] = "auto";
        if (launch->extra_controller_maps[i] && launch->extra_controller_maps[i][0]) {
            argv[count++] = maps[i];
            argv[count++] = launch->extra_controller_maps[i];
        }
    }
    argv[count] = NULL;
}

static void launch_log(const IntegralN64LocalRequest *request,
                       const char *event, const char *format, ...)
{
    if (!request->log) return;
    char detail[INTEGRAL_CONFIG_PATH_MAX + 256];
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);
    request->log(request->context, event, detail);
}

void integral_n64_local_run(const IntegralN64LocalRequest *request)
{
    const IntegralConfigRomSlot *n64_slot = request->slot;
    if (!n64_slot) {
        snprintf(request->status, request->status_size, "%s", "N64 ROM REQUIRED");
        return;
    }
    if (n64_slot->save_id[0] == '\0') {
        snprintf(request->status, request->status_size, "%s", "REGISTER N64 ROM FIRST");
        return;
    }

    IntegralN64LocalPaths paths;
    if (!request->paths(&paths)) {
        snprintf(request->status, request->status_size, "N64_RUNTIME NOT FOUND");
        return;
    }
    const char *save_ids[INTEGRAL_N64_RUNTIME_SYNC_SLOTS];
    unsigned save_id_count = 0;
    save_ids[save_id_count++] = n64_slot->save_id;
    for (unsigned i = 0; i < 4; i++) {
        const IntegralConfigRomSlot *selected = request->transfer[i];
        if (selected) {
            save_ids[save_id_count++] = selected->save_id;
        }
    }
    for (unsigned i = 0; i < save_id_count; i++) {
        if (!request->recover(request->context, save_ids[i])) {
            launch_log(request,
                       "n64_runtime_start_blocked",
                       "reason=outbox_recovery save_id=%s",
                       save_ids[i]);
            return;
        }
    }

    char error[160];
    char game_session_id[96];
    long long fencing_token = 0;
    if (integral_api_start_game_with_saves(request->server,
                                      request->token,
                                      INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT,
                                      save_ids,
                                      save_id_count,
                                      game_session_id,
                                      sizeof(game_session_id),
                                      &fencing_token,
                                      error,
                                      sizeof(error)) != 0) {
        launch_log(request,
                   "n64_runtime_lock_failed",
                   "execution_mode=%s save_count=%u error=%s",
                   INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT,
                   save_id_count,
                   error);
        snprintf(request->status, request->status_size, "N64_RUNTIME LOCK FAILED %s", error);
        return;
    }
    bool game_lock_acquired = true;
    if (!runtime_session_id_is_path_safe(game_session_id)) {
        (void)integral_api_stop_local_game(request->server, request->token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "N64_RUNTIME SESSION ID INVALID");
        return;
    }

    char integral_n64_runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_transfer_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_n64_save_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_config_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_screenshot_dir[INTEGRAL_CONFIG_PATH_MAX] = "screenshot";
    if (!format_runtime_session_path(integral_n64_runtime_dir, sizeof(integral_n64_runtime_dir),
                                     "runtime/n64_runtime", game_session_id, NULL) ||
        !format_runtime_session_path(integral_n64_runtime_transfer_dir, sizeof(integral_n64_runtime_transfer_dir),
                                     "runtime/n64_runtime", game_session_id, "transfer") ||
        !format_runtime_session_path(integral_n64_runtime_n64_save_dir, sizeof(integral_n64_runtime_n64_save_dir),
                                     "runtime/n64_runtime", game_session_id, "n64-save") ||
        !format_runtime_session_path(integral_n64_runtime_config_dir, sizeof(integral_n64_runtime_config_dir),
                                     "runtime/n64_runtime", game_session_id, "config")) {
        (void)integral_api_stop_local_game(request->server, request->token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "N64_RUNTIME SESSION PATH INVALID");
        return;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/n64_runtime") != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_transfer_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_n64_save_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_config_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_screenshot_dir) != 0) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "N64_RUNTIME RUNTIME CREATE FAILED");
        return;
    }

    LocalSyncSlot sync_slots[INTEGRAL_N64_RUNTIME_SYNC_SLOTS];
    memset(sync_slots, 0, sizeof(sync_slots));
    unsigned sync_count = 0;
    if (request->prepare_save(request->context, n64_slot, integral_n64_runtime_n64_save_dir, &sync_slots[sync_count]) != 0) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        return;
    }
    char n64_save_name[96];
    make_safe_n64_runtime_save_name(n64_slot->save_id, n64_save_name, sizeof(n64_save_name));
    sync_count++;
    for (unsigned i = 0; i < 4; i++) {
        const IntegralConfigRomSlot *selected = request->transfer[i];
        if (!selected) {
            continue;
        }
        IntegralConfigRomSlot slot = *selected;
        if (request->prepare_transfer(request->context, i, integral_n64_runtime_transfer_dir, &slot, &sync_slots[sync_count]) != 0) {
            integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
            return;
        }
        sync_count++;
    }

    char controller_map[512];
    if (!request->keymap(request->keys, controller_map, sizeof(controller_map))) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "N64 KEY CONFIG INVALID");
        return;
    }

    char extra_maps[3][512] = {{0}};
    for (unsigned i = 0; i < 3; i++) {
        if (request->extra_keys[i] && request->extra_keys[i][0] &&
            !request->keymap(request->extra_keys[i], extra_maps[i], sizeof(extra_maps[i]))) {
            integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
            snprintf(request->status, request->status_size, "N64 P%u KEY CONFIG INVALID", i + 2);
            return;
        }
    }
    const IntegralN64LocalLaunch launch = {
        .frontend = paths.frontend, .rom = n64_slot->rom_path,
        .core = paths.core, .config_dir = integral_n64_runtime_config_dir,
        .data = paths.data, .screenshot_dir = integral_n64_runtime_screenshot_dir,
        .save_dir = integral_n64_runtime_n64_save_dir, .save_name = n64_save_name,
        .video = paths.video, .audio = paths.audio, .input = paths.input, .rsp = paths.rsp,
        .transfer_dir = integral_n64_runtime_transfer_dir, .controller_map = controller_map, .hotkeys = request->hotkeys,
        .extra_controller_maps = {extra_maps[0], extra_maps[1], extra_maps[2]},
    };
    const char *runtime_argv[INTEGRAL_N64_LOCAL_ARGV_CAPACITY];
    integral_n64_local_arguments(&launch, runtime_argv);
#ifdef _WIN32
    intptr_t spawned = integral_windows_spawnv(_P_NOWAIT, paths.frontend, runtime_argv);
    if (spawned == -1) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "N64_RUNTIME START FAILED");
        launch_log(request, "n64_runtime_spawn_failed", "errno=%d", errno);
        return;
    }
    if (request->monitor_out) {
        HANDLE copy = NULL;
        if (DuplicateHandle(GetCurrentProcess(), (HANDLE)spawned, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS))
            *request->monitor_out = (IntegralChildProcess)copy;
    }
    start_save_sync_thread(spawned, request->server, request->token, game_session_id, fencing_token, sync_slots, sync_count, game_lock_acquired);
#else
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "N64_RUNTIME START FAILED");
        return;
    }
    if (monitor_pid == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            integral_api_stop_local_game(request->server, request->token, game_session_id, fencing_token, error, sizeof(error));
            _exit(127);
        }
        if (pid == 0) {
            request->redirect_output();
            execv(paths.frontend, (char *const *)runtime_argv);
            _exit(127);
        }
        monitor_save_sync_process(pid, request->server, request->token, game_session_id, fencing_token, sync_slots, sync_count, game_lock_acquired);
        _exit(0);
    }
    if (request->monitor_out) *request->monitor_out = monitor_pid;
#endif
    snprintf(request->status, request->status_size, "N64_RUNTIME STARTED TP:%u", sync_count - 1u);
    launch_log(request, "n64_runtime_started", "rom=%s transfer_slots=%u n64_save_id=%s",
               n64_slot->rom_path,
               sync_count - 1u,
               n64_slot->save_id);
}

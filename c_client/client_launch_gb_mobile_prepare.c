/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_gb_mobile.h"
#include "client_file_io.h"
#include "http_client.h"
#include "mobile_session_contract.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#ifndef X_OK
#define X_OK 0
#endif
#else
#include <unistd.h>
#endif

void integral_gb_mobile_run(const IntegralGbMobileRequest *request)
{
    const IntegralConfigRomSlot *selected = request->slot;
    if (access(request->runtime, X_OK) != 0) {
        snprintf(request->status, request->status_size, "%s", "GB MOBILE RUNTIME NOT FOUND");
        return;
    }
    if (!request->recover(request->context, selected->save_id)) {
        return;
    }

    char error[160];
    char mobile_session_id[96];
    char game_session_id[96];
    char create_request_id[160];
    char create_request_path[INTEGRAL_CONFIG_PATH_MAX];
    char safe_save_id[96];
    char safe_rom_id[96];
    char safe_scenario_id[96];
    make_safe_outbox_token(selected->save_id, safe_save_id, sizeof(safe_save_id));
    make_safe_outbox_token(selected->rom_id, safe_rom_id, sizeof(safe_rom_id));
    make_safe_outbox_token(request->scenario_id, safe_scenario_id, sizeof(safe_scenario_id));
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/gb-mobile-create") != 0) {
        snprintf(request->status, request->status_size, "%s", "MOBILE CREATE OUTBOX FAILED");
        return;
    }
    snprintf(create_request_path, sizeof(create_request_path),
             "runtime/gb-mobile-create/%s-%s-%s.txt",
             safe_save_id, safe_rom_id, safe_scenario_id);
    unsigned char *request_bytes = NULL;
    size_t request_size = 0;
    if (read_binary_file_alloc(create_request_path, &request_bytes, &request_size,
                               sizeof(create_request_id) - 1u) == 0 && request_size > 0u) {
        memcpy(create_request_id, request_bytes, request_size);
        create_request_id[request_size] = '\0';
        free(request_bytes);
    }
    else {
        free(request_bytes);
        snprintf(create_request_id, sizeof(create_request_id),
                 "mobile-create:%lld-%ld-%s",
                 (long long)time(NULL), (long)getpid(), safe_scenario_id);
        if (atomic_replace_binary_file(
                create_request_path,
                (const unsigned char *)create_request_id,
                strlen(create_request_id)) != 0) {
            snprintf(request->status, request->status_size, "%s", "MOBILE CREATE OUTBOX FAILED");
            return;
        }
    }
    IntegralMobileRuntimeContract runtime_contract;
    long long fencing_token = 0;
    int create_result = integral_api_start_mobile_session_contract(request->server,
                                          request->token,
                                          selected->save_id,
                                          selected->rom_id,
                                          create_request_id,
                                          request->scenario_id,
                                          mobile_session_id,
                                          sizeof(mobile_session_id),
                                          game_session_id,
                                          sizeof(game_session_id),
                                          &fencing_token,
                                          &runtime_contract,
                                          error,
                                          sizeof(error));
    if (create_result != 0) {
        if (create_result == INTEGRAL_API_MOBILE_CREATE_ABORTED) {
            remove(create_request_path);
        }
        if (create_result == INTEGRAL_API_MOBILE_CREATE_AUTH_SESSION_CONFLICT) {
            snprintf(request->status, request->status_size, "%s",
                      "MOBILE SESSION BELONGS TO ANOTHER LOGIN - WAIT FOR EXPIRY");
        }
        else if (create_result == INTEGRAL_API_MOBILE_CREATE_ABORTED) {
            snprintf(request->status, request->status_size, "%s",
                      "MOBILE CREATE ABORTED - RETRY START");
        }
        else {
            snprintf(request->status, request->status_size, "MOBILE LOCK FAILED %s", error);
        }
        return;
    }
    if (!runtime_session_id_is_path_safe(mobile_session_id) ||
        !runtime_session_id_is_path_safe(game_session_id)) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        (void)integral_api_cancel_mobile_session(request->server,
                                                 request->token,
                                                 mobile_session_id,
                                                 game_session_id,
                                                 fencing_token,
                                                 "invalid session identifier",
                                                 error,
                                                 sizeof(error));
        snprintf(request->status, request->status_size, "%s", "MOBILE SESSION ID INVALID");
        return;
    }
    if (remove(create_request_path) != 0 && errno != ENOENT) {
        char detail[INTEGRAL_CONFIG_PATH_MAX + 6];
        snprintf(detail, sizeof(detail), "path=%s", create_request_path);
        if (request->log) request->log(request->context, "mobile_create_outbox_cleanup_failed", detail);
    }

    char runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    char config_path[INTEGRAL_CONFIG_PATH_MAX];
    char result_path[INTEGRAL_CONFIG_PATH_MAX];
    char manifest_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(runtime_dir, sizeof(runtime_dir),
                                     "runtime/gb-mobile", mobile_session_id, NULL) ||
        !format_runtime_session_path(save_path, sizeof(save_path),
                                     "runtime/gb-mobile", mobile_session_id, "working.sav") ||
        !format_runtime_session_path(config_path, sizeof(config_path),
                                     "runtime/gb-mobile", mobile_session_id, "adapter.bin") ||
        !format_runtime_session_path(result_path, sizeof(result_path),
                                     "runtime/gb-mobile", mobile_session_id, "runtime-result.txt") ||
        !format_runtime_session_path(manifest_path, sizeof(manifest_path),
                                     "runtime/gb-mobile", mobile_session_id, "session.manifest")) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        (void)integral_api_cancel_mobile_session(request->server, request->token,
                                                 mobile_session_id, game_session_id, fencing_token,
                                                 "runtime path invalid", error, sizeof(error));
        snprintf(request->status, request->status_size, "%s", "MOBILE SESSION PATH INVALID");
        return;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/gb-mobile") != 0 ||
        ensure_private_runtime_directory(runtime_dir) != 0) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        integral_api_cancel_mobile_session(request->server, request->token, mobile_session_id, game_session_id, fencing_token, "runtime directory create failed", error, sizeof(error));
        (void)request->cleanup(runtime_dir);
        snprintf(request->status, request->status_size, "%s", "MOBILE RUNTIME CREATE FAILED");
        return;
    }

    IntegralConfigRomSlot slot = *selected;
    LocalSyncSlot sync_slot;
    memset(&sync_slot, 0, sizeof(sync_slot));
    if (request->download(request->context, &slot, save_path, &sync_slot) != 0 ||
        integral_mobile_runtime_contract_write_manifest(
            &runtime_contract, runtime_dir, manifest_path, error, sizeof(error)) != 0 ||
        strcmp(sync_slot.save_path, save_path) != 0) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        integral_api_cancel_mobile_session(request->server, request->token, mobile_session_id, game_session_id, fencing_token, "working SAV write failed", error, sizeof(error));
        (void)request->cleanup(runtime_dir);
        snprintf(request->status, request->status_size, "%s", "MOBILE SAV WRITE FAILED");
        return;
    }
    snprintf(sync_slot.mobile_session_id, sizeof(sync_slot.mobile_session_id), "%s", mobile_session_id);
    unsigned char *authoritative = NULL;
    size_t authoritative_size = 0;
    if (read_binary_file_alloc(save_path, &authoritative, &authoritative_size, INTEGRAL_MAX_SAVE_BYTES) != 0 ||
        authoritative_size == 0u) {
        free(authoritative);
        integral_mobile_runtime_contract_free(&runtime_contract);
        integral_api_cancel_mobile_session(request->server, request->token, mobile_session_id, game_session_id, fencing_token, "authoritative SAV invalid", error, sizeof(error));
        (void)request->cleanup(runtime_dir);
        snprintf(request->status, request->status_size, "%s", "MOBILE SAV SIZE INVALID");
        return;
    }
    sync_slot.mobile_guard = true;
    sync_slot.authoritative_size = authoritative_size;
    snprintf(sync_slot.mobile_runtime_dir, sizeof(sync_slot.mobile_runtime_dir), "%s", runtime_dir);
    snprintf(sync_slot.mobile_result_path, sizeof(sync_slot.mobile_result_path), "%s", result_path);
    free(authoritative);
    integral_mobile_runtime_contract_free(&runtime_contract);
    (void)remove(result_path);

    const char *runtime = request->runtime;
    unsigned gb_window_width = 0u, gb_window_height = 0u;
    char gb_window_width_text[16], gb_window_height_text[16];
    char rtc_offset_text[32];
    request->window_size(request->context, &gb_window_width, &gb_window_height);
    snprintf(gb_window_width_text, sizeof(gb_window_width_text), "%u", gb_window_width);
    snprintf(gb_window_height_text, sizeof(gb_window_height_text), "%u", gb_window_height);
    (void)request->rtc(request->context, rtc_offset_text, sizeof(rtc_offset_text));
    const IntegralGbMobileLaunch launch = {
        .runtime = runtime, .rom = slot.rom_path, .save = save_path,
        .adapter_config = config_path, .manifest = manifest_path, .result = result_path,
        .rtc_offset = rtc_offset_text,
        .window_width = gb_window_width_text, .window_height = gb_window_height_text,
        .keys = request->keys,
    };
    const IntegralGbMobileSession session = {
        .monitor_out = request->monitor_out,
        .server = request->server, .token = request->token,
        .mobile_session_id = mobile_session_id, .game_session_id = game_session_id,
        .fencing_token = fencing_token, .runtime_dir = runtime_dir,
        .scenario_id = request->scenario_id, .sync_slot = &sync_slot,
    };
    integral_gb_mobile_start(&launch, &session, request->status,
                             request->status_size, request->cleanup,
                             request->redirect_output, request->log, request->context);
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_save_sync.h"
#include "client_save_outbox.h"
#include "client_file_io.h"
#include "http_client.h"
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

#define INTEGRAL_LOCAL_GAME_HEARTBEAT_SECONDS 15
#define INTEGRAL_LOCAL_GAME_HEARTBEAT_FAILURE_LIMIT 3

#ifdef _WIN32
typedef struct SaveSyncMonitorArgs {
    intptr_t process_handle;
    char server[128];
    char token[160];
    char game_session_id[96];
    long long fencing_token;
    LocalSyncSlot sync_slots[INTEGRAL_N64_RUNTIME_SYNC_SLOTS];
    unsigned sync_count;
    bool game_session_active;
} SaveSyncMonitorArgs;
#endif

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}


int integral_save_download(const char *server, const char *token,
                            const IntegralConfigRomSlot *slot,
                            const char *session_save_path, LocalSyncSlot *sync_slot,
                            char *status, size_t status_size)
{
    unsigned char save_data[INTEGRAL_MAX_SAVE_BYTES];
    size_t save_size = 0;
    int revision = 0;
    char error[160];
    if (integral_api_download_save(server,
                              token,
                              slot->save_id,
                              save_data,
                              sizeof(save_data),
                              &save_size,
                              &revision,
                              error,
                              sizeof(error)) != 0) {
        snprintf(status, status_size, "SAV DOWNLOAD FAILED %s", error);
        return -1;
    }
    if (atomic_replace_binary_file(session_save_path, save_data, save_size) != 0 ||
        sha256_file_hex(session_save_path, sync_slot->last_hash, sizeof(sync_slot->last_hash)) != 0) {
        copy_text(status, status_size, "SESSION SAV WRITE FAILED");
        return -1;
    }
    copy_text(sync_slot->save_id, sizeof(sync_slot->save_id), slot->save_id);
    copy_text(sync_slot->save_path, sizeof(sync_slot->save_path), session_save_path);
    sync_slot->revision = revision;
    return 0;
}

bool execution_result_field(const char *result,
                          const char *key,
                          char *value_out,
                          size_t value_out_size)
{
    size_t key_size = strlen(key);
    const char *line = result;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t line_size = end ? (size_t)(end - line) : strlen(line);
        if (line_size > key_size + 1u && !memcmp(line, key, key_size) &&
            line[key_size] == '=' && line_size - key_size <= value_out_size) {
            size_t value_size = line_size - key_size - 1u;
            memcpy(value_out, line + key_size + 1u, value_size);
            value_out[value_size] = '\0';
            for (size_t i = 0; i < value_size; ++i) {
                char c = value_out[i];
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-' || c == '.')) {
                    return false;
                }
            }
            return true;
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}


bool cleanup_mobile_runtime_directory(const char *runtime_dir)
{
    static const char prefix[] = "runtime/gb-mobile/mobile_";
    if (!runtime_dir || strncmp(runtime_dir, prefix, sizeof(prefix) - 1u) != 0) {
        return false;
    }
    const char *id = runtime_dir + sizeof(prefix) - 1u;
    size_t id_length = strlen(id);
    if (id_length < 32u || id_length > 64u) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p) {
        if (!(isalnum(*p) || *p == '_' || *p == '-')) {
            return false;
        }
    }
    static const char *files[] = {
        "working.sav", "adapter.bin", "runtime-result.txt", "session.manifest",
    };
    char path[INTEGRAL_CONFIG_PATH_MAX];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", runtime_dir, files[i]);
        (void)remove(path);
    }
    for (unsigned i = 0; i < INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACTS; ++i) {
        snprintf(path, sizeof(path), "%s/artifact-%02u.bin", runtime_dir, i);
        (void)remove(path);
    }
#ifdef _WIN32
    return _rmdir(runtime_dir) == 0 || errno == ENOENT;
#else
    return rmdir(runtime_dir) == 0 || errno == ENOENT;
#endif
}


static bool complete_mobile_lifecycle(const char *server,
                                      const char *token,
                                      const char *game_session_id,
                                      long long fencing_token,
                                      const LocalSyncSlot *sync_slot)
{
    char error[160];
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        if (integral_api_complete_mobile_session(server, token,
                                                 sync_slot->mobile_session_id,
                                                 game_session_id, fencing_token,
                                                 error, sizeof(error)) == 0) {
            client_save_log("mobile_complete_ok", "mobile_session_id=%s attempt=%u", sync_slot->mobile_session_id, attempt);
            return true;
        }
        client_save_log("mobile_complete_retry", "mobile_session_id=%s attempt=%u error=%s", sync_slot->mobile_session_id, attempt, error);
    }
    return false;
}


bool mobile_runtime_result_allows_commit(const LocalSyncSlot *sync_slot,
                                                char *reason_out,
                                                size_t reason_out_size)
{
    char result[4096];
    size_t result_size = 0;
    if (!sync_slot || !sync_slot->mobile_result_path[0] ||
        read_binary_file(sync_slot->mobile_result_path,
                         (unsigned char *)result,
                         sizeof(result) - 1u,
                         &result_size) != 0) {
        copy_text(reason_out, reason_out_size, "runtime result missing");
        return false;
    }
    result[result_size] = '\0';

    char schema[8], clean[8], flushed[8], size_text[32], reported_hash[65];
    if (!execution_result_field(result, "schema_version", schema, sizeof(schema)) ||
        strcmp(schema, "1") != 0 ||
        !execution_result_field(result, "clean_exit", clean, sizeof(clean)) ||
        strcmp(clean, "1") != 0 ||
        !execution_result_field(result, "battery_flush", flushed, sizeof(flushed)) ||
        strcmp(flushed, "1") != 0) {
        copy_text(reason_out, reason_out_size, "runtime did not exit cleanly");
        return false;
    }
    if (!execution_result_field(result, "sav_size", size_text, sizeof(size_text)) ||
        !execution_result_field(result, "sav_sha256", reported_hash,
                                sizeof(reported_hash))) {
        copy_text(reason_out, reason_out_size, "runtime save receipt missing");
        return false;
    }

    errno = 0;
    char *size_end = NULL;
    unsigned long long reported_size = strtoull(size_text, &size_end, 10);
    if (errno != 0 || !size_end || *size_end != '\0' ||
        reported_size > INTEGRAL_MAX_SAVE_BYTES) {
        copy_text(reason_out, reason_out_size, "runtime save size invalid");
        return false;
    }

    unsigned char *save_data = NULL;
    size_t actual_size = 0;
    char actual_hash[65];
    if (read_binary_file_alloc(sync_slot->save_path, &save_data, &actual_size,
                               INTEGRAL_MAX_SAVE_BYTES) != 0 ||
        actual_size != (size_t)reported_size ||
        sha256_file_hex(sync_slot->save_path, actual_hash,
                        sizeof(actual_hash)) != 0 ||
        strlen(reported_hash) != 64u || strcmp(reported_hash, actual_hash) != 0) {
        free(save_data);
        copy_text(reason_out, reason_out_size, "runtime save receipt mismatch");
        return false;
    }
    free(save_data);
    reason_out[0] = '\0';
    return true;
}


bool upload_changed_save(const char *server, const char *token,
                         const char *game_session_id, long long fencing_token,
                         LocalSyncSlot *sync_slot, bool write_outbox_on_failure)
{
    if (process_save_inflight(server, token, game_session_id, fencing_token,
                              sync_slot, write_outbox_on_failure)) return true;
    if (!write_outbox_on_failure) return false;
    /* If staging failed, retain the working file through the ordinary Outbox. */
    sync_slot->preserve_save_path = write_save_recovery_pointer(server, sync_slot,
        sync_slot->save_path, "save_request_preservation_failed", game_session_id, fencing_token);
    return sync_slot->preserve_save_path;
}


static bool heartbeat_failure_is_terminal(const char *error)
{
    return error &&
           (strstr(error, "expired") != NULL ||
            strstr(error, "stale") != NULL ||
            strstr(error, "different active game session") != NULL);
}


static bool preserve_heartbeat_lost_recovery_set(const char *server,
                                                 const char *game_session_id,
                                                 long long fencing_token,
                                                 LocalSyncSlot *sync_slots,
                                                 unsigned sync_count)
{
    bool preserved = true;
    for (unsigned i = 0; i < sync_count; i++) {
        sync_slots[i].preserve_save_path =
            write_save_recovery_pointer(server,
                                        &sync_slots[i],
                                        sync_slots[i].save_path,
                                        "heartbeat_lost",
                                        game_session_id,
                                        fencing_token);
        if (!sync_slots[i].preserve_save_path) {
            preserved = false;
        }
    }
    return preserved;
}


static void reject_mobile_runtime_result(const char *server,
                                         const char *token,
                                         const char *game_session_id,
                                         long long fencing_token,
                                         LocalSyncSlot *sync_slot,
                                         const char *reason)
{
    sync_slot->preserve_save_path =
        write_save_recovery_pointer(server,
                                    sync_slot,
                                    sync_slot->save_path,
                                    "mobile_runtime_not_clean",
                                    game_session_id,
                                    fencing_token);
    char error[160];
    if (integral_api_cancel_mobile_session(server, token,
                                           sync_slot->mobile_session_id,
                                           game_session_id, fencing_token,
                                           reason, error, sizeof(error)) != 0) {
        client_save_log("mobile_cancel_failed",
                   "mobile_session_id=%s error=%s",
                   sync_slot->mobile_session_id, error);
    }
    client_save_log("mobile_result_rejected",
               "mobile_session_id=%s reason=%s recovery=%d",
               sync_slot->mobile_session_id, reason,
               sync_slot->preserve_save_path ? 1 : 0);
}


static void remove_reflected_session_saves(LocalSyncSlot *sync_slots, unsigned sync_count)
{
    static const char prefix[] = "runtime/";
    for (unsigned i = 0; i < sync_count; i++) {
        if (sync_slots[i].preserve_save_path ||
            strncmp(sync_slots[i].save_path, prefix, sizeof(prefix) - 1u) != 0) {
            continue;
        }
        if (remove(sync_slots[i].save_path) != 0 && errno != ENOENT) {
            client_save_log("session_save_cleanup_failed",
                       "save_id=%s path=%s errno=%d",
                       sync_slots[i].save_id,
                       sync_slots[i].save_path,
                       errno);
            continue;
        }
        char rtc_path[INTEGRAL_CONFIG_PATH_MAX];
        if (snprintf(rtc_path, sizeof(rtc_path), "%s.rtc", sync_slots[i].save_path) > 0) {
            (void)remove(rtc_path);
        }
        client_save_log("session_save_removed",
                   "save_id=%s path=%s",
                   sync_slots[i].save_id,
                   sync_slots[i].save_path);
    }
}


#ifndef _WIN32
static bool child_process_alive(IntegralChildProcess pid)
{
    int status = 0;
    pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
        return false;
    }
    if (waited == 0) {
        return true;
    }
    if (errno == ECHILD) {
        return !(kill(pid, 0) != 0 && errno == ESRCH);
    }
    return true;
}

void monitor_save_sync_process(IntegralChildProcess integral_gb_runtime_pid,
                                      const char *server,
                                      const char *token,
                                      const char *game_session_id,
                                      long long fencing_token,
                                      LocalSyncSlot *sync_slots,
                                      unsigned sync_count,
                                      bool game_session_active)
{
    unsigned heartbeat_seconds = 0;
    unsigned heartbeat_failures = 0;
    bool heartbeat_lost = false;
    while (true) {
        bool alive = child_process_alive(integral_gb_runtime_pid);
        if (!heartbeat_lost) {
            for (unsigned i = 0; i < sync_count; i++) {
                if (!sync_slots[i].mobile_guard) {
                    upload_changed_save(server, token, game_session_id,
                                        fencing_token, &sync_slots[i], false);
                }
            }
        }
        if (game_session_active && alive && !heartbeat_lost) {
            heartbeat_seconds++;
            if (heartbeat_seconds >= INTEGRAL_LOCAL_GAME_HEARTBEAT_SECONDS) {
                char error[160];
                int heartbeat_result =
                    sync_count == 1 && sync_slots[0].mobile_guard
                        ? integral_api_heartbeat_mobile_session(server,
                                                                token,
                                                                sync_slots[0].mobile_session_id,
                                                                game_session_id,
                                                                fencing_token,
                                                                error,
                                                                sizeof(error))
                        : integral_api_heartbeat_game(server,
                                                      token,
                                                      game_session_id,
                                                      fencing_token,
                                                      error,
                                                      sizeof(error));
                if (heartbeat_result != 0) {
                    heartbeat_failures++;
                    client_save_log("local_game_heartbeat_failed",
                               "game_session_id=%s failures=%u error=%s",
                               game_session_id,
                               heartbeat_failures,
                               error);
                    if (heartbeat_failure_is_terminal(error) ||
                        heartbeat_failures >= INTEGRAL_LOCAL_GAME_HEARTBEAT_FAILURE_LIMIT) {
                        heartbeat_lost = true;
                        (void)kill(integral_gb_runtime_pid, SIGTERM);
                        client_save_log("local_emulator_stop_on_heartbeat_loss",
                                   "pid=%ld game_session_id=%s",
                                   (long)integral_gb_runtime_pid,
                                   game_session_id);
                    }
                }
                else {
                    heartbeat_failures = 0;
                }
                heartbeat_seconds = 0;
            }
        }
        if (!alive) {
            client_save_log("local_emulator_exited_monitor",
                       "pid=%ld game_session_id=%s",
                       (long)integral_gb_runtime_pid,
                       game_session_id);
            if (heartbeat_lost) {
                (void)preserve_heartbeat_lost_recovery_set(server,
                                                           game_session_id,
                                                           fencing_token,
                                                           sync_slots,
                                                           sync_count);
                break;
            }
            bool safe_to_release = true;
            bool mobile_session = sync_count == 1 && sync_slots[0].mobile_guard;
            char mobile_result_reason[160];
            if (mobile_session &&
                !mobile_runtime_result_allows_commit(&sync_slots[0],
                                                     mobile_result_reason,
                                                     sizeof(mobile_result_reason))) {
                reject_mobile_runtime_result(server, token, game_session_id,
                                             fencing_token, &sync_slots[0],
                                             mobile_result_reason);
                break;
            }
            for (unsigned i = 0; i < sync_count; i++) {
                if (!upload_changed_save(server, token, game_session_id, fencing_token, &sync_slots[i], true)) {
                    safe_to_release = false;
                }
            }
            if (game_session_active && safe_to_release && mobile_session) {
                safe_to_release = complete_mobile_lifecycle(server, token,
                                                            game_session_id,
                                                            fencing_token,
                                                            &sync_slots[0]);
                if (safe_to_release && !sync_slots[0].preserve_save_path) {
                    (void)cleanup_mobile_runtime_directory(sync_slots[0].mobile_runtime_dir);
                }
            }
            else if (game_session_active && safe_to_release) {
                char error[160];
                if (integral_api_stop_local_game(server, token, game_session_id, fencing_token, error, sizeof(error)) != 0) {
                    client_save_log("local_game_stop_failed",
                               "game_session_id=%s error=%s",
                               game_session_id,
                               error);
                }
                else {
                    client_save_log("local_game_stop_ok",
                               "game_session_id=%s",
                               game_session_id);
                    remove_reflected_session_saves(sync_slots, sync_count);
                }
            }
            else if (game_session_active && !safe_to_release) {
                client_save_log("game_stop_withheld_for_recovery",
                           "game_session_id=%s",
                           game_session_id);
            }
            break;
        }
        sleep(1);
    }
}
#endif

#ifdef _WIN32
static DWORD WINAPI monitor_save_sync_thread(LPVOID param)
{
    SaveSyncMonitorArgs *args = (SaveSyncMonitorArgs *)param;
    HANDLE process = (HANDLE)args->process_handle;
    unsigned heartbeat_ticks = 0;
    unsigned heartbeat_failures = 0;
    bool heartbeat_lost = false;
    while (true) {
        DWORD wait_result = WaitForSingleObject(process, 1000);
        if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED) {
            DWORD exit_code = STILL_ACTIVE;
            BOOL has_exit_code = GetExitCodeProcess(process, &exit_code);
            client_save_log("local_emulator_exited_monitor",
                       "game_session_id=%s wait_result=%lu exit_code=%lu exit_code_known=%d",
                       args->game_session_id,
                       (unsigned long)wait_result,
                       (unsigned long)exit_code,
                       has_exit_code ? 1 : 0);
            if (heartbeat_lost) {
                (void)preserve_heartbeat_lost_recovery_set(args->server,
                                                           args->game_session_id,
                                                           args->fencing_token,
                                                           args->sync_slots,
                                                           args->sync_count);
                break;
            }
            bool safe_to_release = true;
            bool mobile_session = args->sync_count == 1 && args->sync_slots[0].mobile_guard;
            char mobile_result_reason[160];
            if (mobile_session &&
                !mobile_runtime_result_allows_commit(&args->sync_slots[0],
                                                     mobile_result_reason,
                                                     sizeof(mobile_result_reason))) {
                reject_mobile_runtime_result(args->server, args->token,
                                             args->game_session_id,
                                             args->fencing_token,
                                             &args->sync_slots[0],
                                             mobile_result_reason);
                break;
            }
            for (unsigned i = 0; i < args->sync_count; i++) {
                if (!upload_changed_save(args->server, args->token, args->game_session_id, args->fencing_token, &args->sync_slots[i], true)) {
                    safe_to_release = false;
                }
            }
            if (args->game_session_active && safe_to_release && mobile_session) {
                safe_to_release = complete_mobile_lifecycle(args->server,
                                                            args->token,
                                                            args->game_session_id,
                                                            args->fencing_token,
                                                            &args->sync_slots[0]);
                if (safe_to_release && !args->sync_slots[0].preserve_save_path) {
                    (void)cleanup_mobile_runtime_directory(args->sync_slots[0].mobile_runtime_dir);
                }
            }
            else if (args->game_session_active && safe_to_release) {
                char error[160];
                if (integral_api_stop_local_game(args->server, args->token, args->game_session_id, args->fencing_token, error, sizeof(error)) != 0) {
                    client_save_log("local_game_stop_failed",
                               "game_session_id=%s error=%s",
                               args->game_session_id,
                               error);
                }
                else {
                    client_save_log("local_game_stop_ok",
                               "game_session_id=%s",
                               args->game_session_id);
                    remove_reflected_session_saves(args->sync_slots, args->sync_count);
                }
            }
            else if (args->game_session_active && !safe_to_release) {
                client_save_log("game_stop_withheld_for_recovery",
                           "game_session_id=%s",
                           args->game_session_id);
            }
            break;
        }
        if (!heartbeat_lost) {
            for (unsigned i = 0; i < args->sync_count; i++) {
                if (!args->sync_slots[i].mobile_guard) {
                    upload_changed_save(args->server, args->token,
                                        args->game_session_id,
                                        args->fencing_token,
                                        &args->sync_slots[i], false);
                }
            }
        }
        if (args->game_session_active && !heartbeat_lost) {
            heartbeat_ticks++;
            if (heartbeat_ticks >= INTEGRAL_LOCAL_GAME_HEARTBEAT_SECONDS) {
                char error[160];
                int heartbeat_result =
                    args->sync_count == 1 && args->sync_slots[0].mobile_guard
                        ? integral_api_heartbeat_mobile_session(args->server,
                                                                args->token,
                                                                args->sync_slots[0].mobile_session_id,
                                                                args->game_session_id,
                                                                args->fencing_token,
                                                                error,
                                                                sizeof(error))
                        : integral_api_heartbeat_game(args->server,
                                                      args->token,
                                                      args->game_session_id,
                                                      args->fencing_token,
                                                      error,
                                                      sizeof(error));
                if (heartbeat_result != 0) {
                    heartbeat_failures++;
                    client_save_log("local_game_heartbeat_failed",
                               "game_session_id=%s failures=%u error=%s",
                               args->game_session_id,
                               heartbeat_failures,
                               error);
                    if (heartbeat_failure_is_terminal(error) ||
                        heartbeat_failures >= INTEGRAL_LOCAL_GAME_HEARTBEAT_FAILURE_LIMIT) {
                        heartbeat_lost = true;
                        (void)TerminateProcess(process, 1);
                        client_save_log("local_emulator_stop_on_heartbeat_loss",
                                   "game_session_id=%s",
                                   args->game_session_id);
                    }
                }
                else {
                    heartbeat_failures = 0;
                }
                heartbeat_ticks = 0;
            }
        }
    }
    CloseHandle(process);
    free(args);
    return 0;
}

void start_save_sync_thread(intptr_t process_handle,
                                   const char *server,
                                   const char *token,
                                   const char *game_session_id,
                                   long long fencing_token,
                                   const LocalSyncSlot *sync_slots,
                                   unsigned sync_count,
                                   bool game_session_active)
{
    SaveSyncMonitorArgs *args = calloc(1, sizeof(*args));
    if (!args) {
        CloseHandle((HANDLE)process_handle);
        return;
    }
    args->process_handle = process_handle;
    copy_text(args->server, sizeof(args->server), server);
    copy_text(args->token, sizeof(args->token), token);
    copy_text(args->game_session_id, sizeof(args->game_session_id), game_session_id);
    args->fencing_token = fencing_token;
    args->sync_count = sync_count > INTEGRAL_N64_RUNTIME_SYNC_SLOTS ? INTEGRAL_N64_RUNTIME_SYNC_SLOTS : sync_count;
    args->game_session_active = game_session_active;
    for (unsigned i = 0; i < args->sync_count; i++) {
        args->sync_slots[i] = sync_slots[i];
    }
    HANDLE thread = CreateThread(NULL, 0, monitor_save_sync_thread, args, 0, NULL);
    if (!thread) {
        CloseHandle((HANDLE)process_handle);
        free(args);
        return;
    }
    CloseHandle(thread);
}
#endif

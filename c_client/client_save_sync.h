/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_SAVE_SYNC_H
#define INTEGRAL_CLIENT_SAVE_SYNC_H
#include "client_config.h"
#include <stdbool.h>
#include <stdint.h>
#ifndef _WIN32
#include <sys/types.h>
typedef pid_t IntegralChildProcess;
#else
typedef intptr_t IntegralChildProcess;
#endif

typedef struct LocalSyncSlot {
    char account[64];
    char save_id[96];
    char mobile_session_id[96];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    char last_hash[65];
    int revision;
    bool mobile_guard;
    bool preserve_save_path;
    size_t authoritative_size;
    char mobile_runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char mobile_result_path[INTEGRAL_CONFIG_PATH_MAX];
} LocalSyncSlot;

/* Implementations live in client_save_sync.c.
 * POSIX: called in the monitor child with fork-owned data; blocks until done.
 * Windows: copies all supplied data before returning; takes handle ownership. */
void monitor_save_sync_process(IntegralChildProcess pid, const char *server,
                              const char *token, const char *game_session_id,
                              long long fencing_token, LocalSyncSlot *slots,
                              unsigned count, bool game_session_active);
#ifdef _WIN32
void start_save_sync_thread(intptr_t process_handle, const char *server,
                            const char *token, const char *game_session_id,
                            long long fencing_token, const LocalSyncSlot *slots,
                            unsigned count, bool game_session_active);
#endif
/* Existing log sink, retaining the former NULL-AppState metadata. */
void client_save_log(const char *event, const char *format, ...);
#define INTEGRAL_N64_RUNTIME_SYNC_SLOTS 5
/* Caller resolves pending outbox entries before downloading. */
int integral_save_download(const char *server, const char *token,
                            const IntegralConfigRomSlot *slot,
                            const char *session_save_path, LocalSyncSlot *sync_slot,
                            char *status, size_t status_size);
bool upload_changed_save(const char *, const char *, const char *, long long,
                          LocalSyncSlot *, bool);
bool cleanup_mobile_runtime_directory(const char *);
bool execution_result_field(const char *, const char *, char *, size_t);
bool mobile_runtime_result_allows_commit(const LocalSyncSlot *, char *, size_t);
#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_LAUNCH_GB_MOBILE_H
#define INTEGRAL_CLIENT_LAUNCH_GB_MOBILE_H
#include "client_config.h"
#include "client_save_sync.h"

typedef struct IntegralGbMobileLaunch {
    const char *runtime;
    const char *rom;
    const char *save;
    const char *adapter_config;
    const char *manifest;
    const char *result;
    const char *rtc_offset;
    const char *window_width;
    const char *window_height;
    const IntegralConfigKeys *keys;
} IntegralGbMobileLaunch;

/* argv[0], fourteen option/value pairs and NULL. Strings remain caller-owned.
 * No quoting, truncation, file access, credentials or process operations. */
#define INTEGRAL_GB_MOBILE_ARGV_CAPACITY 30
void integral_gb_mobile_arguments(const IntegralGbMobileLaunch *launch,
                                 const char *argv[INTEGRAL_GB_MOBILE_ARGV_CAPACITY]);
typedef struct IntegralGbMobileSession {
    IntegralChildProcess *monitor_out;
    const char *server, *token;
    const char *mobile_session_id, *game_session_id;
    long long fencing_token;
    const char *runtime_dir, *scenario_id;
    LocalSyncSlot *sync_slot;
} IntegralGbMobileSession;

/* Prepared caller-owned data: Windows monitoring copies it before return;
 * POSIX monitoring keeps the existing forked copy. No callback is retained. */
void integral_gb_mobile_start(const IntegralGbMobileLaunch *launch,
                              const IntegralGbMobileSession *session,
                              char *status, size_t status_size,
                              bool (*cleanup)(const char *),
                              void (*redirect_output)(void),
                              void (*log)(void *, const char *, const char *),
                              void *log_context);
/* Selected scenario is resolved by the UI before this synchronous preparation.
 * SAV operations still belong to the existing monitor/outbox implementation. */
typedef struct IntegralGbMobileRequest {
    IntegralChildProcess *monitor_out;
    const IntegralConfigRomSlot *slot;
    const char *scenario_id, *runtime, *server, *token;
    const IntegralConfigKeys *keys;
    char *status;
    size_t status_size;
    void *context;
    bool (*recover)(void *, const char *);
    int (*download)(void *, const IntegralConfigRomSlot *, const char *, LocalSyncSlot *);
    bool (*rtc)(void *, char *, size_t);
    void (*window_size)(void *, unsigned *, unsigned *);
    bool (*cleanup)(const char *);
    void (*redirect_output)(void);
    void (*log)(void *, const char *, const char *);
} IntegralGbMobileRequest;
void integral_gb_mobile_run(const IntegralGbMobileRequest *request);
#endif

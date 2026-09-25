/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_LAUNCH_GB_LOCAL_H
#define INTEGRAL_CLIENT_LAUNCH_GB_LOCAL_H

#include "client_config.h"
#include "client_save_sync.h"

/* Logical initial client area; rendering keeps its own integer pixel scale. */
void integral_gb_local_pair_window_size(unsigned client_width, unsigned client_height,
                                       unsigned available_width, unsigned available_height,
                                       unsigned *width, unsigned *height);

typedef struct IntegralGbLocalLaunch {
    const char *runtime;
    const char *rom1;
    const char *save1;
    const char *rom2; /* NULL for one-screen LOCAL. */
    const char *save2;
    const char *port;
    const char *rtc_offset;
    const char *window_width;
    const char *window_height;
    const char *ir_off_delay_ticks;
    const char *sgb;
    const IntegralConfigKeys *keys;
} IntegralGbLocalLaunch;

/* Includes argv[0] and the terminating NULL. Strings remain caller-owned.
 * No allocation, quoting, path normalization or process/API operations. */
#define INTEGRAL_GB_LOCAL_ARGV_CAPACITY 44
void integral_gb_local_arguments(const IntegralGbLocalLaunch *launch,
                                const char *argv[INTEGRAL_GB_LOCAL_ARGV_CAPACITY]);

typedef struct IntegralGbLocalSession {
    const char *server;
    const char *token;
    const char *game_session_id;
    long long fencing_token;
    LocalSyncSlot *slots;
    unsigned count;
} IntegralGbLocalSession;

/* Prepared SAV/session data remain caller-owned. Windows monitoring copies
 * them synchronously; POSIX monitoring uses the existing forked copy.
 * log is a synchronous UI-side observer, not retained by the monitor. */
void integral_gb_local_start(const IntegralGbLocalLaunch *launch,
                             const IntegralGbLocalSession *session,
                             char *status, size_t status_size,
                             void (*log)(void *, const char *, const char *),
                             void *log_context);

/* Synchronous boundary to existing SAV/outbox/RTC and SDL operations.
 * Callbacks do not retain the request or introduce persistent state. */
typedef struct IntegralGbLocalRequest {
    const IntegralConfigRomSlot *slot1, *slot2;
    const char *runtime, *port, *server, *token;
    const char *config_path;
    const IntegralConfigKeys *keys;
    char *status;
    size_t status_size;
    void *context;
    bool (*recover)(void *, const char *);
    int (*download)(void *, const IntegralConfigRomSlot *, const char *, LocalSyncSlot *);
    bool (*rtc)(void *, char *, size_t);
    void (*window_size)(void *, unsigned *, unsigned *);
    void (*log)(void *, const char *, const char *);
} IntegralGbLocalRequest;
void integral_gb_local_run(const IntegralGbLocalRequest *request);

#endif

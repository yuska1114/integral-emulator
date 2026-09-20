/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_LAUNCH_N64_LOCAL_H
#define INTEGRAL_CLIENT_LAUNCH_N64_LOCAL_H
#include "client_config.h"
#include "client_save_sync.h"

typedef struct IntegralN64LocalPaths {
    char frontend[INTEGRAL_CONFIG_PATH_MAX], core[INTEGRAL_CONFIG_PATH_MAX];
    char video[INTEGRAL_CONFIG_PATH_MAX], audio[INTEGRAL_CONFIG_PATH_MAX];
    char input[INTEGRAL_CONFIG_PATH_MAX], rsp[INTEGRAL_CONFIG_PATH_MAX];
    char data[INTEGRAL_CONFIG_PATH_MAX];
} IntegralN64LocalPaths;

typedef struct IntegralN64LocalLaunch {
    const char *frontend, *rom, *core, *config_dir, *data, *screenshot_dir;
    const char *save_dir, *save_name, *video, *audio, *input, *rsp;
    const char *transfer_dir, *controller_map, *hotkeys;
    const char *extra_controller_maps[3];
} IntegralN64LocalLaunch;
#define INTEGRAL_N64_LOCAL_ARGV_CAPACITY 45
/* Identical Windows/POSIX argv, caller-owned strings and terminating NULL. */
void integral_n64_local_arguments(const IntegralN64LocalLaunch *,
                                  const char *argv[INTEGRAL_N64_LOCAL_ARGV_CAPACITY]);

/* Synchronous preparation and launch. UI selection is already refreshed.
 * SAV/SDL adapters retain existing behavior; monitoring owns its OS-specific copy. */
typedef struct IntegralN64LocalRequest {
    IntegralChildProcess *monitor_out;
    const IntegralConfigRomSlot *slot, *transfer[4];
    const char *server, *token, *keys, *hotkeys;
    const char *extra_keys[3];
    char *status;
    size_t status_size;
    void *context;
    bool (*paths)(IntegralN64LocalPaths *);
    bool (*recover)(void *, const char *);
    int (*prepare_save)(void *, const IntegralConfigRomSlot *, const char *, LocalSyncSlot *);
    int (*prepare_transfer)(void *, unsigned, const char *, IntegralConfigRomSlot *, LocalSyncSlot *);
    bool (*keymap)(const char *, char *, size_t);
    void (*redirect_output)(void);
    void (*log)(void *, const char *, const char *);
} IntegralN64LocalRequest;
void integral_n64_local_run(const IntegralN64LocalRequest *);
#endif

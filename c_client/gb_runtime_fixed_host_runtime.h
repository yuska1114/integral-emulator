/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_H

#include "../runtimes/gb/src/server/gb_link_engine.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct IntegralGBRuntimeFixedHostRuntimeConfig {
    const char *host_rom_path;
    const char *remote_rom_path;
    uint8_t *host_save;
    size_t host_save_size;
    uint8_t *remote_save;
    size_t remote_save_size;
    int64_t rtc_offset_seconds;
    uint64_t rtc_target_unix;
    unsigned ir_off_delay_ticks;
    bool preserve_both_audio;
} IntegralGBRuntimeFixedHostRuntimeConfig;

typedef struct IntegralGBRuntimeFixedHostRuntime {
    IntegralGBRuntimeLinkEngine engine;
    bool initialized;
} IntegralGBRuntimeFixedHostRuntime;

/* SAV buffers are borrowed, writable, and always zeroized before return. */
int integral_gb_runtime_fixed_host_runtime_init(
    IntegralGBRuntimeFixedHostRuntime *runtime,
    IntegralGBRuntimeFixedHostRuntimeConfig *config);

int integral_gb_runtime_fixed_host_runtime_run_frame(
    IntegralGBRuntimeFixedHostRuntime *runtime,
    uint8_t host_buttons,
    uint8_t remote_buttons);

void integral_gb_runtime_fixed_host_runtime_free(IntegralGBRuntimeFixedHostRuntime *runtime);

#endif

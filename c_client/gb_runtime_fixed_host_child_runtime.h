/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_CHILD_RUNTIME_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_CHILD_RUNTIME_H

#include "gb_runtime_fixed_host_product_runtime.h"
#include "gb_runtime_fixed_host_runtime.h"
#include "gb_runtime_fixed_host_snapshot_ipc.h"

typedef struct IntegralGBRuntimeFixedHostChildConfig {
    const char *host_rom_path;
    const char *remote_rom_path;
    IntegralGBRuntimeFixedHostProductRole role;
    int64_t rtc_offset_seconds;
    uint64_t rtc_target_unix;
} IntegralGBRuntimeFixedHostChildConfig;

typedef struct IntegralGBRuntimeFixedHostChildRuntime {
    IntegralGBRuntimeFixedHostRuntime core;
    IntegralGBRuntimeFixedHostProductRuntime product;
    bool initialized;
} IntegralGBRuntimeFixedHostChildRuntime;

/* Child-side entry point: SAV contents arrive only through reader/context. */
bool integral_gb_runtime_fixed_host_child_runtime_start(
    IntegralGBRuntimeFixedHostChildRuntime *runtime,
    const IntegralGBRuntimeFixedHostChildConfig *config,
    IntegralGBRuntimeFixedHostIPCRead reader,
    void *context);
int integral_gb_runtime_fixed_host_child_runtime_frame(
    IntegralGBRuntimeFixedHostChildRuntime *runtime,
    uint8_t host_buttons,
    uint8_t remote_buttons);
void integral_gb_runtime_fixed_host_child_runtime_stop(
    IntegralGBRuntimeFixedHostChildRuntime *runtime);

#endif

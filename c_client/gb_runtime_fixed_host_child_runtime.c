/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_child_runtime.h"

#include "../runtimes/gb/src/server/secure_memory.h"

#include <string.h>

bool integral_gb_runtime_fixed_host_child_runtime_start(
    IntegralGBRuntimeFixedHostChildRuntime *runtime,
    const IntegralGBRuntimeFixedHostChildConfig *config,
    IntegralGBRuntimeFixedHostIPCRead reader,
    void *context)
{
    IntegralGBRuntimeFixedHostSnapshotPair pair;
    IntegralGBRuntimeFixedHostRuntimeConfig core_config;
    bool success = false;
    if (runtime == NULL) return false;
    memset(runtime, 0, sizeof(*runtime));
    memset(&pair, 0, sizeof(pair));
    if (config == NULL || reader == NULL || config->host_rom_path == NULL ||
        config->remote_rom_path == NULL ||
        !integral_gb_runtime_fixed_host_snapshot_ipc_receive(reader, context, &pair))
        goto done;
    core_config = (IntegralGBRuntimeFixedHostRuntimeConfig){
        .host_rom_path = config->host_rom_path,
        .remote_rom_path = config->remote_rom_path,
        .host_save = pair.host_data,
        .host_save_size = pair.host_size,
        .remote_save = pair.remote_data,
        .remote_save_size = pair.remote_size,
        .rtc_offset_seconds = config->rtc_offset_seconds,
        .rtc_target_unix = config->rtc_target_unix,
        .preserve_both_audio = true,
    };
    if (integral_gb_runtime_fixed_host_runtime_init(&runtime->core, &core_config) != 0 ||
        !integral_gb_runtime_fixed_host_product_runtime_init(
            &runtime->product, config->role)) goto done;
    runtime->initialized = true;
    success = true;
done:
    integral_gb_runtime_fixed_host_snapshot_pair_release(&pair);
    if (!success) integral_gb_runtime_fixed_host_child_runtime_stop(runtime);
    integral_gb_runtime_secure_zero(&core_config, sizeof(core_config));
    return success;
}

int integral_gb_runtime_fixed_host_child_runtime_frame(
    IntegralGBRuntimeFixedHostChildRuntime *runtime,
    uint8_t host_buttons,
    uint8_t remote_buttons)
{
    if (runtime == NULL || !runtime->initialized) return -1;
    return integral_gb_runtime_fixed_host_runtime_run_frame(
        &runtime->core, host_buttons, remote_buttons);
}

void integral_gb_runtime_fixed_host_child_runtime_stop(
    IntegralGBRuntimeFixedHostChildRuntime *runtime)
{
    if (runtime == NULL) return;
    integral_gb_runtime_fixed_host_runtime_free(&runtime->core);
    integral_gb_runtime_fixed_host_product_runtime_stop(&runtime->product);
    integral_gb_runtime_secure_zero(runtime, sizeof(*runtime));
}

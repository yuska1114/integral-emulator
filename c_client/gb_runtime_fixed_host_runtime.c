/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_runtime.h"

#include "../runtimes/gb/src/server/secure_memory.h"

#include <string.h>

static void zero_input_saves(IntegralGBRuntimeFixedHostRuntimeConfig *config)
{
    if (!config) return;
    if (config->host_save && config->host_save_size) {
        integral_gb_runtime_secure_zero(config->host_save, config->host_save_size);
    }
    if (config->remote_save && config->remote_save_size) {
        integral_gb_runtime_secure_zero(config->remote_save, config->remote_save_size);
    }
}

int integral_gb_runtime_fixed_host_runtime_init(
    IntegralGBRuntimeFixedHostRuntime *runtime,
    IntegralGBRuntimeFixedHostRuntimeConfig *config)
{
    int result;
    if (!runtime || !config) {
        zero_input_saves(config);
        return -1;
    }
    memset(runtime, 0, sizeof(*runtime));
    if (!config->host_rom_path || !config->remote_rom_path ||
        (config->host_save_size && !config->host_save) ||
        (config->remote_save_size && !config->remote_save)) {
        zero_input_saves(config);
        return -1;
    }
    IntegralGBRuntimeLinkEngineConfig engine_config = {
        .rom_a = config->host_rom_path,
        .rom_b = config->remote_rom_path,
        .save_a = config->host_save,
        .save_a_size = config->host_save_size,
        .save_b = config->remote_save,
        .save_b_size = config->remote_save_size,
        .display_role = 0,
        .rtc_offset_seconds = config->rtc_offset_seconds,
        .rtc_target_unix = config->rtc_target_unix,
        .ir_off_delay_ticks = config->ir_off_delay_ticks,
        .preserve_both_audio = config->preserve_both_audio,
    };
    result = integral_gb_runtime_link_engine_init(&runtime->engine, &engine_config);
    zero_input_saves(config);
    if (result != 0) {
        integral_gb_runtime_link_engine_free(&runtime->engine);
        memset(runtime, 0, sizeof(*runtime));
        return -1;
    }
    runtime->initialized = true;
    return 0;
}

int integral_gb_runtime_fixed_host_runtime_run_frame(
    IntegralGBRuntimeFixedHostRuntime *runtime,
    uint8_t host_buttons,
    uint8_t remote_buttons)
{
    if (!runtime || !runtime->initialized) return -1;
    return integral_gb_runtime_link_engine_run_frame(
        &runtime->engine, host_buttons, remote_buttons);
}

void integral_gb_runtime_fixed_host_runtime_free(IntegralGBRuntimeFixedHostRuntime *runtime)
{
    if (!runtime) return;
    integral_gb_runtime_link_engine_free(&runtime->engine);
    memset(runtime, 0, sizeof(*runtime));
}

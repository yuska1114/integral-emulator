/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_SCHEDULER_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_SCHEDULER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_FIXED_HOST_MACRO_MAX 512u

typedef struct IntegralGBRuntimeFixedHostScheduler {
    char macro_host[INTEGRAL_GB_RUNTIME_FIXED_HOST_MACRO_MAX + 1u];
    char macro_remote[INTEGRAL_GB_RUNTIME_FIXED_HOST_MACRO_MAX + 1u];
    size_t host_index;
    size_t remote_index;
    unsigned step_frame;
    unsigned press_frames;
    unsigned step_frames;
    bool macro_running;
    bool paused;
} IntegralGBRuntimeFixedHostScheduler;

bool integral_gb_runtime_fixed_host_scheduler_init(
    IntegralGBRuntimeFixedHostScheduler *scheduler,
    const char *host_macro,
    const char *remote_macro,
    unsigned press_frames,
    unsigned step_frames);
void integral_gb_runtime_fixed_host_scheduler_start_test_automation(
    IntegralGBRuntimeFixedHostScheduler *scheduler);
void integral_gb_runtime_fixed_host_scheduler_set_paused(
    IntegralGBRuntimeFixedHostScheduler *scheduler, bool paused);
bool integral_gb_runtime_fixed_host_scheduler_frame(
    IntegralGBRuntimeFixedHostScheduler *scheduler,
    uint8_t manual_host,
    uint8_t manual_remote,
    uint8_t *host_buttons,
    uint8_t *remote_buttons);
void integral_gb_runtime_fixed_host_scheduler_stop(IntegralGBRuntimeFixedHostScheduler *scheduler);

#endif

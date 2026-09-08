/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_scheduler.h"

#include "../runtimes/gb/src/server/secure_memory.h"

#include <string.h>

static uint8_t macro_button(char token)
{
    switch (token) {
        case 'A': return 0x10u;
        case 'B': return 0x20u;
        case 'S': return 0x80u;
        case 's': return 0x40u;
        case 'R': return 0x01u;
        case 'L': return 0x02u;
        case 'U': return 0x04u;
        case 'D': return 0x08u;
        case '.': case '|': case '\0': return 0u;
        default: return 0xffu;
    }
}

static bool valid_macro(const char *macro)
{
    size_t length;
    if (macro == NULL) return false;
    length = strlen(macro);
    if (length == 0u || length > INTEGRAL_GB_RUNTIME_FIXED_HOST_MACRO_MAX) return false;
    for (size_t index = 0u; index < length; index++)
        if (macro_button(macro[index]) == 0xffu) return false;
    return true;
}

bool integral_gb_runtime_fixed_host_scheduler_init(
    IntegralGBRuntimeFixedHostScheduler *scheduler,
    const char *host_macro,
    const char *remote_macro,
    unsigned press_frames,
    unsigned step_frames)
{
    if (scheduler == NULL || !valid_macro(host_macro) || !valid_macro(remote_macro) ||
        press_frames == 0u || step_frames < press_frames) return false;
    memset(scheduler, 0, sizeof(*scheduler));
    memcpy(scheduler->macro_host, host_macro, strlen(host_macro) + 1u);
    memcpy(scheduler->macro_remote, remote_macro, strlen(remote_macro) + 1u);
    scheduler->press_frames = press_frames;
    scheduler->step_frames = step_frames;
    return true;
}

void integral_gb_runtime_fixed_host_scheduler_start_test_automation(
    IntegralGBRuntimeFixedHostScheduler *scheduler)
{
    if (scheduler == NULL || scheduler->macro_running) return;
    scheduler->macro_running = true;
    scheduler->host_index = scheduler->remote_index = 0u;
    scheduler->step_frame = 0u;
}

void integral_gb_runtime_fixed_host_scheduler_set_paused(
    IntegralGBRuntimeFixedHostScheduler *scheduler, bool paused)
{
    if (scheduler != NULL) scheduler->paused = paused;
}

static void advance_at_barrier(IntegralGBRuntimeFixedHostScheduler *scheduler)
{
    if (scheduler->macro_host[scheduler->host_index] == '|' &&
        scheduler->macro_remote[scheduler->remote_index] == '|') {
        scheduler->host_index++;
        scheduler->remote_index++;
    }
}

bool integral_gb_runtime_fixed_host_scheduler_frame(
    IntegralGBRuntimeFixedHostScheduler *scheduler,
    uint8_t manual_host,
    uint8_t manual_remote,
    uint8_t *host_buttons,
    uint8_t *remote_buttons)
{
    char host_token, remote_token;
    if (scheduler == NULL || host_buttons == NULL || remote_buttons == NULL) return false;
    *host_buttons = *remote_buttons = 0u;
    if (scheduler->paused) return true;
    if (!scheduler->macro_running) {
        *host_buttons = manual_host;
        *remote_buttons = manual_remote;
        return true;
    }
    advance_at_barrier(scheduler);
    host_token = scheduler->macro_host[scheduler->host_index];
    remote_token = scheduler->macro_remote[scheduler->remote_index];
    if (host_token == '\0' && remote_token == '\0') {
        scheduler->macro_running = false;
        return true;
    }
    if (scheduler->step_frame < scheduler->press_frames) {
        *host_buttons = macro_button(host_token);
        *remote_buttons = macro_button(remote_token);
    }
    scheduler->step_frame++;
    if (scheduler->step_frame >= scheduler->step_frames) {
        scheduler->step_frame = 0u;
        if (host_token != '\0' && host_token != '|') scheduler->host_index++;
        if (remote_token != '\0' && remote_token != '|') scheduler->remote_index++;
    }
    return true;
}

void integral_gb_runtime_fixed_host_scheduler_stop(IntegralGBRuntimeFixedHostScheduler *scheduler)
{
    if (scheduler != NULL)
        integral_gb_runtime_secure_zero(scheduler, sizeof(*scheduler));
}

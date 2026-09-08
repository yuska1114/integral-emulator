/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_product_runtime.h"

#include "../runtimes/gb/src/server/secure_memory.h"

#include <string.h>

bool integral_gb_runtime_fixed_host_product_runtime_init(
    IntegralGBRuntimeFixedHostProductRuntime *runtime,
    IntegralGBRuntimeFixedHostProductRole role)
{
    if (runtime == NULL) return false;
    memset(runtime, 0, sizeof(*runtime));
    if (role != INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_HOST &&
        role != INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_REMOTE) return false;
    runtime->role = role;
    runtime->focused = true;
    runtime->running = true;
    return true;
}

void integral_gb_runtime_fixed_host_product_runtime_set_button(
    IntegralGBRuntimeFixedHostProductRuntime *runtime, uint8_t button, bool pressed)
{
    if (runtime == NULL || !runtime->running || !runtime->focused) return;
    if (pressed) runtime->buttons |= button;
    else runtime->buttons &= (uint8_t)~button;
}

void integral_gb_runtime_fixed_host_product_runtime_focus_lost(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    if (runtime == NULL || !runtime->running) return;
    runtime->focused = false;
    runtime->buttons = 0u;
    runtime->neutral_pending = true;
}

bool integral_gb_runtime_fixed_host_product_runtime_take_neutral(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    bool pending;
    if (runtime == NULL || !runtime->running) return false;
    pending = runtime->neutral_pending;
    runtime->neutral_pending = false;
    return pending;
}

void integral_gb_runtime_fixed_host_product_runtime_request_exit(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    if (runtime == NULL || !runtime->running) return;
    runtime->exit_confirming = true;
    runtime->exit_confirm_yes = false;
    runtime->buttons = 0u;
    runtime->neutral_pending = true;
}

void integral_gb_runtime_fixed_host_product_runtime_cancel_exit(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    if (runtime == NULL || !runtime->running) return;
    runtime->exit_confirming = false;
    runtime->exit_confirm_yes = false;
}

void integral_gb_runtime_fixed_host_product_runtime_toggle_exit_selection(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    if (runtime == NULL || !runtime->running || !runtime->exit_confirming) return;
    runtime->exit_confirm_yes = !runtime->exit_confirm_yes;
}

IntegralGBRuntimeFixedHostExitSelection
integral_gb_runtime_fixed_host_product_runtime_select_exit(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    if (runtime == NULL || !runtime->running || !runtime->exit_confirming)
        return INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONTINUE;
    if (runtime->exit_confirm_yes) {
        runtime->buttons = 0u;
        runtime->neutral_pending = true;
        return INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONFIRMED;
    }
    integral_gb_runtime_fixed_host_product_runtime_cancel_exit(runtime);
    return INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONTINUE;
}

void integral_gb_runtime_fixed_host_product_runtime_stop(
    IntegralGBRuntimeFixedHostProductRuntime *runtime)
{
    if (runtime != NULL)
        integral_gb_runtime_secure_zero(runtime, sizeof(*runtime));
}

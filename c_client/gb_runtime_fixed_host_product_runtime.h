/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_RUNTIME_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

typedef enum IntegralGBRuntimeFixedHostProductRole {
    INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_HOST = 1,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_REMOTE = 2,
} IntegralGBRuntimeFixedHostProductRole;

typedef enum IntegralGBRuntimeFixedHostExitSelection {
    INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONTINUE = 0,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_HOST_FINISH = 1,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_REMOTE_LEAVE = 2,
} IntegralGBRuntimeFixedHostExitSelection;

/* A confirmed Remote close is not a transport failure and must not trigger
 * the parent's 20-second automatic reconnect path. */
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_REMOTE_LEAVE_EXIT_CODE 3

typedef struct IntegralGBRuntimeFixedHostProductRuntime {
    IntegralGBRuntimeFixedHostProductRole role;
    uint8_t buttons;
    bool focused;
    bool neutral_pending;
    bool running;
    bool exit_confirming;
    bool exit_confirm_yes;
} IntegralGBRuntimeFixedHostProductRuntime;

bool integral_gb_runtime_fixed_host_product_runtime_init(
    IntegralGBRuntimeFixedHostProductRuntime *runtime,
    IntegralGBRuntimeFixedHostProductRole role);
void integral_gb_runtime_fixed_host_product_runtime_set_button(
    IntegralGBRuntimeFixedHostProductRuntime *runtime, uint8_t button, bool pressed);
void integral_gb_runtime_fixed_host_product_runtime_focus_lost(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);
bool integral_gb_runtime_fixed_host_product_runtime_take_neutral(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);
void integral_gb_runtime_fixed_host_product_runtime_request_exit(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);
void integral_gb_runtime_fixed_host_product_runtime_cancel_exit(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);
void integral_gb_runtime_fixed_host_product_runtime_toggle_exit_selection(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);
IntegralGBRuntimeFixedHostExitSelection
integral_gb_runtime_fixed_host_product_runtime_select_exit(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);
void integral_gb_runtime_fixed_host_product_runtime_stop(
    IntegralGBRuntimeFixedHostProductRuntime *runtime);

#endif

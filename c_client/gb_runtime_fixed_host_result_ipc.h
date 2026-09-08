/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_RESULT_IPC_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RESULT_IPC_H

#include "gb_runtime_fixed_host_snapshot_ipc.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct IntegralGBRuntimeFixedHostResult {
    bool host;
    uint64_t final_frame;
    uint8_t terminal_digest[32];
    IntegralGBRuntimeFixedHostSnapshotPair candidates;
} IntegralGBRuntimeFixedHostResult;

bool integral_gb_runtime_fixed_host_result_ipc_send(
    IntegralGBRuntimeFixedHostIPCWrite writer, void *context,
    IntegralGBRuntimeFixedHostResult *result);
bool integral_gb_runtime_fixed_host_result_ipc_receive(
    IntegralGBRuntimeFixedHostIPCRead reader, void *context,
    IntegralGBRuntimeFixedHostResult *result);
void integral_gb_runtime_fixed_host_result_release(IntegralGBRuntimeFixedHostResult *result);

#endif

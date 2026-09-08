/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_IPC_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_IPC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX (2u * 1024u * 1024u)

typedef ptrdiff_t (*IntegralGBRuntimeFixedHostIPCWrite)(
    void *context, const uint8_t *source, size_t size);
typedef ptrdiff_t (*IntegralGBRuntimeFixedHostIPCRead)(
    void *context, uint8_t *destination, size_t size);

typedef struct IntegralGBRuntimeFixedHostSnapshotPair {
    uint8_t *host_data;
    size_t host_size;
    uint8_t *remote_data;
    size_t remote_size;
} IntegralGBRuntimeFixedHostSnapshotPair;

/* Takes ownership of pair buffers and wipes/releases them on every path. */
bool integral_gb_runtime_fixed_host_snapshot_ipc_send(
    IntegralGBRuntimeFixedHostIPCWrite writer,
    void *context,
    IntegralGBRuntimeFixedHostSnapshotPair *pair);
bool integral_gb_runtime_fixed_host_snapshot_ipc_receive(
    IntegralGBRuntimeFixedHostIPCRead reader,
    void *context,
    IntegralGBRuntimeFixedHostSnapshotPair *pair);
void integral_gb_runtime_fixed_host_snapshot_pair_release(
    IntegralGBRuntimeFixedHostSnapshotPair *pair);

#endif

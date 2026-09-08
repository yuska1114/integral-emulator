/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_snapshot_ipc.h"

#include "../runtimes/gb/src/server/secure_memory.h"

#include <stdlib.h>
#include <string.h>

static const uint8_t SNAPSHOT_MAGIC[4] = {'I', 'F', 'S', '1'};

static void write_u32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24u);
    output[1] = (uint8_t)(value >> 16u);
    output[2] = (uint8_t)(value >> 8u);
    output[3] = (uint8_t)value;
}

static uint32_t read_u32(const uint8_t input[4])
{
    return ((uint32_t)input[0] << 24u) | ((uint32_t)input[1] << 16u) |
           ((uint32_t)input[2] << 8u) | input[3];
}

static bool write_all(IntegralGBRuntimeFixedHostIPCWrite writer, void *context,
                      const uint8_t *source, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        ptrdiff_t amount = writer(context, source + offset, size - offset);
        if (amount <= 0 || (size_t)amount > size - offset) return false;
        offset += (size_t)amount;
    }
    return true;
}

static bool read_all(IntegralGBRuntimeFixedHostIPCRead reader, void *context,
                     uint8_t *destination, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        ptrdiff_t amount = reader(context, destination + offset, size - offset);
        if (amount <= 0 || (size_t)amount > size - offset) return false;
        offset += (size_t)amount;
    }
    return true;
}

void integral_gb_runtime_fixed_host_snapshot_pair_release(
    IntegralGBRuntimeFixedHostSnapshotPair *pair)
{
    if (pair == NULL) return;
    if (pair->host_data != NULL) {
        integral_gb_runtime_secure_zero(pair->host_data, pair->host_size);
        free(pair->host_data);
    }
    if (pair->remote_data != NULL) {
        integral_gb_runtime_secure_zero(pair->remote_data, pair->remote_size);
        free(pair->remote_data);
    }
    integral_gb_runtime_secure_zero(pair, sizeof(*pair));
}

bool integral_gb_runtime_fixed_host_snapshot_ipc_send(
    IntegralGBRuntimeFixedHostIPCWrite writer, void *context,
    IntegralGBRuntimeFixedHostSnapshotPair *pair)
{
    uint8_t header[16] = {0};
    bool success = false;
    if (writer == NULL || pair == NULL ||
        pair->host_size > INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX ||
        pair->remote_size > INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX ||
        (pair->host_size != 0u && pair->host_data == NULL) ||
        (pair->remote_size != 0u && pair->remote_data == NULL)) goto done;
    memcpy(header, SNAPSHOT_MAGIC, sizeof(SNAPSHOT_MAGIC));
    header[4] = 1u;
    write_u32(header + 8u, (uint32_t)pair->host_size);
    write_u32(header + 12u, (uint32_t)pair->remote_size);
    success = write_all(writer, context, header, sizeof(header)) &&
              write_all(writer, context, pair->host_data, pair->host_size) &&
              write_all(writer, context, pair->remote_data, pair->remote_size);
done:
    integral_gb_runtime_secure_zero(header, sizeof(header));
    integral_gb_runtime_fixed_host_snapshot_pair_release(pair);
    return success;
}

bool integral_gb_runtime_fixed_host_snapshot_ipc_receive(
    IntegralGBRuntimeFixedHostIPCRead reader, void *context,
    IntegralGBRuntimeFixedHostSnapshotPair *pair)
{
    uint8_t header[16];
    bool success = false;
    if (reader == NULL || pair == NULL) return false;
    memset(pair, 0, sizeof(*pair));
    if (!read_all(reader, context, header, sizeof(header)) ||
        memcmp(header, SNAPSHOT_MAGIC, sizeof(SNAPSHOT_MAGIC)) != 0 ||
        header[4] != 1u || header[5] != 0u || header[6] != 0u ||
        header[7] != 0u) goto done;
    pair->host_size = read_u32(header + 8u);
    pair->remote_size = read_u32(header + 12u);
    if (pair->host_size > INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX ||
        pair->remote_size > INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX) goto done;
    if (pair->host_size != 0u) {
        pair->host_data = malloc(pair->host_size);
        if (pair->host_data == NULL) goto done;
    }
    if (pair->remote_size != 0u) {
        pair->remote_data = malloc(pair->remote_size);
        if (pair->remote_data == NULL) goto done;
    }
    if (!read_all(reader, context, pair->host_data, pair->host_size) ||
        !read_all(reader, context, pair->remote_data, pair->remote_size)) goto done;
    success = true;
done:
    integral_gb_runtime_secure_zero(header, sizeof(header));
    if (!success) integral_gb_runtime_fixed_host_snapshot_pair_release(pair);
    return success;
}

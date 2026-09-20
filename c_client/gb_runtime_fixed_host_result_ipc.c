/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_result_ipc.h"

#include "../runtimes/gb/src/server/content_hash.h"
#include "../runtimes/gb/src/server/secure_memory.h"

#include <stdlib.h>
#include <string.h>

#define RESULT_HEADER_SIZE 120u
static const uint8_t RESULT_MAGIC[4] = {'I', 'F', 'R', '1'};

static void put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value >> 24u); out[1] = (uint8_t)(value >> 16u);
    out[2] = (uint8_t)(value >> 8u); out[3] = (uint8_t)value;
}
static uint32_t get_u32(const uint8_t *in)
{
    return ((uint32_t)in[0] << 24u) | ((uint32_t)in[1] << 16u) |
           ((uint32_t)in[2] << 8u) | in[3];
}
static void put_u64(uint8_t *out, uint64_t value)
{
    for (unsigned i = 0; i < 8u; i++) out[7u - i] = (uint8_t)(value >> (i * 8u));
}
static uint64_t get_u64(const uint8_t *in)
{
    uint64_t value = 0u;
    for (unsigned i = 0; i < 8u; i++) value = (value << 8u) | in[i];
    return value;
}
static bool write_all(IntegralGBRuntimeFixedHostIPCWrite writer, void *context,
                      const uint8_t *data, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        ptrdiff_t amount = writer(context, data + offset, size - offset);
        if (amount <= 0 || (size_t)amount > size - offset) return false;
        offset += (size_t)amount;
    }
    return true;
}
static bool read_all(IntegralGBRuntimeFixedHostIPCRead reader, void *context,
                     uint8_t *data, size_t size)
{
    size_t offset = 0u;
    while (offset < size) {
        ptrdiff_t amount = reader(context, data + offset, size - offset);
        if (amount <= 0 || (size_t)amount > size - offset) return false;
        offset += (size_t)amount;
    }
    return true;
}

void integral_gb_runtime_fixed_host_result_release(IntegralGBRuntimeFixedHostResult *result)
{
    if (!result) return;
    integral_gb_runtime_fixed_host_snapshot_pair_release(&result->candidates);
    integral_gb_runtime_secure_zero(result, sizeof(*result));
}

bool integral_gb_runtime_fixed_host_result_ipc_send(
    IntegralGBRuntimeFixedHostIPCWrite writer, void *context,
    IntegralGBRuntimeFixedHostResult *result)
{
    uint8_t header[RESULT_HEADER_SIZE] = {0};
    bool valid = writer && result &&
        result->candidates.host_size <= INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX &&
        result->candidates.remote_size <= INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX &&
        (!result->candidates.host_size || result->candidates.host_data) &&
        (!result->candidates.remote_size || result->candidates.remote_data) &&
        (result->host || (!result->candidates.host_size && !result->candidates.remote_size));
    bool success = false;
    if (!valid) goto done;
    memcpy(header, RESULT_MAGIC, 4u); header[4] = 1u; header[5] = result->host ? 1u : 2u;
    put_u64(header + 8u, result->final_frame);
    memcpy(header + 16u, result->terminal_digest, 32u);
    integral_gb_runtime_content_sha256(result->candidates.host_data,
                            result->candidates.host_size, header + 48u);
    integral_gb_runtime_content_sha256(result->candidates.remote_data,
                            result->candidates.remote_size, header + 80u);
    put_u32(header + 112u, (uint32_t)result->candidates.host_size);
    put_u32(header + 116u, (uint32_t)result->candidates.remote_size);
    success = write_all(writer, context, header, sizeof(header)) &&
              write_all(writer, context, result->candidates.host_data,
                        result->candidates.host_size) &&
              write_all(writer, context, result->candidates.remote_data,
                        result->candidates.remote_size);
done:
    integral_gb_runtime_secure_zero(header, sizeof(header));
    integral_gb_runtime_fixed_host_result_release(result);
    return success;
}

bool integral_gb_runtime_fixed_host_result_ipc_receive(
    IntegralGBRuntimeFixedHostIPCRead reader, void *context,
    IntegralGBRuntimeFixedHostResult *result)
{
    return integral_gb_runtime_fixed_host_ipc_receive_event(reader, context, result) == 1;
}

bool integral_gb_runtime_fixed_host_ipc_request_ticket(
    IntegralGBRuntimeFixedHostIPCWrite writer, void *context)
{
    return writer && write_all(writer, context, (const uint8_t *)"IFQ1", 4);
}

int integral_gb_runtime_fixed_host_ipc_receive_event(
    IntegralGBRuntimeFixedHostIPCRead reader, void *context,
    IntegralGBRuntimeFixedHostResult *result)
{
    uint8_t header[RESULT_HEADER_SIZE], digest[32];
    bool success = false;
    if (!reader || !result) return false;
    memset(result, 0, sizeof(*result));
    if (!read_all(reader, context, header, 4)) goto done;
    if (!memcmp(header, "IFQ1", 4)) return 2;
    if (!read_all(reader, context, header + 4, sizeof(header) - 4) ||
        memcmp(header, RESULT_MAGIC, 4u) || header[4] != 1u ||
        (header[5] != 1u && header[5] != 2u) || header[6] || header[7]) goto done;
    result->host = header[5] == 1u;
    result->final_frame = get_u64(header + 8u);
    memcpy(result->terminal_digest, header + 16u, 32u);
    result->candidates.host_size = get_u32(header + 112u);
    result->candidates.remote_size = get_u32(header + 116u);
    if (result->candidates.host_size > INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX ||
        result->candidates.remote_size > INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX ||
        (!result->host && (result->candidates.host_size || result->candidates.remote_size))) goto done;
    if (result->candidates.host_size) {
        result->candidates.host_data = malloc(result->candidates.host_size);
        if (!result->candidates.host_data) goto done;
    }
    if (result->candidates.remote_size) {
        result->candidates.remote_data = malloc(result->candidates.remote_size);
        if (!result->candidates.remote_data) goto done;
    }
    if (!read_all(reader, context, result->candidates.host_data,
                  result->candidates.host_size) ||
        !read_all(reader, context, result->candidates.remote_data,
                  result->candidates.remote_size)) goto done;
    integral_gb_runtime_content_sha256(result->candidates.host_data,
                            result->candidates.host_size, digest);
    if (memcmp(digest, header + 48u, 32u)) goto done;
    integral_gb_runtime_content_sha256(result->candidates.remote_data,
                            result->candidates.remote_size, digest);
    if (memcmp(digest, header + 80u, 32u)) goto done;
    success = true;
done:
    integral_gb_runtime_secure_zero(header, sizeof(header));
    integral_gb_runtime_secure_zero(digest, sizeof(digest));
    if (!success) integral_gb_runtime_fixed_host_result_release(result);
    return success;
}

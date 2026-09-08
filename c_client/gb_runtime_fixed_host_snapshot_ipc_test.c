/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_snapshot_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MemoryPipe { uint8_t data[4096]; size_t size; size_t offset; size_t chunk; } MemoryPipe;
static unsigned checks;
#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

static ptrdiff_t memory_write(void *context, const uint8_t *source, size_t size)
{
    MemoryPipe *pipe = context;
    size_t amount = size > pipe->chunk ? pipe->chunk : size;
    if (amount > sizeof(pipe->data) - pipe->size) return -1;
    memcpy(pipe->data + pipe->size, source, amount);
    pipe->size += amount;
    return (ptrdiff_t)amount;
}

static ptrdiff_t memory_read(void *context, uint8_t *destination, size_t size)
{
    MemoryPipe *pipe = context;
    size_t available = pipe->size - pipe->offset;
    size_t amount = size > pipe->chunk ? pipe->chunk : size;
    if (amount > available) amount = available;
    if (amount == 0u) return 0;
    memcpy(destination, pipe->data + pipe->offset, amount);
    pipe->offset += amount;
    return (ptrdiff_t)amount;
}

static IntegralGBRuntimeFixedHostSnapshotPair make_pair(unsigned seed)
{
    IntegralGBRuntimeFixedHostSnapshotPair pair = {0};
    size_t index;
    pair.host_size = 1024u;
    pair.remote_size = 2048u;
    pair.host_data = malloc(pair.host_size);
    pair.remote_data = malloc(pair.remote_size);
    CHECK(pair.host_data != NULL && pair.remote_data != NULL);
    for (index = 0u; index < pair.host_size; index++)
        pair.host_data[index] = (uint8_t)(index + seed);
    for (index = 0u; index < pair.remote_size; index++)
        pair.remote_data[index] = (uint8_t)(index * 3u + seed);
    return pair;
}

int main(void)
{
    unsigned cycle;
    for (cycle = 0u; cycle < 100u; cycle++) {
        MemoryPipe pipe = {.chunk = cycle % 17u + 1u};
        IntegralGBRuntimeFixedHostSnapshotPair input = make_pair(cycle);
        IntegralGBRuntimeFixedHostSnapshotPair output;
        CHECK(integral_gb_runtime_fixed_host_snapshot_ipc_send(memory_write, &pipe, &input));
        CHECK(input.host_data == NULL && input.remote_data == NULL);
        CHECK(integral_gb_runtime_fixed_host_snapshot_ipc_receive(memory_read, &pipe, &output));
        CHECK(output.host_size == 1024u && output.remote_size == 2048u);
        CHECK(output.host_data[31] == (uint8_t)(31u + cycle));
        CHECK(output.remote_data[47] == (uint8_t)(47u * 3u + cycle));
        integral_gb_runtime_fixed_host_snapshot_pair_release(&output);
        CHECK(output.host_data == NULL && output.remote_data == NULL);
    }
    {
        MemoryPipe truncated = {.chunk = 5u, .size = 3u};
        IntegralGBRuntimeFixedHostSnapshotPair output;
        CHECK(!integral_gb_runtime_fixed_host_snapshot_ipc_receive(
            memory_read, &truncated, &output));
        CHECK(output.host_data == NULL && output.remote_data == NULL);
    }
    printf("PASS fixed Host memory-only IPC cycles=100 checks=%u\n", checks);
    return 0;
}

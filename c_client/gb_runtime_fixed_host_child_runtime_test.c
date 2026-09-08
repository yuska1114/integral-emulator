/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_child_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct MemoryPipe { uint8_t data[64]; size_t size; size_t offset; } MemoryPipe;
static unsigned checks;
#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

static ptrdiff_t pipe_write(void *context, const uint8_t *source, size_t size)
{
    MemoryPipe *pipe = context;
    size_t amount = size > 3u ? 3u : size;
    if (amount > sizeof(pipe->data) - pipe->size) return -1;
    memcpy(pipe->data + pipe->size, source, amount);
    pipe->size += amount;
    return (ptrdiff_t)amount;
}

static ptrdiff_t pipe_read(void *context, uint8_t *destination, size_t size)
{
    MemoryPipe *pipe = context;
    size_t available = pipe->size - pipe->offset;
    size_t amount = size > 5u ? 5u : size;
    if (amount > available) amount = available;
    if (amount == 0u) return 0;
    memcpy(destination, pipe->data + pipe->offset, amount);
    pipe->offset += amount;
    return (ptrdiff_t)amount;
}

int main(int argc, char **argv)
{
    IntegralGBRuntimeFixedHostChildConfig config;
    unsigned cycle;
    if (argc != 3) return 2;
    config = (IntegralGBRuntimeFixedHostChildConfig){
        .host_rom_path = argv[1],
        .remote_rom_path = argv[2],
        .role = INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_HOST,
    };
    for (cycle = 0u; cycle < 100u; cycle++) {
        IntegralGBRuntimeFixedHostSnapshotPair pair = {0};
        IntegralGBRuntimeFixedHostChildRuntime runtime;
        MemoryPipe pipe = {0};
        CHECK(integral_gb_runtime_fixed_host_snapshot_ipc_send(
            pipe_write, &pipe, &pair));
        CHECK(integral_gb_runtime_fixed_host_child_runtime_start(
            &runtime, &config, pipe_read, &pipe));
        CHECK(runtime.initialized);
        CHECK(integral_gb_runtime_fixed_host_child_runtime_frame(
            &runtime, 0u, 0u) == 0);
        integral_gb_runtime_fixed_host_child_runtime_stop(&runtime);
        CHECK(!runtime.initialized && !runtime.core.engine.a.gb &&
              !runtime.core.engine.b.gb);
    }
    printf("PASS fixed Host child lifecycle loopback cycles=100 checks=%u\n",
           checks);
    return 0;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define GB_INTERNAL
#include "gb_runtime_fixed_host_runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static bool all_zero(const uint8_t *data, size_t size)
{
    size_t index;
    for (index = 0; index < size; index++) {
        if (data[index] != 0u) return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    uint8_t host_save[8192], remote_save[8192], rejected_save[32];
    IntegralGBRuntimeFixedHostRuntime runtime;
    IntegralGBRuntimeFixedHostRuntimeConfig config;
    if (argc != 3) {
        fprintf(stderr, "usage: %s HOST_ROM REMOTE_ROM\n", argv[0]);
        return 2;
    }
    memset(host_save, 0x5a, sizeof(host_save));
    memset(remote_save, 0xa5, sizeof(remote_save));
    config = (IntegralGBRuntimeFixedHostRuntimeConfig){
        .host_rom_path = argv[1],
        .remote_rom_path = argv[2],
        .host_save = host_save,
        .host_save_size = sizeof(host_save),
        .remote_save = remote_save,
        .remote_save_size = sizeof(remote_save),
        .preserve_both_audio = true,
        .ir_off_delay_ticks = 32,
    };
    CHECK(integral_gb_runtime_fixed_host_runtime_init(&runtime, &config) == 0);
    CHECK(runtime.initialized);
    CHECK(runtime.engine.a.gb->ir_off_delay_ticks == 32);
    CHECK(runtime.engine.b.gb->ir_off_delay_ticks == 32);
    CHECK(all_zero(host_save, sizeof(host_save)));
    CHECK(all_zero(remote_save, sizeof(remote_save)));
    CHECK(integral_gb_runtime_fixed_host_runtime_run_frame(&runtime, 0u, 0u) == 0);
    integral_gb_runtime_fixed_host_runtime_free(&runtime);
    CHECK(!runtime.initialized);
    CHECK(!runtime.engine.a.gb && !runtime.engine.b.gb);

    memset(rejected_save, 0x3c, sizeof(rejected_save));
    config = (IntegralGBRuntimeFixedHostRuntimeConfig){
        .host_rom_path = "/definitely/missing/gb-runtime-fixed-host.gb",
        .remote_rom_path = argv[2],
        .host_save = rejected_save,
        .host_save_size = sizeof(rejected_save),
    };
    CHECK(integral_gb_runtime_fixed_host_runtime_init(&runtime, &config) != 0);
    CHECK(all_zero(rejected_save, sizeof(rejected_save)));
    CHECK(!runtime.initialized);
    CHECK(integral_gb_runtime_fixed_host_runtime_run_frame(&runtime, 0u, 0u) != 0);
    integral_gb_runtime_fixed_host_runtime_free(&runtime);

    printf("PASS fixed Host runtime memory-only checks=%u\n", checks);
    return 0;
}

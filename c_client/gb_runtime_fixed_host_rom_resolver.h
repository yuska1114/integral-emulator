/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_H
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVER_H

#include "client_config.h"
#include "http_client.h"
#include "rom_metadata.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum IntegralGBRuntimeFixedHostRomResolveStatus {
    INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK = 0,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_INVALID,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_A,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_B,
    INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_BOTH,
} IntegralGBRuntimeFixedHostRomResolveStatus;

typedef struct IntegralGBRuntimeFixedHostRomResolution {
    char path_a[INTEGRAL_CONFIG_PATH_MAX];
    char path_b[INTEGRAL_CONFIG_PATH_MAX];
    size_t slot_a;
    size_t slot_b;
    bool shared_asset;
} IntegralGBRuntimeFixedHostRomResolution;

typedef bool (*IntegralGBRuntimeFixedHostRomProbe)(
    const char *path, IntegralRomMetadata *metadata, void *context);

bool integral_gb_runtime_fixed_host_rom_probe_file(
    const char *path, IntegralRomMetadata *metadata, void *context);
IntegralGBRuntimeFixedHostRomResolveStatus integral_gb_runtime_fixed_host_rom_resolve_with_probe(
    const IntegralConfigRomSlot *slots, const IntegralApiRomSlot *server_slots,
    size_t slot_count,
    const char *game_type_a, const char *platform_a, const char *header_title_a,
    const char *game_type_b, const char *platform_b, const char *header_title_b,
    IntegralGBRuntimeFixedHostRomProbe probe, void *probe_context,
    IntegralGBRuntimeFixedHostRomResolution *resolution);
IntegralGBRuntimeFixedHostRomResolveStatus integral_gb_runtime_fixed_host_rom_resolve_local(
    const IntegralConfigRomSlot *slots, const IntegralApiRomSlot *server_slots,
    size_t slot_count,
    const char *game_type_a, const char *platform_a, const char *header_title_a,
    const char *game_type_b, const char *platform_b, const char *header_title_b,
    IntegralGBRuntimeFixedHostRomResolution *resolution);
const char *integral_gb_runtime_fixed_host_rom_resolve_status_text(
    IntegralGBRuntimeFixedHostRomResolveStatus status);

#endif

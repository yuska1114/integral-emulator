/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_rom_resolver.h"
#include "../runtimes/gb/src/server/content_hash.h"

#include <stdio.h>
#include <string.h>

static void copy_text(char *output, size_t output_size, const char *value)
{
    size_t length;
    if (!output || output_size == 0u || !value) return;
    length = strlen(value);
    if (length >= output_size) length = output_size - 1u;
    memcpy(output, value, length);
    output[length] = '\0';
}

static bool valid_text(const char *value)
{
    return value && value[0];
}

static bool valid_path(const char path[INTEGRAL_CONFIG_PATH_MAX])
{
    return path && path[0] && memchr(path, '\0', INTEGRAL_CONFIG_PATH_MAX);
}

bool integral_gb_runtime_fixed_host_rom_probe_file(
    const char *path, IntegralRomMetadata *metadata, void *context)
{
    (void)context;
    return integral_rom_metadata_read(path, metadata) == 0;
}

static bool slot_matches(
    const IntegralConfigRomSlot *slot, const IntegralApiRomSlot *server_slot,
    const char *game_type, const char *platform, const char *header_title,
    IntegralGBRuntimeFixedHostRomProbe probe, void *probe_context)
{
    IntegralRomMetadata local;
    IntegralGBRuntimeContentSha256 hash;
    unsigned char digest[INTEGRAL_GB_RUNTIME_CONTENT_SHA256_SIZE];
    char sha256[65];
    unsigned char buffer[8192];
    FILE *file;
    size_t size;
    if (!valid_path(slot->rom_path) || !server_slot->sha256[0]) return false;
    file = fopen(slot->rom_path, "rb");
    if (!file) return false;
    integral_gb_runtime_content_sha256_init(&hash);
    while ((size = fread(buffer, 1u, sizeof(buffer), file)) > 0u) {
        integral_gb_runtime_content_sha256_update(&hash, buffer, size);
    }
    bool read_failed = ferror(file) != 0;
    if (fclose(file) != 0 || read_failed) return false;
    integral_gb_runtime_content_sha256_finish(&hash, digest);
    integral_gb_runtime_content_sha256_hex(digest, sha256);
    return valid_path(slot->rom_path) && server_slot->rom_id[0] &&
           server_slot->save_id[0] &&
           strcmp(server_slot->sha256, sha256) == 0 &&
           strcmp(server_slot->game_type, game_type) == 0 &&
           strcmp(server_slot->platform, platform) == 0 &&
           strcmp(server_slot->rom_header_title, header_title) == 0 &&
           probe(slot->rom_path, &local, probe_context) &&
           strcmp(local.platform, server_slot->platform) == 0 &&
           strcmp(local.header_title, server_slot->rom_header_title) == 0;
}

IntegralGBRuntimeFixedHostRomResolveStatus integral_gb_runtime_fixed_host_rom_resolve_with_probe(
    const IntegralConfigRomSlot *slots, const IntegralApiRomSlot *server_slots,
    size_t slot_count,
    const char *game_type_a, const char *platform_a, const char *header_title_a,
    const char *game_type_b, const char *platform_b, const char *header_title_b,
    IntegralGBRuntimeFixedHostRomProbe probe, void *probe_context,
    IntegralGBRuntimeFixedHostRomResolution *resolution)
{
    bool found_a = false;
    bool found_b = false;
    if (resolution) memset(resolution, 0, sizeof(*resolution));
    if (!slots || !server_slots || !resolution || !probe || slot_count == 0u ||
        slot_count > INTEGRAL_CONFIG_ROM_SLOTS ||
        !valid_text(game_type_a) || !valid_text(platform_a) || !valid_text(header_title_a) ||
        !valid_text(game_type_b) || !valid_text(platform_b) || !valid_text(header_title_b)) {
        return INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_INVALID;
    }
    for (size_t index = 0u; index < slot_count && (!found_a || !found_b); index++) {
        if (!found_a && slot_matches(&slots[index], &server_slots[index], game_type_a,
                                    platform_a, header_title_a, probe, probe_context)) {
            copy_text(resolution->path_a, sizeof(resolution->path_a), slots[index].rom_path);
            resolution->slot_a = index;
            found_a = true;
        }
        if (!found_b && slot_matches(&slots[index], &server_slots[index], game_type_b,
                                    platform_b, header_title_b, probe, probe_context)) {
            copy_text(resolution->path_b, sizeof(resolution->path_b), slots[index].rom_path);
            resolution->slot_b = index;
            found_b = true;
        }
    }
    if (!found_a || !found_b) {
        memset(resolution, 0, sizeof(*resolution));
        if (!found_a && !found_b) return INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_BOTH;
        return found_a ? INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_B
                       : INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_A;
    }
    resolution->shared_asset = resolution->slot_a == resolution->slot_b;
    return INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK;
}

IntegralGBRuntimeFixedHostRomResolveStatus integral_gb_runtime_fixed_host_rom_resolve_local(
    const IntegralConfigRomSlot *slots, const IntegralApiRomSlot *server_slots,
    size_t slot_count,
    const char *game_type_a, const char *platform_a, const char *header_title_a,
    const char *game_type_b, const char *platform_b, const char *header_title_b,
    IntegralGBRuntimeFixedHostRomResolution *resolution)
{
    return integral_gb_runtime_fixed_host_rom_resolve_with_probe(
        slots, server_slots, slot_count,
        game_type_a, platform_a, header_title_a,
        game_type_b, platform_b, header_title_b,
        integral_gb_runtime_fixed_host_rom_probe_file, NULL, resolution);
}

const char *integral_gb_runtime_fixed_host_rom_resolve_status_text(
    IntegralGBRuntimeFixedHostRomResolveStatus status)
{
    switch (status) {
        case INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK: return "OK";
        case INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_INVALID: return "INVALID REQUEST";
        case INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_A: return "PLAYER A ROM MISSING";
        case INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_B: return "PLAYER B ROM MISSING";
        case INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_BOTH: return "BOTH ROMS MISSING";
        default: return "INVALID REQUEST";
    }
}

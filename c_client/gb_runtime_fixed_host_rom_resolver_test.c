/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_rom_resolver.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

typedef struct Fixture {
    const char *path;
    const char *platform;
    const char *header;
} Fixture;

static bool fake_probe(const char *path, IntegralRomMetadata *metadata, void *context)
{
    const Fixture *fixtures = context;
    memset(metadata, 0, sizeof(*metadata));
    for (size_t i = 0u; fixtures[i].path; i++) {
        if (strcmp(path, fixtures[i].path) == 0) {
            snprintf(metadata->platform, sizeof(metadata->platform), "%s", fixtures[i].platform);
            snprintf(metadata->header_title, sizeof(metadata->header_title), "%s", fixtures[i].header);
            return true;
        }
    }
    return false;
}

static void set_slot(IntegralConfigRomSlot *local, IntegralApiRomSlot *server,
                     const char *path, const char *sha256,
                     const char *game_type, const char *header)
{
    snprintf(local->rom_path, sizeof(local->rom_path), "%s", path);
    snprintf(server->rom_id, sizeof(server->rom_id), "rom-id");
    snprintf(server->save_id, sizeof(server->save_id), "save-id");
    snprintf(server->sha256, sizeof(server->sha256), "%s", sha256);
    snprintf(server->game_type, sizeof(server->game_type), "%s", game_type);
    snprintf(server->platform, sizeof(server->platform), "gb");
    snprintf(server->rom_header_title, sizeof(server->rom_header_title), "%s", header);
}

int main(void)
{
    IntegralConfigRomSlot local[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    IntegralApiRomSlot server[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    IntegralGBRuntimeFixedHostRomResolution result;
    const Fixture fixtures[] = {
        {"alpha.gbc", "gb", "ALPHA CORE"},
        {"beta.gbc", "gb", "BETA CORE"},
        {NULL, NULL, NULL},
    };
    FILE *file = fopen("alpha.gbc", "wb");
    CHECK(file && fwrite("alpha", 1u, 5u, file) == 5u && fclose(file) == 0);
    file = fopen("beta.gbc", "wb");
    CHECK(file && fwrite("beta", 1u, 4u, file) == 4u && fclose(file) == 0);
    set_slot(&local[0], &server[0], "alpha.gbc",
             "8ed3f6ad685b959ead7022518e1af76cd816f8e8ec7ccdda1ed4018e8f2223f8",
             "catalog_alpha", "ALPHA CORE");
    set_slot(&local[1], &server[1], "beta.gbc",
             "f44e64e75f3948e9f73f8dfa94721c4ce8cbb4f265c4790c702b2d41cfbf2753",
             "catalog_beta", "BETA CORE");

    CHECK(integral_gb_runtime_fixed_host_rom_resolve_with_probe(
              local, server, INTEGRAL_CONFIG_ROM_SLOTS,
              "catalog_alpha", "gb", "ALPHA CORE",
              "catalog_beta", "gb", "BETA CORE",
              fake_probe, (void *)fixtures, &result) ==
          INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK);
    CHECK(result.slot_a == 0u && result.slot_b == 1u && !result.shared_asset);

    snprintf(server[1].rom_header_title, sizeof(server[1].rom_header_title), "WRONG");
    CHECK(integral_gb_runtime_fixed_host_rom_resolve_with_probe(
              local, server, INTEGRAL_CONFIG_ROM_SLOTS,
              "catalog_alpha", "gb", "ALPHA CORE",
              "catalog_beta", "gb", "BETA CORE",
              fake_probe, (void *)fixtures, &result) ==
          INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_B);
    CHECK(integral_gb_runtime_fixed_host_rom_resolve_status_text(
              INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_MISSING_B)[0] != '\0');
    remove("alpha.gbc");
    remove("beta.gbc");
    puts("gb runtime fixed host ROM resolver test passed");
    return 0;
}

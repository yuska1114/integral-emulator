/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sgb_boot_resource.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_resource_path(const char *path)
{
#ifdef _WIN32
    assert(_putenv_s("INTEGRAL_EMULATOR_GB_RUNTIME_SGB2_BOOT_ROM", path) == 0);
#else
    assert(setenv("INTEGRAL_EMULATOR_GB_RUNTIME_SGB2_BOOT_ROM", path, 1) == 0);
#endif
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s VALID_BOOT_ROM CORRUPT_TEMP_PATH\n", argv[0]);
        return 2;
    }
    uint8_t data[INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE];
    char resolved[4096];
    char error[192];

    set_resource_path(argv[1]);
    assert(integral_gb_runtime_sgb2_boot_resource_load(
               data, resolved, sizeof(resolved), error, sizeof(error)) == 0);
    assert(strcmp(resolved, argv[1]) == 0);

    set_resource_path("/definitely/missing/integral-sgb2-boot.bin");
    assert(integral_gb_runtime_sgb2_boot_resource_load(
               data, resolved, sizeof(resolved), error, sizeof(error)) != 0);

    data[0] ^= 0x01;
    FILE *corrupt = fopen(argv[2], "wb");
    assert(corrupt != NULL);
    assert(fwrite(data, 1, sizeof(data), corrupt) == sizeof(data));
    assert(fclose(corrupt) == 0);
    set_resource_path(argv[2]);
    assert(integral_gb_runtime_sgb2_boot_resource_load(
               data, resolved, sizeof(resolved), error, sizeof(error)) != 0);
    assert(strstr(error, "SHA-256 mismatch") != NULL);
    assert(remove(argv[2]) == 0);

    puts("SGB2 SameBoot resource tests passed");
    return 0;
}

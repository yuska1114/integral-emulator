/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SGB_BOOT_RESOURCE_H
#define INTEGRAL_GB_RUNTIME_SGB_BOOT_RESOURCE_H

#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE 256u
#define INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SHA256 \
    "8a65465a9da7ec657726a671da9a85963cf19d2c975e499aea6b97eb37e0b6ea"

int integral_gb_runtime_sgb2_boot_resource_load(uint8_t output[INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE],
                                        char *resolved_path,
                                        size_t resolved_path_size,
                                        char *error,
                                        size_t error_size);

#endif

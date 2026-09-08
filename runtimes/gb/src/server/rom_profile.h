/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_ROM_PROFILE_H
#define INTEGRAL_GB_RUNTIME_ROM_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "gb.h"

typedef struct IntegralGBRuntimeRomProfile {
    char title[17];
    uint8_t cgb_flag;
    uint8_t new_licensee[2];
    uint8_t sgb_flag;
    uint8_t old_licensee;
    uint8_t header_checksum;
} IntegralGBRuntimeRomProfile;

typedef enum IntegralGBRuntimeRomModelReason {
    INTEGRAL_GB_RUNTIME_ROM_MODEL_CGB_HEADER,
    INTEGRAL_GB_RUNTIME_ROM_MODEL_SGB_HEADER,
    INTEGRAL_GB_RUNTIME_ROM_MODEL_DMG_DEFAULT,
} IntegralGBRuntimeRomModelReason;

int integral_gb_runtime_rom_profile_read(const char *path,
                                 IntegralGBRuntimeRomProfile *profile,
                                 char *error,
                                 size_t error_size);
int integral_gb_runtime_rom_profile_select_model(const IntegralGBRuntimeRomProfile *profile,
                                         GB_model_t *model,
                                         IntegralGBRuntimeRomModelReason *reason,
                                         char *error,
                                         size_t error_size);
const char *integral_gb_runtime_rom_model_reason_name(IntegralGBRuntimeRomModelReason reason);

#endif

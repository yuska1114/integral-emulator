/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom_profile.h"
#include "../common/utf8_file.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define INTEGRAL_GB_RUNTIME_ROM_HEADER_SIZE 0x150u

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error && error_size > 0) {
        snprintf(error, error_size, "%s", message);
    }
}

int integral_gb_runtime_rom_profile_read(const char *path,
                                 IntegralGBRuntimeRomProfile *profile,
                                 char *error,
                                 size_t error_size)
{
    if (!path || !profile) {
        set_error(error, error_size, "missing ROM path or profile output");
        return -1;
    }

    FILE *rom = integral_fopen(path, "rb");
    if (!rom) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "open failed: %s", strerror(errno));
        }
        return -1;
    }

    uint8_t header[INTEGRAL_GB_RUNTIME_ROM_HEADER_SIZE];
    size_t read_size = fread(header, 1, sizeof(header), rom);
    int read_error = ferror(rom);
    fclose(rom);
    if (read_error) {
        set_error(error, error_size, "header read failed");
        return -1;
    }
    if (read_size != sizeof(header)) {
        set_error(error, error_size, "ROM is shorter than 0x150-byte header");
        return -1;
    }

    memset(profile, 0, sizeof(*profile));
    memcpy(profile->title, header + 0x134, 16);
    profile->title[16] = '\0';
    size_t title_length = 16;
    while (title_length > 0 &&
           (profile->title[title_length - 1] == '\0' || profile->title[title_length - 1] == ' ')) {
        profile->title[--title_length] = '\0';
    }
    profile->cgb_flag = header[0x143];
    profile->new_licensee[0] = header[0x144];
    profile->new_licensee[1] = header[0x145];
    profile->sgb_flag = header[0x146];
    profile->old_licensee = header[0x14B];
    profile->header_checksum = header[0x14D];
    return 0;
}

int integral_gb_runtime_rom_profile_select_model(const IntegralGBRuntimeRomProfile *profile,
                                         GB_model_t *model,
                                         IntegralGBRuntimeRomModelReason *reason,
                                         char *error,
                                         size_t error_size)
{
    if (!profile || !model || !reason) {
        set_error(error, error_size, "missing model selection input");
        return -1;
    }
    if (profile->cgb_flag == 0x80 || profile->cgb_flag == 0xC0) {
        *model = GB_MODEL_CGB_E;
        *reason = INTEGRAL_GB_RUNTIME_ROM_MODEL_CGB_HEADER;
        return 0;
    }
    if ((profile->cgb_flag & 0x80) != 0) {
        if (error && error_size > 0) {
            snprintf(error, error_size, "unsupported CGB flag %02X", profile->cgb_flag);
        }
        return -1;
    }
    if (profile->sgb_flag == 0x03 && profile->old_licensee == 0x33) {
        *model = GB_MODEL_SGB2;
        *reason = INTEGRAL_GB_RUNTIME_ROM_MODEL_SGB_HEADER;
        return 0;
    }
    *model = GB_MODEL_DMG_B;
    *reason = INTEGRAL_GB_RUNTIME_ROM_MODEL_DMG_DEFAULT;
    return 0;
}

const char *integral_gb_runtime_rom_model_reason_name(IntegralGBRuntimeRomModelReason reason)
{
    switch (reason) {
        case INTEGRAL_GB_RUNTIME_ROM_MODEL_CGB_HEADER:
            return "cgb_header";
        case INTEGRAL_GB_RUNTIME_ROM_MODEL_SGB_HEADER:
            return "sgb_header";
        case INTEGRAL_GB_RUNTIME_ROM_MODEL_DMG_DEFAULT:
            return "dmg_default";
    }
    return "unknown";
}

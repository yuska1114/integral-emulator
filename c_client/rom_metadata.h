/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_ROM_METADATA_H
#define INTEGRAL_ROM_METADATA_H

#include <stdbool.h>

typedef struct IntegralRomMetadata {
    char platform[4];
    char region[17];
    char header_title[21];
    bool checksum_ok;
    unsigned char cgb_flag;
    unsigned char sgb_flag;
    unsigned char cartridge_type;
    unsigned char rom_size;
    unsigned char ram_size;
    unsigned char destination;
    unsigned char version;
} IntegralRomMetadata;

int integral_rom_metadata_read(const char *path, IntegralRomMetadata *metadata);

#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom_metadata.h"

#include <stdio.h>
#include <string.h>

#define CHECK(value) do { if (!(value)) { fprintf(stderr, "check failed: %s:%d\n", __FILE__, __LINE__); return 1; } } while (0)

static int write_bytes(const char *path, const unsigned char *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    return fwrite(data, 1u, size, file) == size && fclose(file) == 0 ? 0 : -1;
}

static void gb_checksum(unsigned char rom[0x150])
{
    unsigned char checksum = 0u;
    for (size_t i = 0x134u; i <= 0x14cu; i++) checksum = (unsigned char)(checksum - rom[i] - 1u);
    rom[0x14du] = checksum;
}

int main(void)
{
    unsigned char gb[0x150] = {0};
    unsigned char n64[0x40] = {0};
    IntegralRomMetadata metadata;

    memcpy(gb + 0x134u, "GENERIC SAMPLE", 14u);
    gb[0x14au] = 1u;
    gb_checksum(gb);
    CHECK(write_bytes("metadata_sample.gbc", gb, sizeof(gb)) == 0);
    CHECK(integral_rom_metadata_read("metadata_sample.gbc", &metadata) == 0);
    CHECK(strcmp(metadata.platform, "gb") == 0);
    CHECK(strcmp(metadata.region, "WORLD") == 0);
    CHECK(strcmp(metadata.header_title, "GENERIC SAMPLE") == 0);

    n64[0] = 0x80u; n64[1] = 0x37u; n64[2] = 0x12u; n64[3] = 0x40u;
    memcpy(n64 + 0x20u, "GENERIC N64", 11u);
    n64[0x3eu] = 'E';
    CHECK(write_bytes("metadata_sample.z64", n64, sizeof(n64)) == 0);
    CHECK(integral_rom_metadata_read("metadata_sample.z64", &metadata) == 0);
    CHECK(strcmp(metadata.platform, "n64") == 0);
    CHECK(strcmp(metadata.region, "US") == 0);
    CHECK(strcmp(metadata.header_title, "GENERIC N64") == 0);

    memset(gb + 0x134u, 0, 16u);
    gb_checksum(gb);
    CHECK(write_bytes("metadata_empty.gb", gb, sizeof(gb)) == 0);
    CHECK(integral_rom_metadata_read("metadata_empty.gb", &metadata) != 0);
    gb[0x134u] = 1u;
    gb_checksum(gb);
    CHECK(write_bytes("metadata_invalid.gb", gb, sizeof(gb)) == 0);
    CHECK(integral_rom_metadata_read("metadata_invalid.gb", &metadata) != 0);

    remove("metadata_sample.gbc"); remove("metadata_sample.z64");
    remove("metadata_empty.gb"); remove("metadata_invalid.gb");
    puts("ROM metadata test passed");
    return 0;
}

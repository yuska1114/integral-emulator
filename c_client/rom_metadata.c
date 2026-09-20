/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom_metadata.h"
#include "../runtimes/gb/src/common/utf8_file.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static const char *extension(const char *path)
{
    const char *dot = path ? strrchr(path, '.') : NULL;
    return dot ? dot : "";
}

static bool ascii_equal_ignore_case(const char *left, const char *right)
{
    if (!left || !right) return false;
    while (*left && *right) {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) return false;
        left++;
        right++;
    }
    return *left == '\0' && *right == '\0';
}

static bool extension_is(const char *path, const char *first, const char *second)
{
    const char *value = extension(path);
    return ascii_equal_ignore_case(value, first) ||
           (second && ascii_equal_ignore_case(value, second));
}

static bool copy_header_text(const unsigned char *source, size_t size,
                             char *output, size_t output_size)
{
    size_t written = 0u;
    bool has_alnum = false;
    if (!source || !output || output_size == 0u) return false;
    while (written < size && source[written] != 0u &&
           source[written] != 0x80u && source[written] != 0xc0u) {
        unsigned char value = source[written];
        if (value < 0x20u || value > 0x7eu || written + 1u >= output_size) return false;
        output[written] = (char)value;
        if (isalnum(value)) has_alnum = true;
        written++;
    }
    while (written > 0u && output[written - 1u] == ' ') written--;
    output[written] = '\0';
    return written > 0u && has_alnum;
}

static int normalize_n64_header(const unsigned char *raw, size_t raw_size,
                                unsigned char output[0x40])
{
    if (raw_size < 0x40u) return -1;
    if (raw[0] == 0x80u && raw[1] == 0x37u && raw[2] == 0x12u && raw[3] == 0x40u) {
        memcpy(output, raw, 0x40u);
        return 0;
    }
    if (raw[0] == 0x37u && raw[1] == 0x80u && raw[2] == 0x40u && raw[3] == 0x12u) {
        for (size_t i = 0u; i < 0x40u; i += 2u) {
            output[i] = raw[i + 1u];
            output[i + 1u] = raw[i];
        }
        return 0;
    }
    if (raw[0] == 0x40u && raw[1] == 0x12u && raw[2] == 0x37u && raw[3] == 0x80u) {
        for (size_t i = 0u; i < 0x40u; i += 4u) {
            output[i] = raw[i + 3u];
            output[i + 1u] = raw[i + 2u];
            output[i + 2u] = raw[i + 1u];
            output[i + 3u] = raw[i];
        }
        return 0;
    }
    return -1;
}

static void n64_region(unsigned char code, char output[17])
{
    switch (code) {
        case 'J': strcpy(output, "JP"); break;
        case 'E': strcpy(output, "US"); break;
        case 'P': case 'D': case 'F': case 'I': case 'S': case 'U':
        case 'X': case 'Y': strcpy(output, "EU"); break;
        case 'A': strcpy(output, "WORLD"); break;
        case 'B': strcpy(output, "BR"); break;
        case 'C': strcpy(output, "CN"); break;
        case 'K': strcpy(output, "KR"); break;
        default: snprintf(output, 17u, "N64_%02X", code); break;
    }
}

int integral_rom_metadata_read(const char *path, IntegralRomMetadata *metadata)
{
    unsigned char raw[0x150];
    unsigned char n64[0x40];
    FILE *file;
    size_t size;
    if (!path || !metadata) return -1;
    memset(metadata, 0, sizeof(*metadata));
    file = integral_fopen(path, "rb");
    if (!file) return -1;
    size = fread(raw, 1u, sizeof(raw), file);
    if (fclose(file) != 0) return -1;

    if (extension_is(path, ".z64", ".n64") || extension_is(path, ".v64", NULL)) {
        if (normalize_n64_header(raw, size, n64) != 0 ||
            !copy_header_text(n64 + 0x20u, 20u, metadata->header_title,
                              sizeof(metadata->header_title))) return -1;
        strcpy(metadata->platform, "n64");
        n64_region(n64[0x3eu], metadata->region);
        metadata->checksum_ok = true;
        metadata->destination = n64[0x3eu];
        return 0;
    }

    if (!extension_is(path, ".gb", ".gbc") || size < sizeof(raw)) return -1;
    if (!copy_header_text(raw + 0x0134u, 16u, metadata->header_title,
                          sizeof(metadata->header_title))) return -1;
    unsigned char checksum = 0u;
    for (size_t i = 0x0134u; i <= 0x014cu; i++) {
        checksum = (unsigned char)(checksum - raw[i] - 1u);
    }
    metadata->checksum_ok = checksum == raw[0x014du];
    if (!metadata->checksum_ok) return -1;
    strcpy(metadata->platform, "gb");
    strcpy(metadata->region, raw[0x014au] == 0u ? "JP" :
                             (raw[0x014au] == 1u ? "WORLD" : "GB_UNKNOWN"));
    metadata->cgb_flag = raw[0x0143u];
    metadata->sgb_flag = raw[0x0146u];
    metadata->cartridge_type = raw[0x0147u];
    metadata->rom_size = raw[0x0148u];
    metadata->ram_size = raw[0x0149u];
    metadata->destination = raw[0x014au];
    metadata->version = raw[0x014cu];
    return 0;
}

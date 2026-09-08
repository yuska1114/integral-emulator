/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remote_input.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void write_be32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24u);
    data[1] = (unsigned char)(value >> 16u);
    data[2] = (unsigned char)(value >> 8u);
    data[3] = (unsigned char)value;
}

static void write_be64(unsigned char *data, uint64_t value)
{
    int index;
    for (index = 7; index >= 0; --index) {
        data[index] = (unsigned char)value;
        value >>= 8u;
    }
}

static int write_record(const char *path, uint64_t buttons)
{
    unsigned char record[INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE] = {0};
    struct timespec now;
    FILE *file;
    if (timespec_get(&now, TIME_UTC) != TIME_UTC) return 1;
    memcpy(record, "S64C", 4u);
    record[4] = 1u;
    write_be32(record + 8u, 1u);
    write_be64(record + 12u,
               (uint64_t)now.tv_sec * 1000u +
                   (uint64_t)now.tv_nsec / 1000000u);
    write_be64(record + 20u, buttons);
    file = fopen(path, "wb");
    if (file == NULL) return 1;
    if (fwrite(record, 1u, sizeof(record), file) != sizeof(record) ||
        fclose(file) != 0) {
        return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    unsigned char record[INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE] = {0};
    uint64_t buttons = 0u;
    if (argc == 4 && strcmp(argv[1], "--write") == 0) {
        char *end = NULL;
        unsigned long long parsed = strtoull(argv[3], &end, 0);
        if (end == argv[3] || *end != '\0' ||
            parsed > INTEGRAL_N64_RUNTIME_REMOTE_INPUT_BUTTON_MASK) return 2;
        return write_record(argv[2], (uint64_t)parsed);
    }
    if (argc != 1) return 2;
    memcpy(record, "S64C", 4u);
    record[4] = 1u;
    write_be32(record + 8u, 7u);
    write_be64(record + 12u, 10000u);
    write_be64(record + 20u, 0x153u);
    assert(integral_n64_runtime_remote_input_parse(record, sizeof(record), 10500u,
                                        &buttons) == 0);
    assert(buttons == 0x153u);
    write_be64(record + 20u, 1u << 14u);
    assert(integral_n64_runtime_remote_input_parse(record, sizeof(record), 10500u,
                                        &buttons) == 0);
    assert(buttons == (1u << 14u));

    assert(integral_n64_runtime_remote_input_parse(record, sizeof(record), 10751u,
                                        &buttons) != 0);
    assert(buttons == 0u);
    write_be64(record + 20u, 0x40000u);
    assert(integral_n64_runtime_remote_input_parse(record, sizeof(record), 10500u,
                                        &buttons) != 0);
    record[4] = 2u;
    assert(integral_n64_runtime_remote_input_parse(record, sizeof(record), 10500u,
                                        &buttons) != 0);
    puts("N64 Runtime remote Controller 2 input tests: OK");
    return 0;
}

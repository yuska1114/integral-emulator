/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "server_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(value)                                                          \
    do {                                                                      \
        if (!(value)) {                                                       \
            fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static int parse(int argc, char **argv)
{
    ServerOptions options;
    memset(&options, 0, sizeof(options));
    return integral_gb_runtime_server_parse_options(argc, argv, &options);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    char *same_rom[] = {
        "server", "--rom1", "same.gbc", "--save1", "slot1.sav",
        "--rom2", "same.gbc", "--save2", "slot2.sav",
    };
    CHECK(parse((int)(sizeof(same_rom) / sizeof(same_rom[0])), same_rom) == 0);

    char *same_save[] = {
        "server", "--rom1", "first.gbc", "--save1", "shared.sav",
        "--rom2", "second.gbc", "--save2", "shared.sav",
    };
    CHECK(parse((int)(sizeof(same_save) / sizeof(same_save[0])), same_save) != 0);

    const char *save_path = "server-options-shared-save.tmp";
    FILE *save = fopen(save_path, "wb");
    CHECK(save != NULL);
    CHECK(fputc(0, save) != EOF);
    CHECK(fclose(save) == 0);
    char *same_file[] = {
        "server", "--rom1", "first.gbc", "--save1",
        "server-options-shared-save.tmp", "--rom2", "second.gbc", "--save2",
        "./server-options-shared-save.tmp",
    };
    int same_file_result = parse(
        (int)(sizeof(same_file) / sizeof(same_file[0])), same_file);
    CHECK(remove(save_path) == 0);
    CHECK(same_file_result != 0);

    puts("server options same-ROM/save isolation test ok");
    return 0;
}

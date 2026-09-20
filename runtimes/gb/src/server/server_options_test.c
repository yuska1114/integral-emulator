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
    char *ir_options[] = {"server", "--self", "--rom1", "test.gbc", "--save1", "test.sav", "--ir-off-delay-ticks", "0"};
    ServerOptions ir = {0};
    CHECK(integral_gb_runtime_server_parse_options(8, ir_options, &ir) == 0);
    CHECK(ir.ir_off_delay_ticks == 0);
    ir_options[7] = "256";
    CHECK(integral_gb_runtime_server_parse_options(8, ir_options, &ir) == 0);
    CHECK(ir.ir_off_delay_ticks == 256);
    ir_options[7] = "257";
    CHECK(parse(8, ir_options) != 0);
    ir_options[7] = "-1";
    CHECK(parse(8, ir_options) != 0);
    ServerOptions auto_a_options;
    memset(&auto_a_options, 0, sizeof(auto_a_options));
    char *normal_play[] = {
        "server", "--self", "--rom1", "normal.gbc", "--save1", "normal.sav",
    };
    CHECK(integral_gb_runtime_server_parse_options(
        (int)(sizeof(normal_play) / sizeof(normal_play[0])), normal_play,
        &auto_a_options) == 0);
    CHECK(auto_a_options.auto_a_frames == 0);
    char *automatic_play[] = {
        "server", "--self", "--rom1", "normal.gbc", "--save1", "normal.sav",
        "--auto-a-frames", "3", "--auto-a-pulse", "2",
    };
    CHECK(integral_gb_runtime_server_parse_options(
        (int)(sizeof(automatic_play) / sizeof(automatic_play[0])), automatic_play,
        &auto_a_options) == 0);
    CHECK(auto_a_options.auto_a_frames == 3 && auto_a_options.auto_a_pulse == 2);

    char *same_rom[] = {
        "server", "--rom1", "same.gbc", "--save1", "slot1.sav",
        "--rom2", "same.gbc", "--save2", "slot2.sav",
    };
    CHECK(parse((int)(sizeof(same_rom) / sizeof(same_rom[0])), same_rom) == 0);
    ServerOptions local_pair = {0};
    CHECK(integral_gb_runtime_server_parse_options(
        (int)(sizeof(same_rom) / sizeof(same_rom[0])), same_rom, &local_pair) == 0);
    CHECK(!local_pair.remote_input_enabled);
    char *lan_pair[] = {
        "server", "--rom1", "same.gbc", "--save1", "slot1.sav",
        "--rom2", "same.gbc", "--save2", "slot2.sav",
        "--remote-input", "--bind", "127.0.0.1", "--port", "25100",
    };
    ServerOptions lan = {0};
    CHECK(integral_gb_runtime_server_parse_options(
        (int)(sizeof(lan_pair) / sizeof(lan_pair[0])), lan_pair, &lan) == 0);
    CHECK(lan.remote_input_enabled);
    CHECK(strcmp(lan.bind, "127.0.0.1") == 0 && lan.port == 25100);

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

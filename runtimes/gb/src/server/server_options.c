/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "server_options.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "display_scale.h"
#include "parse_util.h"
#include "protocol.h"

void integral_gb_runtime_server_print_usage(const char *program)
{
    printf("  --ir-off-delay-ticks 0..256: experimental SERVER2 IR release delay (default 32; 0 disables)\n");
    printf("Usage: %s --rom1 PATH [--rom2 PATH] --save1 PATH [--save2 PATH] [--self] [--display-slots 1|2] [--slot1-keys SPEC] [--slot2-keys SPEC] [--fast-key KEY] [--screenshot-key KEY] [--escape-key KEY] [--turbo-hold-key KEY] [--reset-key KEY] [--speed 1|2|3|4] [--auto-a-frames N] [--auto-a-pulse N] [--slot1-macro TEXT] [--slot2-macro TEXT] [--macro-step-frames N] [--macro-press-frames N] [--macro-screenshots] [--dump-screenshot] [--lan-remote] [--lan-remote-port PORT] [--rtc-offset-minutes N] [--rtc-offset-seconds N] [--smoke-screenshot] [--bind ADDR] [--port PORT] [--frames N] [--display] [--scale auto|1|2|3|4|5|6] [--window-width N --window-height N] [--audio] [--no-audio] [--audio-max-ms N] [--no-skip-boot-rom] [--no-link] [--remote-input] [--remote-input-dual] [--auth-token TOKEN] [--slot1-auth-token TOKEN] [--slot2-auth-token TOKEN] [--slot1-auth-token-file PATH] [--slot2-auth-token-file PATH]\n", program);
}

static bool read_auth_token_file(const char *path, char *dest, size_t dest_size)
{
    FILE *in = fopen(path, "rb");
    if (!in) {
        fprintf(stderr, "Failed to open auth token file '%s'\n", path);
        return false;
    }
    size_t n = fread(dest, 1, dest_size - 1, in);
    int extra = fgetc(in);
    bool too_long = extra != EOF;
    fclose(in);
    if (too_long) {
        fprintf(stderr, "Auth token file is too long: %s\n", path);
        return false;
    }
    while (n > 0 && (dest[n - 1] == '\n' || dest[n - 1] == '\r' || dest[n - 1] == ' ' || dest[n - 1] == '\t')) {
        n--;
    }
    dest[n] = '\0';
    return n > 0;
}

int integral_gb_runtime_server_parse_options(int argc, char **argv, ServerOptions *options)
{
    options->bind = "127.0.0.1";
    options->port = INTEGRAL_GB_RUNTIME_DEFAULT_PORT;
    options->scale = integral_display_scale_from_environment(
        "INTEGRAL_EMULATOR_DISPLAY_SCALE");
    options->skip_boot_rom = true;
    options->sgb_disabled = false;
    options->link_enabled = true;
    options->ir_off_delay_ticks = 32;
    options->audio_max_ms = 120;
    options->display_slots = 2;
    options->speed_multiplier = 1;
    options->lan_remote_port = INTEGRAL_GB_RUNTIME_LAN_REMOTE_DEFAULT_PORT;
    options->macro_step_frames = 60;
    options->macro_press_frames = 6;
    integral_gb_runtime_key_config_slot1_default(&options->slot1_keys);
    integral_gb_runtime_key_config_slot2_default(&options->slot2_keys);
    options->fast_key = integral_gb_runtime_key_config_fast_default();
    options->screenshot_key = integral_gb_runtime_key_config_screenshot_default();
    options->escape_key = integral_gb_runtime_key_config_escape_default();
    options->turbo_hold_key = integral_gb_runtime_key_config_turbo_hold_default();
    options->reset_key = integral_gb_runtime_key_config_reset_default();

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0) {
            integral_gb_runtime_server_print_usage(argv[0]);
            exit(0);
        }
        else if (strcmp(argv[i], "--rom1") == 0 && i + 1 < argc) {
            options->rom1 = argv[++i];
        }
        else if (strcmp(argv[i], "--rom2") == 0 && i + 1 < argc) {
            options->rom2 = argv[++i];
        }
        else if (strcmp(argv[i], "--save1") == 0 && i + 1 < argc) {
            options->save1 = argv[++i];
        }
        else if (strcmp(argv[i], "--save2") == 0 && i + 1 < argc) {
            options->save2 = argv[++i];
        }
        else if (strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
            options->bind = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 65535, &options->port) != 0) {
                fprintf(stderr, "Invalid --port value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 65535, &options->frames) != 0) {
                fprintf(stderr, "Invalid --frames value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--display") == 0) {
            options->display = true;
        }
        else if (strcmp(argv[i], "--no-skip-boot-rom") == 0) {
            options->skip_boot_rom = false;
        }
        else if (strcmp(argv[i], "--sgb") == 0 && i + 1 < argc) {
            const char *value = argv[++i];
            if (strcmp(value, "enable") == 0) options->sgb_disabled = false;
            else if (strcmp(value, "disable") == 0) options->sgb_disabled = true;
            else {
                fprintf(stderr, "Invalid --sgb value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--no-link") == 0) {
            options->link_enabled = false;
        }
        else if (strcmp(argv[i], "--remote-input") == 0) {
            options->remote_input_enabled = true;
        }
        else if (strcmp(argv[i], "--remote-input-dual") == 0) {
            options->remote_input_enabled = true;
            options->remote_input_dual_enabled = true;
        }
        else if (strcmp(argv[i], "--auth-token") == 0 && i + 1 < argc) {
            options->slot1_auth_token = argv[++i];
            options->slot2_auth_token = options->slot1_auth_token;
        }
        else if (strcmp(argv[i], "--slot1-auth-token") == 0 && i + 1 < argc) {
            options->slot1_auth_token = argv[++i];
        }
        else if (strcmp(argv[i], "--slot2-auth-token") == 0 && i + 1 < argc) {
            options->slot2_auth_token = argv[++i];
        }
        else if (strcmp(argv[i], "--slot1-auth-token-file") == 0 && i + 1 < argc) {
            options->slot1_auth_token_file = argv[++i];
        }
        else if (strcmp(argv[i], "--slot2-auth-token-file") == 0 && i + 1 < argc) {
            options->slot2_auth_token_file = argv[++i];
        }
        else if (strcmp(argv[i], "--lan-remote") == 0) {
            options->lan_remote_enabled = true;
            options->self_mode = true;
            options->link_enabled = false;
            options->remote_input_enabled = false;
            options->display_slots = 1;
        }
        else if (strcmp(argv[i], "--lan-remote-port") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 1, 65535, &options->lan_remote_port) != 0) {
                fprintf(stderr, "Invalid --lan-remote-port value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--self") == 0) {
            options->self_mode = true;
            options->link_enabled = false;
            options->remote_input_enabled = false;
            options->display_slots = 1;
        }
        else if (strcmp(argv[i], "--display-slots") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 65535, &options->display_slots) != 0 ||
                options->display_slots < 1 ||
                options->display_slots > 2) {
                fprintf(stderr, "Invalid --display-slots value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--slot1-keys") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_key_config_parse(&options->slot1_keys, argv[++i]) != 0) {
                fprintf(stderr, "Invalid --slot1-keys value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--slot2-keys") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_key_config_parse(&options->slot2_keys, argv[++i]) != 0) {
                fprintf(stderr, "Invalid --slot2-keys value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--fast-key") == 0 && i + 1 < argc) {
            options->fast_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
            if (options->fast_key == SDLK_UNKNOWN) {
                fprintf(stderr, "Invalid --fast-key value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--screenshot-key") == 0 && i + 1 < argc) {
            options->screenshot_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
            if (options->screenshot_key == SDLK_UNKNOWN) {
                fprintf(stderr, "Invalid --screenshot-key value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--escape-key") == 0 && i + 1 < argc) {
            options->escape_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
            if (options->escape_key == SDLK_UNKNOWN) {
                fprintf(stderr, "Invalid --escape-key value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--turbo-hold-key") == 0 && i + 1 < argc) {
            options->turbo_hold_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
            if (options->turbo_hold_key == SDLK_UNKNOWN) {
                fprintf(stderr, "Invalid --turbo-hold-key value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--reset-key") == 0 && i + 1 < argc) {
            options->reset_key = integral_gb_runtime_key_config_key_from_name(argv[++i]);
            if (options->reset_key == SDLK_UNKNOWN) {
                fprintf(stderr, "Invalid --reset-key value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 1, 4, &options->speed_multiplier) != 0) {
                fprintf(stderr, "Invalid --speed value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--ir-off-delay-ticks") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 256, &options->ir_off_delay_ticks) != 0) return -1;
        }
        else if (strcmp(argv[i], "--auto-a-frames") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 65535, &options->auto_a_frames) != 0) {
                fprintf(stderr, "Invalid --auto-a-frames value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--auto-a-pulse") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 65535, &options->auto_a_pulse) != 0) {
                fprintf(stderr, "Invalid --auto-a-pulse value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--slot1-macro") == 0 && i + 1 < argc) {
            options->slot1_macro = argv[++i];
        }
        else if (strcmp(argv[i], "--slot2-macro") == 0 && i + 1 < argc) {
            options->slot2_macro = argv[++i];
        }
        else if (strcmp(argv[i], "--macro-step-frames") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 1, 65535, &options->macro_step_frames) != 0) {
                fprintf(stderr, "Invalid --macro-step-frames value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--macro-press-frames") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 1, 65535, &options->macro_press_frames) != 0) {
                fprintf(stderr, "Invalid --macro-press-frames value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--macro-screenshots") == 0) {
            options->macro_screenshots = true;
        }
        else if (strcmp(argv[i], "--dump-screenshot") == 0) {
            options->dump_screenshot = true;
        }
        else if (strcmp(argv[i], "--rtc-offset-minutes") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_int_range(argv[++i], -32768, 32767, &options->rtc_offset_minutes) != 0 ||
                options->rtc_offset_minutes < -(24 * 60) ||
                options->rtc_offset_minutes > 24 * 60) {
                fprintf(stderr, "Invalid --rtc-offset-minutes value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--rtc-offset-seconds") == 0 && i + 1 < argc) {
            char *end = NULL;
            long long value = strtoll(argv[++i], &end, 10);
            if (!end || *end != '\0' || value < -315360000LL || value > 315360000LL) {
                fprintf(stderr, "Invalid --rtc-offset-seconds value\n");
                return -1;
            }
            options->rtc_offset_seconds = (int64_t)value;
        }
        else if (strcmp(argv[i], "--smoke-screenshot") == 0) {
            options->smoke_screenshot = true;
        }
        else if (strcmp(argv[i], "--audio") == 0) {
            options->local_audio_enabled = true;
            options->local_audio_option_set = true;
        }
        else if (strcmp(argv[i], "--no-audio") == 0) {
            options->local_audio_enabled = false;
            options->local_audio_option_set = true;
        }
        else if (strcmp(argv[i], "--audio-max-ms") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 0, 65535, &options->audio_max_ms) != 0 ||
                options->audio_max_ms < 20) {
                fprintf(stderr, "Invalid --audio-max-ms value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            if (integral_display_scale_parse(argv[++i], &options->scale) != 0) {
                fprintf(stderr, "Invalid --scale value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--window-width") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 160, 16384,
                                                     &options->window_width) != 0) {
                fprintf(stderr, "Invalid --window-width value\n");
                return -1;
            }
        }
        else if (strcmp(argv[i], "--window-height") == 0 && i + 1 < argc) {
            if (integral_gb_runtime_parse_uint_range(argv[++i], 144, 16384,
                                                     &options->window_height) != 0) {
                fprintf(stderr, "Invalid --window-height value\n");
                return -1;
            }
        }
        else {
            fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
            return -1;
        }
    }

    if (!options->rom1 || !options->save1) {
        fprintf(stderr, "Missing required ROM/save path\n");
        return -1;
    }
    if (!options->self_mode && !options->remote_input_enabled && (!options->rom2 || !options->save2)) {
        fprintf(stderr, "Missing required Slot 2 ROM/save path\n");
        return -1;
    }
    if (!options->self_mode && options->rom2 && !options->save2) {
        fprintf(stderr, "Missing required Slot 2 save path\n");
        return -1;
    }
    if (!options->self_mode && options->save2 &&
        (strcmp(options->save1, options->save2) == 0 ||
         integral_gb_runtime_paths_refer_to_same_regular_file(options->save1,
                                                               options->save2))) {
        fprintf(stderr, "Slot 1 and Slot 2 saves must be different files\n");
        return -1;
    }
    if (options->self_mode) {
        options->link_enabled = false;
        options->remote_input_enabled = false;
        options->remote_input_dual_enabled = false;
        options->display_slots = 1;
    }
    if ((options->window_width == 0u) != (options->window_height == 0u) ||
        (options->window_width != 0u &&
         options->window_width < INTEGRAL_GB_RUNTIME_GB_WIDTH * options->display_slots)) {
        fprintf(stderr, "GB target window size must contain the selected display slots\n");
        return -1;
    }
    if (options->remote_input_dual_enabled && (!options->remote_input_enabled || !options->rom2)) {
        fprintf(stderr, "--remote-input-dual requires --remote-input and --rom2\n");
        return -1;
    }
    if (!options->local_audio_option_set) {
        options->local_audio_enabled = options->display;
    }
    if (options->lan_remote_enabled) {
        options->local_audio_enabled = false;
    }
    static char slot1_auth_token_from_file[INTEGRAL_GB_RUNTIME_AUTH_TOKEN_SIZE + 1u];
    static char slot2_auth_token_from_file[INTEGRAL_GB_RUNTIME_AUTH_TOKEN_SIZE + 1u];
    if (options->slot1_auth_token_file) {
        if (!read_auth_token_file(options->slot1_auth_token_file,
                                  slot1_auth_token_from_file,
                                  sizeof(slot1_auth_token_from_file))) {
            return -1;
        }
        options->slot1_auth_token = slot1_auth_token_from_file;
    }
    if (options->slot2_auth_token_file) {
        if (!read_auth_token_file(options->slot2_auth_token_file,
                                  slot2_auth_token_from_file,
                                  sizeof(slot2_auth_token_from_file))) {
            return -1;
        }
        options->slot2_auth_token = slot2_auth_token_from_file;
    }
    return 0;
}

const char *integral_gb_runtime_server_window_mode_name(const ServerOptions *options)
{
    if (options->lan_remote_enabled) {
        return "lan remote";
    }
    if (options->self_mode) {
        return "self";
    }
    return options->display_slots == 1 ? "server1" : "server2";
}

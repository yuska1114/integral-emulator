/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SERVER_OPTIONS_H
#define INTEGRAL_GB_RUNTIME_SERVER_OPTIONS_H

#include <stdbool.h>
#include <stdint.h>

#include <SDL.h>

#include "key_config.h"

typedef struct ServerOptions {
    const char *rom1;
    const char *rom2;
    const char *save1;
    const char *save2;
    const char *slot1_auth_token;
    const char *slot2_auth_token;
    const char *slot1_auth_token_file;
    const char *slot2_auth_token_file;
    const char *bind;
    unsigned port;
    unsigned frames;
    bool display;
    unsigned scale;
    bool skip_boot_rom;
    bool link_enabled;
    bool remote_input_enabled;
    bool remote_input_dual_enabled;
    bool local_audio_enabled;
    bool local_audio_option_set;
    bool self_mode;
    bool lan_remote_enabled;
    bool smoke_screenshot;
    unsigned audio_max_ms;
    unsigned display_slots;
    unsigned lan_remote_port;
    unsigned speed_multiplier;
    unsigned auto_a_frames;
    unsigned auto_a_pulse;
    const char *slot1_macro;
    const char *slot2_macro;
    unsigned macro_step_frames;
    unsigned macro_press_frames;
    bool macro_screenshots;
    bool dump_screenshot;
    int rtc_offset_minutes;
    int64_t rtc_offset_seconds;
    IntegralGBRuntimeKeyConfig slot1_keys;
    IntegralGBRuntimeKeyConfig slot2_keys;
    SDL_Keycode fast_key;
    SDL_Keycode screenshot_key;
    SDL_Keycode escape_key;
    SDL_Keycode turbo_hold_key;
    SDL_Keycode reset_key;
} ServerOptions;

void integral_gb_runtime_server_print_usage(const char *program);
int integral_gb_runtime_server_parse_options(int argc, char **argv, ServerOptions *options);
const char *integral_gb_runtime_server_window_mode_name(const ServerOptions *options);

#endif

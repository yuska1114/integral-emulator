/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_APP_MENU_STATE_H
#define INTEGRAL_GB_RUNTIME_APP_MENU_STATE_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>

#include "choice_list.h"
#include "key_config.h"
#include "menu_paths.h"

enum {
    INTEGRAL_GB_RUNTIME_MENU_ROWS = 10,
    INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS = 5,
};

typedef enum IntegralGBRuntimeMode {
    INTEGRAL_GB_RUNTIME_MODE_SELF,
    INTEGRAL_GB_RUNTIME_MODE_SERVER1,
    INTEGRAL_GB_RUNTIME_MODE_SERVER2,
    INTEGRAL_GB_RUNTIME_MODE_CLIENT,
    INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE,
    INTEGRAL_GB_RUNTIME_MODE_SCREENSHOT_VIEW,
    INTEGRAL_GB_RUNTIME_MODE_KEY_CONFIG,
} IntegralGBRuntimeMode;

typedef enum KeyConfigTarget {
    KEY_CONFIG_NONE,
    KEY_CONFIG_SLOT1,
    KEY_CONFIG_SLOT2,
    KEY_CONFIG_UTILS,
} KeyConfigTarget;

typedef struct IntegralGBRuntimeMenu {
    ChoiceList roms;
    char slot1_rom[INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
    char slot2_rom[INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
    char host[128];
    char port[16];
    char slot1_keys[256];
    char slot2_keys[256];
    char fast_key[64];
    char screenshot_key[64];
    char escape_key[64];
    char turbo_hold_key[64];
    char reset_key[64];
    IntegralGBRuntimeKeyConfig slot1_key_config;
    IntegralGBRuntimeKeyConfig slot2_key_config;
    SDL_Keycode fast_keycode;
    SDL_Keycode screenshot_keycode;
    SDL_Keycode escape_keycode;
    SDL_Keycode turbo_hold_keycode;
    SDL_Keycode reset_keycode;
    int rtc_offset_minutes;
    unsigned speed_multiplier;
    bool client_sends_slot2_paths;
    bool client_auto_discover;
    IntegralGBRuntimeMode mode;
    unsigned selected_row;
    bool editing;
    bool quit_confirm;
    bool quit_confirm_yes;
    KeyConfigTarget key_config_target;
    unsigned key_config_step;
    bool controller_capture_wait_release;
    SDL_Keycode controller_capture_release_key;
    IntegralGBRuntimeKeyConfig pending_key_config;
    char status[256];
} IntegralGBRuntimeMenu;

const char *integral_gb_runtime_menu_mode_name(IntegralGBRuntimeMode mode);
char *integral_gb_runtime_menu_row_value(IntegralGBRuntimeMenu *menu, unsigned row);
size_t integral_gb_runtime_menu_row_value_capacity(unsigned row);
void integral_gb_runtime_menu_append_text(char *value, size_t capacity, const char *text);
void integral_gb_runtime_menu_cycle_selected(IntegralGBRuntimeMenu *menu, int direction);
void integral_gb_runtime_menu_format_rtc_offset(int offset_minutes, char *out, size_t out_size);
bool integral_gb_runtime_menu_validate_launch_selection(IntegralGBRuntimeMenu *menu);

#endif

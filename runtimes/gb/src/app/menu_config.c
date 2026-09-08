/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "menu_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "file_util.h"
#include "key_config.h"
#include "menu_paths.h"
#include "string_util.h"

#define DEFAULT_KEY_CONFIG_FILE "config/gb_runtime_keys.conf"

static void trim_line(char *text)
{
    char *start = text;
    while (*start == ' ' || *start == '\t') {
        start++;
    }
    if (start != text) {
        memmove(text, start, strlen(start) + 1);
    }

    size_t len = strlen(text);
    while (len > 0 &&
           (text[len - 1] == '\n' ||
            text[len - 1] == '\r' ||
            text[len - 1] == ' ' ||
            text[len - 1] == '\t')) {
        text[--len] = '\0';
    }
}

const char *integral_gb_runtime_menu_config_path(void)
{
    const char *path = getenv("GB_RUNTIME_KEY_CONFIG");
    return path && path[0] != '\0' ? path : DEFAULT_KEY_CONFIG_FILE;
}

void integral_gb_runtime_menu_load_config_file(IntegralGBRuntimeMenu *menu)
{
    FILE *in = fopen(integral_gb_runtime_menu_config_path(), "r");
    if (!in) {
        return;
    }

    char line[512];
    while (fgets(line, sizeof(line), in)) {
        trim_line(line);
        if (line[0] == '\0' || line[0] == '#') {
            continue;
        }

        char *equals = strchr(line, '=');
        if (!equals) {
            continue;
        }
        *equals = '\0';
        char *name = line;
        char *value = equals + 1;
        trim_line(name);
        trim_line(value);

        IntegralGBRuntimeKeyConfig parsed;
        if (strcmp(name, "slot1_keys") == 0) {
            if (integral_gb_runtime_key_config_parse(&parsed, value) == 0) {
                menu->slot1_key_config = parsed;
            }
        }
        else if (strcmp(name, "slot2_keys") == 0) {
            if (integral_gb_runtime_key_config_parse(&parsed, value) == 0) {
                menu->slot2_key_config = parsed;
            }
        }
        else if (strcmp(name, "fast_key") == 0) {
            SDL_Keycode key = integral_gb_runtime_key_config_key_from_name(value);
            if (key != SDLK_UNKNOWN) {
                menu->fast_keycode = key;
            }
        }
        else if (strcmp(name, "screenshot_key") == 0) {
            SDL_Keycode key = integral_gb_runtime_key_config_key_from_name(value);
            if (key != SDLK_UNKNOWN) {
                menu->screenshot_keycode = key;
            }
        }
        else if (strcmp(name, "escape_key") == 0) {
            SDL_Keycode key = integral_gb_runtime_key_config_key_from_name(value);
            if (key != SDLK_UNKNOWN) {
                menu->escape_keycode = key;
            }
        }
        else if (strcmp(name, "turbo_hold_key") == 0) {
            SDL_Keycode key = integral_gb_runtime_key_config_key_from_name(value);
            if (key != SDLK_UNKNOWN) {
                menu->turbo_hold_keycode = key;
            }
        }
        else if (strcmp(name, "reset_key") == 0) {
            SDL_Keycode key = integral_gb_runtime_key_config_key_from_name(value);
            if (key != SDLK_UNKNOWN) {
                menu->reset_keycode = key;
            }
        }
        else if (strcmp(name, "recent_slot1_rom") == 0) {
            if (integral_gb_runtime_menu_rom_filename_valid(value)) {
                (void)integral_gb_runtime_copy_text(menu->slot1_rom, sizeof(menu->slot1_rom), value);
            }
        }
    }
    fclose(in);
}

static int ensure_config_parent_directory(const char *path)
{
    char parent[INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
    if (!integral_gb_runtime_copy_text(parent, sizeof(parent), path)) {
        return -1;
    }
    char *slash = strrchr(parent, '/');
    if (!slash) {
        return 0;
    }
    *slash = '\0';
    if (parent[0] == '\0') {
        return 0;
    }
    return integral_gb_runtime_ensure_directory(parent, 0755);
}

int integral_gb_runtime_menu_save_config_file(const IntegralGBRuntimeMenu *menu)
{
    const char *path = integral_gb_runtime_menu_config_path();
    if (ensure_config_parent_directory(path) != 0) {
        return -1;
    }

    FILE *out = fopen(path, "w");
    if (!out) {
        return -1;
    }
    fprintf(out, "# INTEGRAL EMULATOR GB Runtime key settings\n");
    fprintf(out, "slot1_keys=%s\n", menu->slot1_keys);
    fprintf(out, "slot2_keys=%s\n", menu->slot2_keys);
    fprintf(out, "fast_key=%s\n", menu->fast_key);
    fprintf(out, "screenshot_key=%s\n", menu->screenshot_key);
    fprintf(out, "escape_key=%s\n", menu->escape_key);
    fprintf(out, "turbo_hold_key=%s\n", menu->turbo_hold_key);
    fprintf(out, "reset_key=%s\n", menu->reset_key);
    fprintf(out, "recent_slot1_rom=%s\n", menu->slot1_rom);
    int result = ferror(out) ? -1 : 0;
    if (fclose(out) != 0) {
        result = -1;
    }
    return result;
}

void integral_gb_runtime_menu_refresh_key_descriptions(IntegralGBRuntimeMenu *menu)
{
    integral_gb_runtime_key_config_describe(&menu->slot1_key_config, menu->slot1_keys, sizeof(menu->slot1_keys));
    integral_gb_runtime_key_config_describe(&menu->slot2_key_config, menu->slot2_keys, sizeof(menu->slot2_keys));
    (void)integral_gb_runtime_copy_text(menu->fast_key,
                              sizeof(menu->fast_key),
                              integral_gb_runtime_key_config_key_name(menu->fast_keycode));
    (void)integral_gb_runtime_copy_text(menu->screenshot_key,
                              sizeof(menu->screenshot_key),
                              integral_gb_runtime_key_config_key_name(menu->screenshot_keycode));
    (void)integral_gb_runtime_copy_text(menu->escape_key,
                              sizeof(menu->escape_key),
                              integral_gb_runtime_key_config_key_name(menu->escape_keycode));
    (void)integral_gb_runtime_copy_text(menu->turbo_hold_key,
                              sizeof(menu->turbo_hold_key),
                              integral_gb_runtime_key_config_key_name(menu->turbo_hold_keycode));
    (void)integral_gb_runtime_copy_text(menu->reset_key,
                              sizeof(menu->reset_key),
                              integral_gb_runtime_key_config_key_name(menu->reset_keycode));
}

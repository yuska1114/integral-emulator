/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#endif

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static void strip_newline(char *text)
{
    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == '\n' || text[len - 1] == '\r')) {
        text[--len] = '\0';
    }
}

static void ensure_parent_directory(const char *path)
{
    char dir[256];
    copy_text(dir, sizeof(dir), path);
    char *slash = strrchr(dir, '/');
    if (!slash) {
        return;
    }
    *slash = '\0';
    if (dir[0] != '\0') {
#ifdef _WIN32
        (void)_mkdir(dir);
#else
        (void)mkdir(dir, 0755);
#endif
    }
}

static int load_config_file(const char *path,
                            IntegralConfigLogin *login,
                            IntegralConfigLocal *local,
                            IntegralConfigWindow *window,
                            IntegralConfigKeys *keys,
                            IntegralConfigRomSlot *slots,
                            size_t slot_count)
{
    if (login) {
        memset(login, 0, sizeof(*login));
    }
    if (slots) {
        memset(slots, 0, sizeof(slots[0]) * slot_count);
    }
    if (keys) {
        memset(keys, 0, sizeof(*keys));
    }
    if (local) {
        local->slot1_index = -1;
        local->slot2_index = -1;
        local->ir_off_delay_ticks = 32;
    }
    if (window) {
        window->width = INTEGRAL_CONFIG_WINDOW_DEFAULT_WIDTH;
        window->height = INTEGRAL_CONFIG_WINDOW_DEFAULT_HEIGHT;
    }

    FILE *file = fopen(path, "r");
    if (!file) {
        return errno == ENOENT ? 1 : -1;
    }

    char line[4096];
    while (fgets(line, sizeof(line), file)) {
        strip_newline(line);
        if (login) {
            char value[2048];
            if (sscanf(line, "login.remember=%2047[^\n]", value) == 1) {
                login->remember = strcmp(value, "1") == 0;
                continue;
            }
            if (sscanf(line, "login.server=%2047[^\n]", value) == 1) {
                copy_text(login->server, sizeof(login->server), value);
                continue;
            }
            if (sscanf(line, "login.server_id=%2047[^\n]", value) == 1) {
                copy_text(login->server_id, sizeof(login->server_id), value);
                continue;
            }
            if (sscanf(line, "login.username=%2047[^\n]", value) == 1) {
                copy_text(login->username, sizeof(login->username), value);
                continue;
            }
        }
        if (local) {
            int value = -1;
            const char *prefix = "gb.ir_off_delay_ticks=";
            if (strncmp(line, prefix, strlen(prefix)) == 0) {
                char *end;
                const char *start = line + strlen(prefix);
                errno = 0;
                unsigned long ticks = strtoul(start, &end, 10);
                if (end != start && *end == '\0' && errno == 0 && ticks <= 256)
                    local->ir_off_delay_ticks = (unsigned)ticks;
                continue;
            }
            if (sscanf(line, "local.slot1_index=%d", &value) == 1) {
                local->slot1_index = value;
                continue;
            }
            if (sscanf(line, "local.slot2_index=%d", &value) == 1) {
                local->slot2_index = value;
                continue;
            }
        }
        if (window) {
            unsigned value = 0u;
            if (sscanf(line, "window.width=%u", &value) == 1) {
                if (value >= INTEGRAL_CONFIG_WINDOW_MIN_WIDTH &&
                    value <= INTEGRAL_CONFIG_WINDOW_MAX_SIZE) {
                    window->width = value;
                }
                continue;
            }
            if (sscanf(line, "window.height=%u", &value) == 1) {
                if (value >= INTEGRAL_CONFIG_WINDOW_MIN_HEIGHT &&
                    value <= INTEGRAL_CONFIG_WINDOW_MAX_SIZE) {
                    window->height = value;
                }
                continue;
            }
        }
        if (keys) {
            char value[2048];
            if (strncmp(line, "keys.n64_p2=", 12) == 0) {
                copy_text(keys->n64_p2, sizeof(keys->n64_p2), line + 12);
                continue;
            }
            if (strncmp(line, "keys.n64_p3=", 12) == 0) {
                copy_text(keys->n64_p3, sizeof(keys->n64_p3), line + 12);
                continue;
            }
            if (strncmp(line, "keys.n64_p4=", 12) == 0) {
                copy_text(keys->n64_p4, sizeof(keys->n64_p4), line + 12);
                continue;
            }
            if (sscanf(line, "keys.slot1=%2047[^\n]", value) == 1) {
                copy_text(keys->slot1, sizeof(keys->slot1), value);
                continue;
            }
            if (sscanf(line, "keys.slot2=%2047[^\n]", value) == 1) {
                copy_text(keys->slot2, sizeof(keys->slot2), value);
                continue;
            }
            if (sscanf(line, "keys.n64_p1=%2047[^\n]", value) == 1) {
                copy_text(keys->n64_p1, sizeof(keys->n64_p1), value);
                continue;
            }
            if (sscanf(line, "keys.fast=%2047[^\n]", value) == 1) {
                copy_text(keys->fast, sizeof(keys->fast), value);
                continue;
            }
            if (sscanf(line, "keys.screenshot=%2047[^\n]", value) == 1) {
                copy_text(keys->screenshot, sizeof(keys->screenshot), value);
                continue;
            }
            if (sscanf(line, "keys.escape=%2047[^\n]", value) == 1) {
                copy_text(keys->escape, sizeof(keys->escape), value);
                continue;
            }
            if (sscanf(line, "keys.turbo_hold=%2047[^\n]", value) == 1) {
                copy_text(keys->turbo_hold, sizeof(keys->turbo_hold), value);
                continue;
            }
            if (sscanf(line, "keys.reset=%2047[^\n]", value) == 1) {
                copy_text(keys->reset, sizeof(keys->reset), value);
                continue;
            }
            if (strncmp(line, "keys.client_alias_right=", 24) == 0) {
                copy_text(keys->client_alias_right, sizeof(keys->client_alias_right), line + 24);
                continue;
            }
            if (strncmp(line, "keys.client_alias_left=", 23) == 0) {
                copy_text(keys->client_alias_left, sizeof(keys->client_alias_left), line + 23);
                continue;
            }
            if (strncmp(line, "keys.client_alias_up=", 21) == 0) {
                copy_text(keys->client_alias_up, sizeof(keys->client_alias_up), line + 21);
                continue;
            }
            if (strncmp(line, "keys.client_alias_down=", 23) == 0) {
                copy_text(keys->client_alias_down, sizeof(keys->client_alias_down), line + 23);
                continue;
            }
            if (strncmp(line, "keys.client_alias_enter=", 24) == 0) {
                copy_text(keys->client_alias_enter, sizeof(keys->client_alias_enter), line + 24);
                continue;
            }
            if (strncmp(line, "keys.client_alias_escape=", 25) == 0) {
                copy_text(keys->client_alias_escape, sizeof(keys->client_alias_escape), line + 25);
                continue;
            }
        }
        if (slots) {
            unsigned index = 0;
            char key[32];
            char value[256];
            if (sscanf(line, "slot%u.%31[^=]=%255[^\n]", &index, key, value) != 3) {
                continue;
            }
            if (index == 0 || index > slot_count) {
                continue;
            }
            IntegralConfigRomSlot *slot = &slots[index - 1];
            if (strcmp(key, "rom_path") == 0) {
                copy_text(slot->rom_path, sizeof(slot->rom_path), value);
            }
            else if (strcmp(key, "rom_id") == 0) {
                copy_text(slot->rom_id, sizeof(slot->rom_id), value);
            }
            else if (strcmp(key, "save_id") == 0) {
                copy_text(slot->save_id, sizeof(slot->save_id), value);
            }
        }
    }
    fclose(file);
    return 0;
}

static int replace_file(const char *temporary_path, const char *path)
{
#ifdef _WIN32
    return MoveFileExA(temporary_path,
                       path,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? 0
               : -1;
#else
    return rename(temporary_path, path);
#endif
}

static int save_config_file(const char *path,
                            const IntegralConfigLogin *login,
                            const IntegralConfigLocal *local,
                            const IntegralConfigWindow *window,
                            const IntegralConfigKeys *keys,
                            const IntegralConfigRomSlot *slots,
                            size_t slot_count)
{
    ensure_parent_directory(path);
    char temporary_path[INTEGRAL_CONFIG_PATH_MAX * 2u];
    if (snprintf(temporary_path, sizeof(temporary_path), "%s.part", path) >=
        (int)sizeof(temporary_path)) {
        return -1;
    }
    FILE *file = fopen(temporary_path, "w");
    if (!file) {
        return -1;
    }

    fprintf(file, "# INTEGRAL EMULATOR client config\n");
    if (login) {
        fprintf(file, "login.server=%s\n", login->server);
        fprintf(file, "login.server_id=%s\n", login->server_id[0] ? login->server_id : "primary");
    }
    if (login && login->remember) {
        fprintf(file, "login.remember=1\n");
        fprintf(file, "login.username=%s\n", login->username);
    }
    else {
        fprintf(file, "login.remember=0\n");
    }
    if (local) {
        fprintf(file, "local.slot1_index=%d\n", local->slot1_index);
        fprintf(file, "local.slot2_index=%d\n", local->slot2_index);
        fprintf(file, "gb.ir_off_delay_ticks=%u\n", local->ir_off_delay_ticks);
    }
    if (window) {
        fprintf(file, "window.width=%u\n", window->width);
        fprintf(file, "window.height=%u\n", window->height);
    }
    if (keys) {
        fprintf(file, "keys.slot1=%s\n", keys->slot1);
        fprintf(file, "keys.slot2=%s\n", keys->slot2);
        fprintf(file, "keys.n64_p1=%s\n", keys->n64_p1);
        fprintf(file, "keys.n64_p2=%s\n", keys->n64_p2);
        fprintf(file, "keys.n64_p3=%s\n", keys->n64_p3);
        fprintf(file, "keys.n64_p4=%s\n", keys->n64_p4);
        fprintf(file, "keys.fast=%s\n", keys->fast);
        fprintf(file, "keys.screenshot=%s\n", keys->screenshot);
        fprintf(file, "keys.escape=%s\n", keys->escape);
        fprintf(file, "keys.turbo_hold=%s\n", keys->turbo_hold);
        fprintf(file, "keys.reset=%s\n", keys->reset);
        fprintf(file, "keys.client_alias_right=%s\n", keys->client_alias_right);
        fprintf(file, "keys.client_alias_left=%s\n", keys->client_alias_left);
        fprintf(file, "keys.client_alias_up=%s\n", keys->client_alias_up);
        fprintf(file, "keys.client_alias_down=%s\n", keys->client_alias_down);
        fprintf(file, "keys.client_alias_enter=%s\n", keys->client_alias_enter);
        fprintf(file, "keys.client_alias_escape=%s\n", keys->client_alias_escape);
    }

    for (size_t i = 0; i < slot_count; i++) {
        const IntegralConfigRomSlot *slot = &slots[i];
        fprintf(file, "slot%zu.rom_path=%s\n", i + 1, slot->rom_path);
        fprintf(file, "slot%zu.rom_id=%s\n", i + 1, slot->rom_id);
        fprintf(file, "slot%zu.save_id=%s\n", i + 1, slot->save_id);
    }
    if (fclose(file) != 0 || replace_file(temporary_path, path) != 0) {
        (void)remove(temporary_path);
        return -1;
    }
    return 0;
}

int integral_config_load_login(const char *path, IntegralConfigLogin *login)
{
    IntegralConfigLogin loaded;
    (void)load_config_file(path, &loaded, NULL, NULL, NULL, NULL, 0);
    *login = loaded;
    return 0;
}

int integral_config_save_login(const char *path, const IntegralConfigLogin *login)
{
    IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS];
    IntegralConfigKeys keys;
    IntegralConfigLocal local;
    IntegralConfigWindow window;
    (void)load_config_file(path, NULL, &local, &window, &keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
    return save_config_file(path, login, &local, &window, &keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
}

int integral_config_load_local(const char *path, IntegralConfigLocal *local)
{
    IntegralConfigLocal loaded;
    (void)load_config_file(path, NULL, &loaded, NULL, NULL, NULL, 0);
    *local = loaded;
    return 0;
}

int integral_config_save_local(const char *path, const IntegralConfigLocal *local)
{
    IntegralConfigLogin login;
    IntegralConfigKeys keys;
    IntegralConfigWindow window;
    IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS];
    (void)load_config_file(path, &login, NULL, &window, &keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
    return save_config_file(path, &login, local, &window, &keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
}

unsigned integral_config_ir_off_delay(const char *path)
{
    IntegralConfigLocal local;
    if (!path) return 32;
    integral_config_load_local(path, &local);
    return local.ir_off_delay_ticks;
}

int integral_config_load_window(const char *path, IntegralConfigWindow *window)
{
    IntegralConfigWindow loaded;
    (void)load_config_file(path, NULL, NULL, &loaded, NULL, NULL, 0);
    *window = loaded;
    return 0;
}

int integral_config_save_window(const char *path, const IntegralConfigWindow *window)
{
    IntegralConfigLogin login;
    IntegralConfigLocal local;
    IntegralConfigKeys keys;
    IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS];
    (void)load_config_file(path, &login, &local, NULL, &keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
    return save_config_file(path, &login, &local, window, &keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
}

int integral_config_load_keys(const char *path, IntegralConfigKeys *keys)
{
    IntegralConfigKeys loaded;
    (void)load_config_file(path, NULL, NULL, NULL, &loaded, NULL, 0);
    *keys = loaded;
    return 0;
}

int integral_config_save_keys(const char *path, const IntegralConfigKeys *keys)
{
    IntegralConfigLogin login;
    IntegralConfigLocal local;
    IntegralConfigWindow window;
    IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS];
    (void)load_config_file(path, &login, &local, &window, NULL, slots, INTEGRAL_CONFIG_ROM_SLOTS);
    return save_config_file(path, &login, &local, &window, keys, slots, INTEGRAL_CONFIG_ROM_SLOTS);
}

int integral_config_load_rom_slots(const char *path, IntegralConfigRomSlot *slots, size_t slot_count)
{
    int result = load_config_file(path, NULL, NULL, NULL, NULL, slots, slot_count);
    return result < 0 ? -1 : 0;
}

int integral_config_save_rom_slots(const char *path, const IntegralConfigRomSlot *slots, size_t slot_count)
{
    IntegralConfigLogin login;
    IntegralConfigKeys keys;
    IntegralConfigLocal local;
    IntegralConfigWindow window;
    (void)load_config_file(path, &login, &local, &window, &keys, NULL, 0);
    return save_config_file(path, &login, &local, &window, &keys, slots, slot_count);
}

void integral_keys_defaults(IntegralConfigKeys *keys)
{
    memset(keys, 0, sizeof(*keys));
    integral_keys_reset_editable_defaults(keys);
}

void integral_keys_reset_editable_defaults(IntegralConfigKeys *keys)
{
    copy_text(keys->slot1, sizeof(keys->slot1), "RIGHT,LEFT,UP,DOWN,Z,X,RSHIFT,RETURN");
    copy_text(keys->slot2, sizeof(keys->slot2), "D,A,W,S,G,H,R,T");
    copy_text(keys->n64_p1, sizeof(keys->n64_p1), "D,A,W,S,RETURN,Z,LCTRL,LSHIFT,L,J,I,K,C,X,RIGHT,LEFT,UP,DOWN");
    copy_text(keys->fast, sizeof(keys->fast), "F");
    copy_text(keys->screenshot, sizeof(keys->screenshot), "P");
    copy_text(keys->escape, sizeof(keys->escape), "ESCAPE");
    copy_text(keys->turbo_hold, sizeof(keys->turbo_hold), "B");
    copy_text(keys->reset, sizeof(keys->reset), "O");
    keys->client_alias_right[0] = '\0';
    keys->client_alias_left[0] = '\0';
    keys->client_alias_up[0] = '\0';
    keys->client_alias_down[0] = '\0';
    keys->client_alias_enter[0] = '\0';
    keys->client_alias_escape[0] = '\0';
}

void integral_keys_apply_defaults_for_missing(IntegralConfigKeys *keys)
{
    IntegralConfigKeys defaults;
    integral_keys_defaults(&defaults);
    if (keys->slot1[0] == '\0') {
        copy_text(keys->slot1, sizeof(keys->slot1), defaults.slot1);
    }
    if (keys->slot2[0] == '\0') {
        copy_text(keys->slot2, sizeof(keys->slot2), defaults.slot2);
    }
    if (keys->n64_p1[0] == '\0') {
        copy_text(keys->n64_p1, sizeof(keys->n64_p1), defaults.n64_p1);
    }
    if (keys->fast[0] == '\0') {
        copy_text(keys->fast, sizeof(keys->fast), defaults.fast);
    }
    if (keys->screenshot[0] == '\0') {
        copy_text(keys->screenshot, sizeof(keys->screenshot), defaults.screenshot);
    }
    if (keys->escape[0] == '\0') {
        copy_text(keys->escape, sizeof(keys->escape), defaults.escape);
    }
    if (keys->turbo_hold[0] == '\0') {
        copy_text(keys->turbo_hold, sizeof(keys->turbo_hold), defaults.turbo_hold);
    }
    if (keys->reset[0] == '\0') {
        copy_text(keys->reset, sizeof(keys->reset), defaults.reset);
    }
}

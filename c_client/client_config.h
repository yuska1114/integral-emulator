/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_LEAGUE_CLIENT_CONFIG_H
#define INTEGRAL_LEAGUE_CLIENT_CONFIG_H

#include <stddef.h>

#define INTEGRAL_CONFIG_ROM_SLOTS 8
#define INTEGRAL_CONFIG_PATH_MAX 1024
#define INTEGRAL_CONFIG_ID_MAX 96
#define INTEGRAL_CONFIG_KEY_SPEC_MAX 1536
#define INTEGRAL_CONFIG_KEY_NAME_MAX 96
#define INTEGRAL_CONFIG_DEFAULT_PATH "config/integral_client.conf"
#define INTEGRAL_CONFIG_WINDOW_DEFAULT_WIDTH 360u
#define INTEGRAL_CONFIG_WINDOW_DEFAULT_HEIGHT 360u
#define INTEGRAL_CONFIG_WINDOW_MIN_WIDTH 360u
#define INTEGRAL_CONFIG_WINDOW_MIN_HEIGHT 360u
#define INTEGRAL_CONFIG_WINDOW_MAX_SIZE 16384u

typedef struct IntegralConfigKeys {
    char slot1[INTEGRAL_CONFIG_KEY_SPEC_MAX];
    char slot2[INTEGRAL_CONFIG_KEY_SPEC_MAX];
    char n64_p1[INTEGRAL_CONFIG_KEY_SPEC_MAX];
    char fast[INTEGRAL_CONFIG_KEY_NAME_MAX];
    char screenshot[INTEGRAL_CONFIG_KEY_NAME_MAX];
    char escape[INTEGRAL_CONFIG_KEY_NAME_MAX];
    char turbo_hold[INTEGRAL_CONFIG_KEY_NAME_MAX];
    char reset[INTEGRAL_CONFIG_KEY_NAME_MAX];
} IntegralConfigKeys;

typedef struct IntegralConfigLogin {
    int remember;
    char server[128];
    char server_id[32];
    char username[64];
} IntegralConfigLogin;

typedef struct IntegralConfigLocal {
    int slot1_index;
    int slot2_index;
} IntegralConfigLocal;

typedef struct IntegralConfigWindow {
    unsigned width;
    unsigned height;
} IntegralConfigWindow;

typedef struct IntegralConfigRomSlot {
    char rom_path[INTEGRAL_CONFIG_PATH_MAX];
    char rom_id[INTEGRAL_CONFIG_ID_MAX];
    char save_id[INTEGRAL_CONFIG_ID_MAX];
} IntegralConfigRomSlot;

int integral_config_load_login(const char *path, IntegralConfigLogin *login);
int integral_config_save_login(const char *path, const IntegralConfigLogin *login);
int integral_config_load_local(const char *path, IntegralConfigLocal *local);
int integral_config_save_local(const char *path, const IntegralConfigLocal *local);
int integral_config_load_window(const char *path, IntegralConfigWindow *window);
int integral_config_save_window(const char *path, const IntegralConfigWindow *window);
int integral_config_load_keys(const char *path, IntegralConfigKeys *keys);
int integral_config_save_keys(const char *path, const IntegralConfigKeys *keys);
int integral_config_load_rom_slots(const char *path, IntegralConfigRomSlot *slots, size_t slot_count);
int integral_config_save_rom_slots(const char *path, const IntegralConfigRomSlot *slots, size_t slot_count);

#endif

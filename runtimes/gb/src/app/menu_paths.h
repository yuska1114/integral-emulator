/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_APP_MENU_PATHS_H
#define INTEGRAL_GB_RUNTIME_APP_MENU_PATHS_H

#include <stdbool.h>
#include <stddef.h>

enum {
    INTEGRAL_GB_RUNTIME_MENU_PATH_MAX = 512,
};

bool integral_gb_runtime_menu_has_suffix(const char *text, const char *suffix);
const char *integral_gb_runtime_menu_basename_no_ext(const char *path, char *out, size_t out_size);
bool integral_gb_runtime_menu_rom_filename_valid(const char *name);
bool integral_gb_runtime_menu_rom_file_exists_in_roms(const char *name);
bool integral_gb_runtime_menu_rom_path_from_filename(const char *name, char *out, size_t out_size);

#endif

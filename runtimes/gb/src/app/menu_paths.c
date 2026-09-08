/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "menu_paths.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "log_util.h"
#include "string_util.h"

bool integral_gb_runtime_menu_has_suffix(const char *text, const char *suffix)
{
    size_t text_len = strlen(text);
    size_t suffix_len = strlen(suffix);
    if (text_len < suffix_len) {
        return false;
    }
    return strcasecmp(text + text_len - suffix_len, suffix) == 0;
}

const char *integral_gb_runtime_menu_basename_no_ext(const char *path, char *out, size_t out_size)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    (void)integral_gb_runtime_copy_text(out, out_size, base);
    char *dot = strrchr(out, '.');
    if (dot) {
        *dot = '\0';
    }
    return out;
}

bool integral_gb_runtime_menu_rom_filename_valid(const char *name)
{
    if (!name || name[0] == '\0') {
        return false;
    }
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
        return false;
    }
    if (strchr(name, '/') || strchr(name, '\\')) {
        return false;
    }
    return integral_gb_runtime_menu_has_suffix(name, ".gb") || integral_gb_runtime_menu_has_suffix(name, ".gbc");
}

bool integral_gb_runtime_menu_rom_file_exists_in_roms(const char *name)
{
    if (!integral_gb_runtime_menu_rom_filename_valid(name)) {
        return false;
    }

    char path[INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
    const char prefix[] = "roms/";
    size_t prefix_len = sizeof(prefix) - 1u;
    size_t name_len = strlen(name);
    if (prefix_len + name_len + 1u > sizeof(path)) {
        return false;
    }
    memcpy(path, prefix, prefix_len);
    memcpy(path + prefix_len, name, name_len + 1u);

    struct stat st;
    if (stat(path, &st) != 0) {
        if (integral_gb_runtime_log_enabled()) {
            fprintf(stderr, "rom path: stat failed for '%s': %s\n", path, strerror(errno));
            fflush(stderr);
        }
        return false;
    }
    if (!S_ISREG(st.st_mode)) {
        if (integral_gb_runtime_log_enabled()) {
            fprintf(stderr, "rom path: '%s' is not a regular file\n", path);
            fflush(stderr);
        }
        return false;
    }
    return true;
}

bool integral_gb_runtime_menu_rom_path_from_filename(const char *name, char *out, size_t out_size)
{
    if (!integral_gb_runtime_menu_rom_file_exists_in_roms(name)) {
        return false;
    }
    const char prefix[] = "roms/";
    size_t prefix_len = sizeof(prefix) - 1u;
    size_t name_len = strlen(name);
    if (prefix_len + name_len + 1u > out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return false;
    }
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, name, name_len + 1u);
    return true;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "file_util.h"
#include "utf8_file.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

static int platform_mkdir(const char *path, mode_t mode)
{
#ifdef _WIN32
    (void)mode;
    return _mkdir(path);
#else
    return mkdir(path, mode);
#endif
}

bool integral_gb_runtime_file_exists_regular(const char *path)
{
    return integral_file_regular(path);
}

bool integral_gb_runtime_paths_refer_to_same_regular_file(const char *a, const char *b)
{
    if (!a || !b || a[0] == '\0' || b[0] == '\0') {
        return false;
    }

    struct stat st_a;
    struct stat st_b;
    if (stat(a, &st_a) != 0 || stat(b, &st_b) != 0 ||
        !S_ISREG(st_a.st_mode) ||
        !S_ISREG(st_b.st_mode)) {
        return false;
    }

#ifdef _WIN32
    char full_a[INTEGRAL_GB_RUNTIME_FILE_PATH_MAX];
    char full_b[INTEGRAL_GB_RUNTIME_FILE_PATH_MAX];
    if (!_fullpath(full_a, a, sizeof(full_a)) ||
        !_fullpath(full_b, b, sizeof(full_b))) {
        return strcmp(a, b) == 0;
    }
    for (size_t i = 0;; i++) {
        unsigned char ca = (unsigned char)full_a[i];
        unsigned char cb = (unsigned char)full_b[i];
        if (ca == '\\') {
            ca = '/';
        }
        if (cb == '\\') {
            cb = '/';
        }
        ca = (unsigned char)tolower(ca);
        cb = (unsigned char)tolower(cb);
        if (ca != cb) {
            return false;
        }
        if (ca == '\0') {
            return true;
        }
    }
#else
    return st_a.st_dev == st_b.st_dev && st_a.st_ino == st_b.st_ino;
#endif
}

int integral_gb_runtime_ensure_directory(const char *path, mode_t mode)
{
    if (!path || !*path) {
        return 0;
    }
    if (platform_mkdir(path, mode) == 0 || errno == EEXIST) {
        return 0;
    }
    return -1;
}

int integral_gb_runtime_ensure_parent_directories(const char *path, mode_t mode)
{
    char copy[INTEGRAL_GB_RUNTIME_FILE_PATH_MAX];
    size_t path_len = strlen(path);
    if (path_len >= sizeof(copy)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(copy, path, path_len + 1);

    for (char *p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            if (integral_gb_runtime_ensure_directory(copy, mode) != 0) {
                return -1;
            }
            *p = '/';
        }
    }

    char *last_slash = strrchr(copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        return integral_gb_runtime_ensure_directory(copy, mode);
    }
    return 0;
}

int integral_gb_runtime_copy_file(const char *source, const char *dest)
{
    FILE *in = fopen(source, "rb");
    if (!in) {
        return -1;
    }
    FILE *out = fopen(dest, "wb");
    if (!out) {
        fclose(in);
        return -1;
    }

    char buffer[8192];
    while (true) {
        size_t n = fread(buffer, 1, sizeof(buffer), in);
        if (n > 0 && fwrite(buffer, 1, n, out) != n) {
            fclose(out);
            fclose(in);
            return -1;
        }
        if (n < sizeof(buffer)) {
            if (ferror(in)) {
                fclose(out);
                fclose(in);
                return -1;
            }
            break;
        }
    }

    fclose(out);
    fclose(in);
    return 0;
}

int integral_gb_runtime_replace_file(const char *temporary, const char *destination)
{
#ifdef _WIN32
    return MoveFileExA(temporary,
                       destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? 0
               : -1;
#else
    return rename(temporary, destination);
#endif
}

int integral_gb_runtime_unlink(const char *path)
{
#ifdef _WIN32
    return _unlink(path);
#else
    return unlink(path);
#endif
}

long integral_gb_runtime_getpid(void)
{
#ifdef _WIN32
    return (long)_getpid();
#else
    return (long)getpid();
#endif
}

bool integral_gb_runtime_join_path(char *dest, size_t dest_size, const char *dir, const char *file)
{
    size_t dir_len = strlen(dir);
    size_t file_len = strlen(file);
    size_t needs_slash = dir_len > 0 && dir[dir_len - 1] != '/' ? 1u : 0u;
    if (dir_len + needs_slash + file_len + 1u > dest_size) {
        return false;
    }

    memcpy(dest, dir, dir_len);
    size_t offset = dir_len;
    if (needs_slash) {
        dest[offset++] = '/';
    }
    memcpy(dest + offset, file, file_len);
    dest[offset + file_len] = '\0';
    return true;
}

void integral_gb_runtime_split_path(const char *path,
                          char *dir,
                          size_t dir_size,
                          char *file,
                          size_t file_size)
{
    const char *slash = strrchr(path, '/');
    if (!slash) {
        if (dir_size > 0) {
            dir[0] = '.';
        }
        if (dir_size > 1) {
            dir[1] = '\0';
        }
        if (file_size > 0) {
            size_t path_len = strlen(path);
            if (path_len >= file_size) {
                path_len = file_size - 1;
            }
            memcpy(file, path, path_len);
            file[path_len] = '\0';
        }
        return;
    }

    size_t dir_len = (size_t)(slash - path);
    if (dir_size > 0) {
        if (dir_len >= dir_size) {
            dir_len = dir_size - 1;
        }
        memcpy(dir, path, dir_len);
        dir[dir_len] = '\0';
    }
    if (file_size > 0) {
        size_t file_len = strlen(slash + 1);
        if (file_len >= file_size) {
            file_len = file_size - 1;
        }
        memcpy(file, slash + 1, file_len);
        file[file_len] = '\0';
    }
}

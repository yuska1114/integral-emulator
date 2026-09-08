/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_COMMON_FILE_UTIL_H
#define INTEGRAL_GB_RUNTIME_COMMON_FILE_UTIL_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

#define INTEGRAL_GB_RUNTIME_FILE_PATH_MAX 1024u

bool integral_gb_runtime_file_exists_regular(const char *path);
bool integral_gb_runtime_paths_refer_to_same_regular_file(const char *a, const char *b);
int integral_gb_runtime_ensure_directory(const char *path, mode_t mode);
int integral_gb_runtime_ensure_parent_directories(const char *path, mode_t mode);
int integral_gb_runtime_copy_file(const char *source, const char *dest);
int integral_gb_runtime_replace_file(const char *temporary, const char *destination);
int integral_gb_runtime_unlink(const char *path);
long integral_gb_runtime_getpid(void);
bool integral_gb_runtime_join_path(char *dest, size_t dest_size, const char *dir, const char *file);
void integral_gb_runtime_split_path(const char *path,
                          char *dir,
                          size_t dir_size,
                          char *file,
                          size_t file_size);

#endif

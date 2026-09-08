/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "process_util.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <direct.h>
#include <process.h>
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <unistd.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
static int spawn_wait_logged(const char *path, char *const argv[], bool *started)
{
    if (started) {
        *started = false;
    }
    fprintf(stderr, "spawn: trying %s\n", path);
    fflush(stderr);
    intptr_t status = _spawnv(_P_WAIT, path, (const char *const *)argv);
    if (status == -1) {
        fprintf(stderr, "spawn: %s failed: %s\n", path, strerror(errno));
        fflush(stderr);
        return -1;
    }
    if (started) {
        *started = true;
    }
    fprintf(stderr, "spawn: %s exited status=%ld\n", path, (long)status);
    fflush(stderr);
    return status == 0 ? 0 : -1;
}
#endif

static bool ascii_equal_ignore_case_process(const char *a, const char *b)
{
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

static char *last_path_separator_process(char *path)
{
    char *last_slash = strrchr(path, '\\');
    char *last_forward = strrchr(path, '/');
    if (!last_slash) {
        return last_forward;
    }
    if (!last_forward) {
        return last_slash;
    }
    return last_slash > last_forward ? last_slash : last_forward;
}

static int chdir_to_path_process(const char *path)
{
#ifdef _WIN32
    return _chdir(path);
#else
    return chdir(path);
#endif
}

static int chdir_to_package_root_from_exe_path(char *exe_path)
{
    char *slash = last_path_separator_process(exe_path);
    if (!slash) {
        errno = EINVAL;
        return -1;
    }
    *slash = '\0';

    char *parent_slash = last_path_separator_process(exe_path);
    const char *dir_name = parent_slash ? parent_slash + 1 : exe_path;
    if (ascii_equal_ignore_case_process(dir_name, "build") && parent_slash) {
        *parent_slash = '\0';
    }

    int result = chdir_to_path_process(exe_path);
    fprintf(stderr, "cwd: package root '%s' result=%d\n", exe_path, result);
    fflush(stderr);
    return result;
}

int integral_gb_runtime_chdir_to_package_root(void)
{
    const char *override_root = getenv("GB_RUNTIME_PACKAGE_ROOT");
    if (override_root && override_root[0] != '\0') {
        int result = chdir_to_path_process(override_root);
        fprintf(stderr, "cwd: package root override '%s' result=%d\n", override_root, result);
        fflush(stderr);
        return result;
    }

    char exe_path[1024];
#ifdef _WIN32
    DWORD len = GetModuleFileNameA(NULL, exe_path, sizeof(exe_path));
    if (len == 0 || len >= sizeof(exe_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return chdir_to_package_root_from_exe_path(exe_path);
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(exe_path);
    if (_NSGetExecutablePath(exe_path, &size) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }

    char resolved_path[1024];
    if (realpath(exe_path, resolved_path)) {
        return chdir_to_package_root_from_exe_path(resolved_path);
    }
    return chdir_to_package_root_from_exe_path(exe_path);
#else
    ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
    if (len < 0 || (size_t)len >= sizeof(exe_path)) {
        return -1;
    }
    exe_path[len] = '\0';
    return chdir_to_package_root_from_exe_path(exe_path);
#endif
}

int integral_gb_runtime_execv_with_exe_fallback(const char *path, char *const argv[])
{
#ifdef _WIN32
    size_t path_len = strlen(path);
    bool has_exe_suffix = false;
    if (path_len >= 4) {
        const char *suffix = path + path_len - 4;
        if (suffix[0] == '.' &&
            (suffix[1] == 'e' || suffix[1] == 'E') &&
            (suffix[2] == 'x' || suffix[2] == 'X') &&
            (suffix[3] == 'e' || suffix[3] == 'E')) {
            has_exe_suffix = true;
        }
    }
    if (has_exe_suffix) {
        bool started = false;
        return spawn_wait_logged(path, argv, &started);
    }

    char exe_path[512];
    const char suffix[] = ".exe";
    if (path_len + sizeof(suffix) > sizeof(exe_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(exe_path, path, path_len);
    memcpy(exe_path + path_len, suffix, sizeof(suffix));
    bool exe_started = false;
    int exe_result = spawn_wait_logged(exe_path, argv, &exe_started);
    if (exe_started) {
        if (exe_result != 0) {
            fprintf(stderr, "spawn: %s started and returned nonzero; not trying extensionless fallback\n", exe_path);
            fflush(stderr);
        }
        return exe_result;
    }
    int exe_error = errno;

    bool plain_started = false;
    int plain_result = spawn_wait_logged(path, argv, &plain_started);
    if (plain_started) {
        return plain_result;
    }
    errno = exe_error;
    return -1;
#else
    fprintf(stderr, "exec: trying %s\n", path);
    fflush(stderr);
    return execv(path, argv);
#endif
}

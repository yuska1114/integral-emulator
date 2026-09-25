/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_log.h"
#include "client_state.h"
#include "client_runtime_support.h"
#include "client_file_io.h"
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL.h>



#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

static FILE *g_client_log = NULL;
static char g_client_log_path[256] = "integral_client.log";

static const char *screen_name(AppScreen screen);
#ifndef _WIN32
static int open_private_client_log_append(const char *path);
#endif
#ifndef _WIN32
static bool client_log_mode_is_private(const char *path);
#endif

#ifdef _WIN32
struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return localtime_s(result, timep) == 0 ? result : NULL;
}

#endif

static const char *screen_name(AppScreen screen)
{
    switch (screen) {
        case SCREEN_SCREENSHOTS: return "screenshots";
        case SCREEN_LOGIN:
            return "login";
        case SCREEN_PASSWORD_CHANGE:
            return "password_change";
        case SCREEN_MAIN_MENU:
            return "main";
        case SCREEN_LOCAL_MODE:
            return "local_mode";
        case SCREEN_LOCAL:
            return "local";
        case SCREEN_GB_MOBILE:
            return "gb_mobile";
        case SCREEN_ROOM_MODE:
            return "room_mode";
        case SCREEN_JOIN_ROOM:
            return "join_room";
        case SCREEN_ROOM:
            return "room";
        case SCREEN_N64_ROOM:
            return "n64_room";
        case SCREEN_SETTINGS:
            return "settings";
        case SCREEN_GB_KEY_CONFIG:
            return "gb_keys";
        case SCREEN_N64_KEY_CONFIG:
            return "n64_keys";
        case SCREEN_UTIL_KEY_CONFIG:
            return "util_keys";
        case SCREEN_ROM_REGISTER:
            return "rom_register";
        case SCREEN_N64_RUNTIME:
            return "n64_runtime";
    }
    return "unknown";
}


#ifndef _WIN32
static int open_private_client_log_append(const char *path)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    return fd;
}

#endif

void client_log_open(const char *path)
{
    if (path && path[0]) {
        copy_text(g_client_log_path, sizeof(g_client_log_path), path);
    }
#ifdef _WIN32
    g_client_log = fopen(g_client_log_path, "a");
#else
    int fd = open_private_client_log_append(g_client_log_path);
    g_client_log = fd >= 0 ? fdopen(fd, "a") : NULL;
    if (!g_client_log && fd >= 0) close(fd);
#endif
    if (!g_client_log) {
        fprintf(stderr, "client log open failed: %s: %s\n", g_client_log_path, strerror(errno));
        return;
    }
    setvbuf(g_client_log, NULL, _IOLBF, 0);
}


void client_log_close(void)
{
    if (g_client_log) {
        fclose(g_client_log);
        g_client_log = NULL;
    }
}


void client_log_v(const AppState *state, const char *event, const char *fmt, va_list args)
{
    if (!g_client_log) {
        return;
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &tm_now);

    const char *username = state && state->login.username[0] ? state->login.username : "-";
    unsigned room = state ? state->room.common.room_number : 0;
    const char *screen = state ? screen_name(state->ui.screen) : "-";
    fprintf(g_client_log,
            "%s pid=%ld user=%s screen=%s room=%u event=%s ",
            timestamp,
            (long)getpid(),
            username,
            screen,
            room,
            event ? event : "-");
    if (fmt && fmt[0]) {
        vfprintf(g_client_log, fmt, args);
    }
    fputc('\n', g_client_log);
    fflush(g_client_log);
}


void client_log(const AppState *state, const char *event, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    client_log_v(state, event, fmt, args);
    va_end(args);
}


void client_save_log(const char *event, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    client_log_v(NULL, event, fmt, args);
    va_end(args);
}


void redirect_child_output_to_client_log(void)
{
#ifdef _WIN32
    int fd = open(g_client_log_path, O_WRONLY | O_CREAT | O_APPEND, 0644);
#else
    int fd = open_private_client_log_append(g_client_log_path);
#endif
    if (fd < 0) {
        return;
    }
    (void)dup2(fd, STDOUT_FILENO);
    (void)dup2(fd, STDERR_FILENO);
    if (fd > STDERR_FILENO) {
        close(fd);
    }
}


#ifndef _WIN32
static bool client_log_mode_is_private(const char *path)
{
    struct stat status;
    return stat(path, &status) == 0 && S_ISREG(status.st_mode) &&
           (status.st_mode & 0777) == 0600;
}

#endif

#ifndef _WIN32
int run_client_log_permission_smoke(const char *work_dir)
{
    if (!work_dir || chdir(work_dir) != 0) return 2;
    static const char *log_path = "client.log";
    FILE *fixture = fopen(log_path, "wb");
    if (!fixture || fclose(fixture) != 0 || chmod(log_path, 0644) != 0) return 1;

    client_log_open(log_path);
    if (!g_client_log) return 1;
    client_log(NULL, "permission_smoke", "writer=client");
    client_log_close();
    if (!client_log_mode_is_private(log_path)) return 1;

    if (chmod(log_path, 0644) != 0) return 1;
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        redirect_child_output_to_client_log();
        static const char message[] = "child output\n";
        ssize_t written = write(STDOUT_FILENO, message, sizeof(message) - 1u);
        _exit(written == (ssize_t)(sizeof(message) - 1u) ? 0 : 1);
    }
    int child_status = 0;
    if (waitpid(child, &child_status, 0) != child ||
        !WIFEXITED(child_status) || WEXITSTATUS(child_status) != 0 ||
        !client_log_mode_is_private(log_path)) {
        return 1;
    }
    if (remove(log_path) != 0) return 1;
    client_log_open(log_path);
    if (!g_client_log) return 1;
    client_log_close();
    if (!client_log_mode_is_private(log_path)) return 1;
    printf("client log permissions ok create=0600 normal=0600 child=0600\n");
    return 0;
}

#endif

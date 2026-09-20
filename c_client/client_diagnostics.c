/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_diagnostics.h"
#include "client_app.h"
#include "client_local.h"
#include "client_view.h"
#include "client_log.h"
#include "client_runtime_support.h"
#include "client_file_io.h"
#include "client_save_outbox.h"
#include "client_user_config.h"
#include "client_rom_catalog.h"
#include "sdl_unicode_text.h"
#include "gb_runtime_fixed_host_product_runtime.h"
#include "../runtimes/gb/src/common/key_config.h"
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


#define INTEGRAL_N64_RUNTIME_MEDIA_DIR "runtime/n64_runtime_media"
#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

#ifdef _WIN32
typedef struct N64RuntimeStopTestObservation {
    N64RuntimeStopResult stop;
    ULONGLONG elapsed_ms;
    bool process_running;
    bool handle_open;
    bool stop_file_exists;
    bool state_cleared;
} N64RuntimeStopTestObservation;
#endif

static bool runtime_path_has_mode(const char *path, unsigned expected_mode);
#ifdef _WIN32
static bool n64_runtime_stop_test_request_received(const char *path);
#endif
#ifdef _WIN32
static int run_n64_runtime_stop_test_child(const char *behavior,
                                           const char *stop_path);
#endif
#ifdef _WIN32
static bool spawn_n64_runtime_stop_test_child(const char *behavior,
                                              const char *stop_path,
                                              PROCESS_INFORMATION *process_info);
#endif
#ifdef _WIN32
static bool observe_n64_runtime_stop_test_case(const char *work_dir,
                                               const char *behavior,
                                               N64RuntimeStopTestObservation *observation);
#endif
#ifdef _WIN32
static bool n64_runtime_stop_test_case_passed(
    const char *behavior,
    const N64RuntimeStopTestObservation *observation);
#endif
#ifdef _WIN32
static void write_n64_runtime_stop_test_observation(
    FILE *result,
    const char *behavior,
    const N64RuntimeStopTestObservation *observation);
#endif
#ifdef _WIN32
static int run_n64_runtime_stop_process_test(const char *work_dir,
                                             const char *log_path);
#endif
static int run_runtime_path_cleanup_smoke(const char *work_dir);
static bool parse_unsigned_option(const char *value, unsigned *parsed);
static bool parse_n64_room_auto_options(int argc,
                                        char **argv,
                                        N64RoomAutoOptions *options,
                                        char *error,
                                        size_t error_size);

static bool runtime_path_has_mode(const char *path, unsigned expected_mode)
{
#ifdef _WIN32
    (void)path;
    (void)expected_mode;
    return true;
#else
    struct stat status;
    return stat(path, &status) == 0 && (status.st_mode & 0777) == expected_mode;
#endif
}


#ifdef _WIN32
static bool n64_runtime_stop_test_request_received(const char *path)
{
    static const unsigned char expected[] = "S64STOP1\n";
    unsigned char record[sizeof(expected)] = {0};
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    size_t count = fread(record, 1, sizeof(record), file);
    fclose(file);
    return count == sizeof(expected) - 1u &&
           memcmp(record, expected, sizeof(expected) - 1u) == 0;
}

#endif

#ifdef _WIN32
static int run_n64_runtime_stop_test_child(const char *behavior,
                                           const char *stop_path)
{
    if (!behavior || !stop_path) return 2;
    if (strcmp(behavior, "ignore") == 0) {
        for (;;) Sleep(1000u);
    }
    if (strcmp(behavior, "graceful") != 0) return 2;
    for (;;) {
        if (n64_runtime_stop_test_request_received(stop_path)) return 0;
        Sleep(25u);
    }
}

#endif

#ifdef _WIN32
static bool spawn_n64_runtime_stop_test_child(const char *behavior,
                                              const char *stop_path,
                                              PROCESS_INFORMATION *process_info)
{
    char executable[INTEGRAL_CONFIG_PATH_MAX];
    char command[INTEGRAL_CONFIG_PATH_MAX * 3u];
    DWORD executable_length = GetModuleFileNameA(NULL,
                                                 executable,
                                                 (DWORD)sizeof(executable));
    if (executable_length == 0 || executable_length >= sizeof(executable)) {
        return false;
    }
    int command_length = snprintf(command,
                                  sizeof(command),
                                  "\"%s\" --n64-stop-test-child %s \"%s\"",
                                  executable,
                                  behavior,
                                  stop_path);
    if (command_length < 0 || (size_t)command_length >= sizeof(command)) {
        return false;
    }
    STARTUPINFOA startup;
    memset(&startup, 0, sizeof(startup));
    startup.cb = sizeof(startup);
    memset(process_info, 0, sizeof(*process_info));
    return CreateProcessA(executable,
                          command,
                          NULL,
                          NULL,
                          FALSE,
                          CREATE_NO_WINDOW,
                          NULL,
                          NULL,
                          &startup,
                          process_info) != 0;
}

#endif

#ifdef _WIN32
static bool observe_n64_runtime_stop_test_case(const char *work_dir,
                                               const char *behavior,
                                               N64RuntimeStopTestObservation *observation)
{
    char stop_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!work_dir || !behavior || !observation) {
        return false;
    }
    int stop_path_length = snprintf(stop_path,
                                    sizeof(stop_path),
                                    "%s/%s.stop.request",
                                    work_dir,
                                    behavior);
    if (stop_path_length < 0 || (size_t)stop_path_length >= sizeof(stop_path)) {
        return false;
    }
    (void)remove(stop_path);
    PROCESS_INFORMATION process_info;
    if (!spawn_n64_runtime_stop_test_child(behavior, stop_path, &process_info)) {
        return false;
    }
    CloseHandle(process_info.hThread);
    HANDLE original_handle = process_info.hProcess;
    DWORD child_pid = process_info.dwProcessId;

    AppState *state = calloc(1, sizeof(*state));
    if (!state) {
        (void)TerminateProcess(process_info.hProcess, 1);
        (void)WaitForSingleObject(process_info.hProcess, 2000u);
        CloseHandle(process_info.hProcess);
        return false;
    }
    bind_room_context(state);
    copy_text(state->login.username,
              sizeof(state->login.username),
              "N64_STOP_TEST");
    copy_text(state->room.n64.n64_runtime_media_launched_session_id,
              sizeof(state->room.n64.n64_runtime_media_launched_session_id),
              behavior);
    copy_text(state->room.n64.n64_runtime_stop_request_path,
              sizeof(state->room.n64.n64_runtime_stop_request_path),
              stop_path);
    state->room.n64.n64_runtime_media_host_pid =
        (IntegralChildProcess)(intptr_t)process_info.hProcess;

    ULONGLONG started_ms = GetTickCount64();
    memset(observation, 0, sizeof(*observation));
    observation->stop = stop_n64_runtime_media_host_process(&state->room);
    observation->elapsed_ms = GetTickCount64() - started_ms;
    observation->state_cleared = state->room.n64.n64_runtime_media_host_pid == 0 &&
                                 state->room.n64.n64_runtime_media_launched_session_id[0] == '\0' &&
                                 state->room.n64.n64_runtime_stop_request_path[0] == '\0';
    free(state);

    DWORD handle_flags = 0;
    SetLastError(ERROR_SUCCESS);
    observation->handle_open = GetHandleInformation(original_handle,
                                                     &handle_flags) != 0 ||
                               GetLastError() != ERROR_INVALID_HANDLE;
    HANDLE process_probe = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION |
                                           SYNCHRONIZE,
                                       FALSE,
                                       child_pid);
    if (process_probe) {
        DWORD exit_code = STILL_ACTIVE;
        observation->process_running =
            !GetExitCodeProcess(process_probe, &exit_code) ||
            exit_code == STILL_ACTIVE;
        CloseHandle(process_probe);
    }
    observation->stop_file_exists =
        GetFileAttributesA(stop_path) != INVALID_FILE_ATTRIBUTES;
    return true;
}

#endif

#ifdef _WIN32
static bool n64_runtime_stop_test_case_passed(
    const char *behavior,
    const N64RuntimeStopTestObservation *observation)
{
    if (!observation || !observation->stop.request_created || !observation->stop.stopped ||
        observation->process_running || observation->handle_open ||
        observation->stop_file_exists || !observation->state_cleared) {
        return false;
    }
    if (strcmp(behavior, "ignore") == 0) {
        return !observation->stop.graceful && observation->stop.forced &&
               observation->elapsed_ms >= INTEGRAL_N64_RUNTIME_STOP_WAIT_MS &&
               observation->elapsed_ms < INTEGRAL_N64_RUNTIME_STOP_WAIT_MS + 3000u;
    }
    return observation->stop.graceful && !observation->stop.forced &&
           observation->elapsed_ms < INTEGRAL_N64_RUNTIME_STOP_WAIT_MS;
}

#endif

#ifdef _WIN32
static void write_n64_runtime_stop_test_observation(
    FILE *result,
    const char *behavior,
    const N64RuntimeStopTestObservation *observation)
{
    fprintf(result,
            "%s request_created=%d elapsed_ms=%llu graceful=%d forced=%d "
            "process_running=%d handle_open=%d stop_file_exists=%d "
            "state_cleared=%d\n",
            behavior,
            observation->stop.request_created ? 1 : 0,
            (unsigned long long)observation->elapsed_ms,
            observation->stop.graceful ? 1 : 0,
            observation->stop.forced ? 1 : 0,
            observation->process_running ? 1 : 0,
            observation->handle_open ? 1 : 0,
            observation->stop_file_exists ? 1 : 0,
            observation->state_cleared ? 1 : 0);
}

#endif

#ifdef _WIN32
static int run_n64_runtime_stop_process_test(const char *work_dir,
                                             const char *log_path)
{
    char result_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!work_dir || ensure_directory(work_dir) != 0) {
        return 2;
    }
    int result_path_length = snprintf(result_path,
                                      sizeof(result_path),
                                      "%s/result.txt",
                                      work_dir);
    if (result_path_length < 0 ||
        (size_t)result_path_length >= sizeof(result_path)) {
        return 2;
    }
    client_log_open(log_path);
    N64RuntimeStopTestObservation ignored;
    N64RuntimeStopTestObservation graceful;
    bool ignored_ran = observe_n64_runtime_stop_test_case(work_dir,
                                                          "ignore",
                                                          &ignored);
    bool graceful_ran = observe_n64_runtime_stop_test_case(work_dir,
                                                           "graceful",
                                                           &graceful);
    FILE *result = fopen(result_path, "wb");
    if (!result) {
        client_log_close();
        return 2;
    }
    if (ignored_ran) {
        write_n64_runtime_stop_test_observation(result, "ignore", &ignored);
    }
    else {
        fputs("ignore spawn_or_observation_failed=1\n", result);
    }
    if (graceful_ran) {
        write_n64_runtime_stop_test_observation(result, "graceful", &graceful);
    }
    else {
        fputs("graceful spawn_or_observation_failed=1\n", result);
    }
    bool passed = ignored_ran && graceful_ran &&
                  n64_runtime_stop_test_case_passed("ignore", &ignored) &&
                  n64_runtime_stop_test_case_passed("graceful", &graceful);
    fprintf(result, "%s\n", passed ? "PASS" : "FAIL");
    fclose(result);
    client_log_close();
    return passed ? 0 : 1;
}

#endif

static int run_runtime_path_cleanup_smoke(const char *work_dir)
{
    if (!work_dir || chdir(work_dir) != 0 ||
        !runtime_session_id_is_path_safe("session_SAFE-1") ||
        runtime_session_id_is_path_safe("") ||
        runtime_session_id_is_path_safe("../escape") ||
        runtime_session_id_is_path_safe("space id")) {
        return 1;
    }
    char too_long[97];
    memset(too_long, 'A', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';
    if (runtime_session_id_is_path_safe(too_long)) return 1;

    const char *session_id = "session_SAFE-1";
    char session_dir[INTEGRAL_CONFIG_PATH_MAX];
    char transfer_dir[INTEGRAL_CONFIG_PATH_MAX];
    char n64_save_dir[INTEGRAL_CONFIG_PATH_MAX];
    char config_dir[INTEGRAL_CONFIG_PATH_MAX];
    char screenshot_dir[INTEGRAL_CONFIG_PATH_MAX];
    if (ensure_directory("runtime") != 0 ||
        ensure_directory(INTEGRAL_N64_RUNTIME_MEDIA_DIR) != 0) {
        return 1;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_N64_RUNTIME_MEDIA_DIR) != 0 ||
        !format_runtime_session_path(session_dir, sizeof(session_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR, session_id, NULL) ||
        !format_runtime_session_path(transfer_dir, sizeof(transfer_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR, session_id, "transfer") ||
        !format_runtime_session_path(n64_save_dir, sizeof(n64_save_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR, session_id, "n64-save") ||
        !format_runtime_session_path(config_dir, sizeof(config_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR, session_id, "config") ||
        !format_runtime_session_path(screenshot_dir, sizeof(screenshot_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR, session_id, "screenshots") ||
        ensure_private_runtime_directory(session_dir) != 0 ||
        ensure_private_runtime_directory(transfer_dir) != 0 ||
        ensure_private_runtime_directory(n64_save_dir) != 0 ||
        ensure_private_runtime_directory(config_dir) != 0 ||
        ensure_private_runtime_directory(screenshot_dir) != 0) {
        return 1;
    }
    if (!runtime_path_has_mode("runtime", 0700) ||
        !runtime_path_has_mode(INTEGRAL_N64_RUNTIME_MEDIA_DIR, 0700) ||
        !runtime_path_has_mode(session_dir, 0700) ||
        !runtime_path_has_mode(transfer_dir, 0700) ||
        !runtime_path_has_mode(n64_save_dir, 0700) ||
        !runtime_path_has_mode(config_dir, 0700) ||
        !runtime_path_has_mode(screenshot_dir, 0700)) {
        return 1;
    }
    static const char *temporary_files[] = {
        "transfer/slot1.gbc", "transfer/slot2.gbc",
        "transfer/slot1.sav", "transfer/slot1.sav.rtc",
        "transfer/slot2.sav", "transfer/slot2.sav.rtc",
        "n64-save/n64.sav", "controller2.bin", "remote-media.ipc", "stop.request",
    };
    static const unsigned char sample[] = {0x53, 0x41, 0x56};
    char path[INTEGRAL_CONFIG_PATH_MAX];
    for (size_t i = 0; i < sizeof(temporary_files) / sizeof(temporary_files[0]); ++i) {
        if (!format_runtime_session_path(path, sizeof(path), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                         session_id, temporary_files[i]) ||
            write_private_runtime_file(path, sample, sizeof(sample)) != 0 ||
            !runtime_path_has_mode(path, 0600)) {
            return 1;
        }
    }
    char screenshot_path[INTEGRAL_CONFIG_PATH_MAX];
    char config_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(screenshot_path, sizeof(screenshot_path), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     session_id, "screenshots/user-saved.bmp") ||
        !format_runtime_session_path(config_path, sizeof(config_path), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     session_id, "config/user-kept.cfg") ||
        write_private_runtime_file(screenshot_path, sample, sizeof(sample)) != 0 ||
        write_private_runtime_file(config_path, sample, sizeof(sample)) != 0 ||
        write_private_runtime_file("runtime/outside.sav", sample, sizeof(sample)) != 0 ||
        !runtime_path_has_mode(screenshot_path, 0600) ||
        !runtime_path_has_mode(config_path, 0600) ||
        !runtime_path_has_mode("runtime/outside.sav", 0600)) {
        return 1;
    }
    cleanup_n64_room_session_files("../escape");
    cleanup_n64_room_session_files(session_id);
    for (size_t i = 0; i < sizeof(temporary_files) / sizeof(temporary_files[0]); ++i) {
        if (!format_runtime_session_path(path, sizeof(path), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                         session_id, temporary_files[i]) ||
            local_file_exists(path)) {
            return 1;
        }
    }
    if (!local_file_exists(screenshot_path) || !local_file_exists(config_path) ||
        !local_file_exists("runtime/outside.sav")) {
        return 1;
    }
    char too_small[8];
    if (format_runtime_session_path(too_small, sizeof(too_small),
                                    INTEGRAL_N64_RUNTIME_MEDIA_DIR, session_id, NULL)) {
        return 1;
    }
    printf("runtime path cleanup ok ids_rejected=4 known_files_removed=10 user_files_preserved=3\n");
    return 0;
}


static bool parse_unsigned_option(const char *value, unsigned *parsed)
{
    char *end = NULL;
    unsigned long number;
    if (!value || !value[0] || !parsed) return false;
    errno = 0;
    number = strtoul(value, &end, 10);
    if (errno != 0 || !end || *end != '\0' || number == 0 || number > 86400u) return false;
    *parsed = (unsigned)number;
    return true;
}


static bool parse_n64_room_auto_options(int argc,
                                        char **argv,
                                        N64RoomAutoOptions *options,
                                        char *error,
                                        size_t error_size)
{
    memset(options, 0, sizeof(*options));
    options->server_id = "primary";
    options->password_env = "INTEGRAL_N64_RUNTIME_AUTO_PASSWORD";
    options->duration_seconds = 90u;
    options->timeout_seconds = 240u;
    options->remote_vsync_enabled = true;
    for (int index = 1; index < argc; index++) {
        const char *name = argv[index];
        if (strcmp(name, "--n64-room-auto") == 0 && index + 1 < argc) {
            const char *role = argv[++index];
            if (strcmp(role, "host") == 0) options->role = N64_ROOM_AUTO_HOST;
            else if (strcmp(role, "remote") == 0) options->role = N64_ROOM_AUTO_REMOTE;
            else {
                snprintf(error, error_size, "--n64-room-auto must be host or remote");
                return false;
            }
        }
        else if (strcmp(name, "--auto-server") == 0 && index + 1 < argc) options->server = argv[++index];
        else if (strcmp(name, "--auto-server-id") == 0 && index + 1 < argc) options->server_id = argv[++index];
        else if (strcmp(name, "--auto-username") == 0 && index + 1 < argc) options->username = argv[++index];
        else if (strcmp(name, "--auto-password-env") == 0 && index + 1 < argc) options->password_env = argv[++index];
        else if (strcmp(name, "--auto-room-code") == 0 && index + 1 < argc) options->room_code = argv[++index];
        else if (strcmp(name, "--auto-status-file") == 0 && index + 1 < argc) options->status_file = argv[++index];
        else if (strcmp(name, "--auto-duration-seconds") == 0 && index + 1 < argc) {
            if (!parse_unsigned_option(argv[++index], &options->duration_seconds)) {
                snprintf(error, error_size, "invalid --auto-duration-seconds");
                return false;
            }
        }
        else if (strcmp(name, "--auto-timeout-seconds") == 0 && index + 1 < argc) {
            if (!parse_unsigned_option(argv[++index], &options->timeout_seconds)) {
                snprintf(error, error_size, "invalid --auto-timeout-seconds");
                return false;
            }
        }
        else if (strcmp(name, "--auto-remote-vsync") == 0 && index + 1 < argc) {
            const char *value = argv[++index];
            if (strcmp(value, "on") == 0) options->remote_vsync_enabled = true;
            else if (strcmp(value, "off") == 0) options->remote_vsync_enabled = false;
            else {
                snprintf(error, error_size, "--auto-remote-vsync must be on or off");
                return false;
            }
        }
        else if (strcmp(name, "--auto-remote-render-driver") == 0 && index + 1 < argc) {
            const char *value = argv[++index];
            options->remote_renderer_driver = strcmp(value, "auto") == 0 ? NULL : value;
            options->remote_renderer_driver_set = true;
        }
    }
    if (options->role == N64_ROOM_AUTO_NONE) return true;
    if (!options->server || !options->server[0] || !options->username || !options->username[0] ||
        !options->status_file || !options->status_file[0]) {
        snprintf(error, error_size, "N64 auto mode needs server, username, and status file");
        return false;
    }
    if (options->role == N64_ROOM_AUTO_REMOTE &&
        (!options->room_code || strlen(options->room_code) != 5u)) {
        snprintf(error, error_size, "N64 auto remote needs a five-digit room code");
        return false;
    }
    return true;
}


void write_n64_room_auto_status(const N64RoomAutoOptions *options,
                                       const char *state_name,
                                       const AppState *state,
                                       const char *detail)
{
    FILE *file;
    const IntegralApiRoom *room = NULL;
    if (!options || !options->status_file || !options->status_file[0]) return;
    if (state && state->room.common.room_number >= 1u && state->room.common.room_number <= INTEGRAL_API_ROOMS) {
        room = &state->room.common.current_room;
    }
    file = fopen(options->status_file, "wb");
    if (!file) return;
    fprintf(file,
            "state=%s\nrole=%s\nroom=%u\nroom_code=%s\npaired=%d\ndetail=%s\n",
            state_name ? state_name : "UNKNOWN",
            options->role == N64_ROOM_AUTO_HOST ? "host" : "remote",
            state ? state->room.common.room_number : 0u,
            room && room->room_code[0] ? room->room_code : "",
            state && state->room.n64.n64_runtime_media_paired ? 1 : 0,
            detail ? detail : "");
    fclose(file);
}


bool start_n64_room_auto(AppState *state,
                                const N64RoomAutoOptions *options,
                                char *error,
                                size_t error_size)
{
    const char *password = getenv(options->password_env);
    IntegralApiRoom room;
    if (!password || !password[0]) {
        snprintf(error, error_size, "password environment variable %s is empty", options->password_env);
        return false;
    }
    copy_text(state->login.server, sizeof(state->login.server), options->server);
    copy_text(state->login.server_id, sizeof(state->login.server_id), options->server_id);
    copy_text(state->login.username, sizeof(state->login.username), options->username);
    copy_text(state->login.password, sizeof(state->login.password), password);
    state->login.remember_login = false;
    submit_login(state);
    clear_secret(state->login.password, sizeof(state->login.password));
    if (!state->login.token[0]) {
        snprintf(error, error_size, "login failed: %.100s", state->login.status);
        return false;
    }
    if (!refresh_rom_slots_from_server(state)) {
        snprintf(error, error_size, "ROM slot sync failed: %.100s", state->login.status);
        return false;
    }
    if (state->room.common.room_number != 0u) leave_current_room(&state->room, false);
    memset(&room, 0, sizeof(room));
    if (options->role == N64_ROOM_AUTO_HOST) {
        if (integral_api_create_room(state->login.server, state->login.token, "n64",
                                     &room, error, error_size) != 0) return false;
        client_log(state, "n64_auto_room_create", "room=%u code=%s", room.room_number, room.room_code);
    }
    else {
        if (integral_api_join_room_code(state->login.server, state->login.token,
                                        options->room_code, &room, error, error_size) != 0) return false;
        client_log(state, "n64_auto_room_join", "room=%u code=%s", room.room_number, room.room_code);
    }
    activate_matched_room(&state->room, &room);
    write_n64_room_auto_status(options, "WAITING_PEER", state, "room active; selection published");
    return true;
}


int save_screenshot(SDL_Renderer *renderer, const char *path)
{
    int width = 0;
    int height = 0;
    if (SDL_GetRendererOutputSize(renderer, &width, &height) != 0 ||
        width <= 0 || height <= 0) {
        fprintf(stderr, "SDL_GetRendererOutputSize failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                          width,
                                                          height,
                                                          32,
                                                          SDL_PIXELFORMAT_ARGB8888);
    if (!surface) {
        fprintf(stderr, "SDL_CreateRGBSurfaceWithFormat failed: %s\n", SDL_GetError());
        return 1;
    }
    if (SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888, surface->pixels, surface->pitch) != 0) {
        fprintf(stderr, "SDL_RenderReadPixels failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return 1;
    }
    if (SDL_SaveBMP(surface, path) != 0) {
        fprintf(stderr, "SDL_SaveBMP failed: %s\n", SDL_GetError());
        SDL_FreeSurface(surface);
        return 1;
    }
    SDL_FreeSurface(surface);
    return 0;
}


int client_diagnostics_startup(int argc, char **argv, ClientDiagnostics *diag)
{
    memset(diag, 0, sizeof(*diag));
#ifndef _WIN32
    /* Treat a peer close as an ordinary transport error. */
    signal(SIGPIPE, SIG_IGN);
#endif
#ifdef _WIN32
    if (argc == 4 && strcmp(argv[1], "--n64-stop-test-child") == 0) {
        return run_n64_runtime_stop_test_child(argv[2], argv[3]);
    }
    if (argc >= 3 && strcmp(argv[1], "--n64-stop-process-test") == 0) {
        const char *test_log_path = "n64_runtime_stop_process_test.log";
        for (int i = 3; i + 1 < argc; i++) {
            if (strcmp(argv[i], "--log-file") == 0) {
                test_log_path = argv[i + 1];
            }
        }
        return run_n64_runtime_stop_process_test(argv[2], test_log_path);
    }
#endif
    diag->smoke_test = argc > 1 && strcmp(argv[1], "--smoke-test") == 0;
    if (diag->smoke_test &&
        (integral_text_scroll_offset(100, 328, 2000, true) != 0 ||
         integral_text_scroll_offset(1000, 328, 0, true) != 672 ||
         integral_text_scroll_offset(1000, 328, 999, false) != 0 ||
         integral_text_scroll_offset(1000, 328, 3000, false) != 100 ||
         integral_text_scroll_offset(1000, 328, 14500, false) != 672 ||
         integral_text_scroll_offset(1000, 328, 15440, false) != 0)) {
        fprintf(stderr, "text viewport calculation failed\n");
        return 2;
    }
    bool login_smoke = argc > 1 && strcmp(argv[1], "--login-smoke") == 0;
    bool config_smoke = argc > 1 && strcmp(argv[1], "--config-smoke") == 0;
    bool outbox_smoke = argc > 1 && strcmp(argv[1], "--outbox-smoke") == 0;
    bool log_permission_smoke = argc > 1 && strcmp(argv[1], "--log-permission-smoke") == 0;
    bool runtime_path_cleanup_smoke = argc > 1 && strcmp(argv[1], "--runtime-path-cleanup-smoke") == 0;
    bool local_navigation_smoke = argc > 1 && strcmp(argv[1], "--local-navigation-smoke") == 0;
    diag->screenshot_path = NULL;
    diag->config_path = INTEGRAL_CONFIG_DEFAULT_PATH;
    diag->log_path = "integral_client.log";
    if (!parse_n64_room_auto_options(argc, argv, &diag->n64_auto,
                                     diag->n64_auto_error, sizeof(diag->n64_auto_error))) {
        fprintf(stderr, "N64 ROOM auto option error: %s\n", diag->n64_auto_error);
        return 2;
    }
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            diag->config_path = argv[i + 1];
        }
        else if (strcmp(argv[i], "--log-file") == 0) {
            diag->log_path = argv[i + 1];
        }
    }
    if (log_permission_smoke) {
#ifdef _WIN32
        fprintf(stderr, "log permission smoke requires POSIX\n");
        return 2;
#else
        if (argc < 3) {
            fprintf(stderr, "usage: %s --log-permission-smoke EMPTY_DIRECTORY\n", argv[0]);
            return 2;
        }
        return run_client_log_permission_smoke(argv[2]);
#endif
    }
    if (outbox_smoke) {
        if (argc < 3 || chdir(argv[2]) != 0) {
            fprintf(stderr, "usage: %s --outbox-smoke EMPTY_DIRECTORY\n", argv[0]);
            return 2;
        }
        if (ensure_directory("runtime") != 0 ||
            ensure_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
            fprintf(stderr, "outbox permissive fixture create failed\n");
            return 1;
        }
        LocalSyncSlot slot;
        memset(&slot, 0, sizeof(slot));
        copy_text(slot.save_id, sizeof(slot.save_id), "save_outbox_smoke");
        slot.revision = 7;
        const unsigned char save_data[] = {0x47, 0x53, 0x43, 0x53, 0x41, 0x56};
        if (!write_save_upload_outbox("http://127.0.0.1:1",
                                      &slot,
                                      save_data,
                                      sizeof(save_data),
                                      "smokehash",
                                      "smoke_failure",
                                      "game_outbox_smoke",
                                      9,
                                      "smoke-request-id")) {
            fprintf(stderr, "outbox write failed\n");
            return 1;
        }
        unsigned pending = 0;
        unsigned replayed = replay_save_upload_outbox("http://127.0.0.1:1",
                                                       "smoke-token",
                                                       slot.save_id,
                                                       &pending, "");
        if (replayed != 0 || pending != 1) {
            fprintf(stderr, "outbox pending check failed replayed=%u pending=%u\n", replayed, pending);
            return 1;
        }
        DIR *dir = opendir(INTEGRAL_SAVE_OUTBOX_DIR);
        struct dirent *entry;
        char record_path[INTEGRAL_CONFIG_PATH_MAX] = "";
        while (dir && (entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len > 8 && strcmp(entry->d_name + name_len - 8, ".pending") == 0) {
                snprintf(record_path, sizeof(record_path), "%s/%s", INTEGRAL_SAVE_OUTBOX_DIR, entry->d_name);
                break;
            }
        }
        if (dir) {
            closedir(dir);
        }
        unsigned char *record_data = NULL;
        size_t record_size = 0;
        char record_line[1024];
        char save_path[INTEGRAL_CONFIG_PATH_MAX];
        if (record_path[0] == '\0' ||
            read_binary_file_alloc(record_path, &record_data, &record_size, sizeof(record_line) - 1) != 0) {
            fprintf(stderr, "outbox record read failed\n");
            return 1;
        }
        memcpy(record_line, record_data, record_size);
        record_line[record_size] = '\0';
        free(record_data);
        if (!extract_tsv_field(record_line, "path", save_path, sizeof(save_path))) {
            fprintf(stderr, "outbox record path missing\n");
            return 1;
        }
        if (!runtime_path_has_mode("runtime", 0700) ||
            !runtime_path_has_mode(INTEGRAL_SAVE_OUTBOX_DIR, 0700) ||
            !runtime_path_has_mode(record_path, 0600) ||
            !runtime_path_has_mode(save_path, 0600)) {
            fprintf(stderr, "outbox private permissions missing\n");
            return 1;
        }
#ifndef _WIN32
        if (chmod(record_path, 0644) != 0 || chmod(save_path, 0644) != 0) {
            fprintf(stderr, "outbox permissive recovery fixture create failed\n");
            return 1;
        }
        pending = 0;
        replayed = replay_save_upload_outbox("http://127.0.0.1:1",
                                             "smoke-token",
                                             slot.save_id,
                                             &pending, "");
        if (replayed != 0 || pending != 1 ||
            !runtime_path_has_mode(record_path, 0600) ||
            !runtime_path_has_mode(save_path, 0600)) {
            fprintf(stderr, "outbox recovery permission correction failed\n");
            return 1;
        }
#endif
        char completion_path[INTEGRAL_CONFIG_PATH_MAX];
        snprintf(completion_path, sizeof(completion_path), "%s.complete", save_path);
        static const unsigned char completion[] =
            "save_id=save_outbox_smoke\texpected_revision=7\tnext_revision=8";
        if (!complete_replayed_outbox_entry(save_path,
                                            record_path,
                                            completion_path,
                                            (const char *)completion,
                                            sizeof(completion) - 1u)) {
            fprintf(stderr, "outbox completion simulation failed\n");
            return 1;
        }
        if (!runtime_path_has_mode(completion_path, 0600)) {
            fprintf(stderr, "outbox completion permissions missing\n");
            return 1;
        }
        pending = 0;
        replayed = replay_save_upload_outbox("http://127.0.0.1:1",
                                             "smoke-token",
                                             slot.save_id,
                                             &pending, "");
        bool sent_file_found = false;
        dir = opendir(INTEGRAL_SAVE_OUTBOX_DIR);
        while (dir && (entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len >= 5u && strcmp(entry->d_name + name_len - 5u, ".sent") == 0) {
                sent_file_found = true;
                break;
            }
        }
        if (dir) closedir(dir);
        if (replayed != 0 || pending != 0 || local_file_exists(save_path) ||
            local_file_exists(record_path) || !local_file_exists(completion_path) ||
            sent_file_found) {
            fprintf(stderr, "outbox resolution check failed replayed=%u pending=%u\n", replayed, pending);
            return 1;
        }
        printf("outbox ok pending_preserved=1 replay_sav_deleted=1 completion_record=1\n");
        return 0;
    }
    if (runtime_path_cleanup_smoke) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s --runtime-path-cleanup-smoke EMPTY_DIRECTORY\n", argv[0]);
            return 2;
        }
        return run_runtime_path_cleanup_smoke(argv[2]);
    }
    if (login_smoke) {
        if (argc < 5) {
            fprintf(stderr, "usage: %s --login-smoke SERVER USERNAME PASSWORD [SERVER_ID]\n", argv[0]);
            return 2;
        }
        char token[160];
        char authenticated_username[64];
        char error[160];
        int must_change_password = 0;
        int allow_user_initial_save_import = 0;
        const char *server_id = argc >= 6 ? argv[5] : "primary";
        if (integral_api_login(argv[2], argv[3], argv[4], server_id,
                               token, sizeof(token), authenticated_username,
                               sizeof(authenticated_username), &must_change_password,
                               &allow_user_initial_save_import,
                               error, sizeof(error)) != 0) {
            fprintf(stderr, "login failed: %s\n", error);
            return 1;
        }
        printf("login ok username=%s token_prefix=%.*s must_change_password=%d allow_user_initial_save_import=%d\n",
               authenticated_username, 16, token, must_change_password,
               allow_user_initial_save_import);
        return 0;
    }
    if (config_smoke) {
        if (argc < 3) {
            fprintf(stderr, "usage: %s --config-smoke PATH\n", argv[0]);
            return 2;
        }
        IntegralConfigRomSlot slots[INTEGRAL_ROM_SLOTS];
        memset(slots, 0, sizeof(slots));
        copy_text(slots[0].rom_path, sizeof(slots[0].rom_path), "roms/example.gbc");
        copy_text(slots[0].rom_id, sizeof(slots[0].rom_id), "rom_test");
        copy_text(slots[0].save_id, sizeof(slots[0].save_id), "save_test");
        if (integral_config_save_rom_slots(argv[2], slots, INTEGRAL_ROM_SLOTS) != 0) {
            fprintf(stderr, "config save failed\n");
            return 1;
        }
        memset(slots, 0, sizeof(slots));
        if (integral_config_load_rom_slots(argv[2], slots, INTEGRAL_ROM_SLOTS) != 0) {
            fprintf(stderr, "config load failed\n");
            return 1;
        }
        IntegralConfigLocal local_config;
        integral_config_load_local(argv[2], &local_config);
        local_config.slot1_index = 0;
        local_config.slot2_index = -1;
        if (integral_config_save_local(argv[2], &local_config) != 0) {
            fprintf(stderr, "local config save failed\n");
            return 1;
        }
        IntegralConfigWindow window_config = {.width = 777u, .height = 611u};
        if (integral_config_save_window(argv[2], &window_config) != 0) {
            fprintf(stderr, "window config save failed\n");
            return 1;
        }
        IntegralConfigLogin login_config;
        memset(&login_config, 0, sizeof(login_config));
        login_config.remember = 1;
        copy_text(login_config.server, sizeof(login_config.server), "http://127.0.0.1:8080");
        copy_text(login_config.server_id, sizeof(login_config.server_id), "primary");
        copy_text(login_config.username, sizeof(login_config.username), "testuser");
        if (integral_config_save_login(argv[2], &login_config) != 0) {
            fprintf(stderr, "login config save failed\n");
            return 1;
        }
        memset(&login_config, 0, sizeof(login_config));
        login_config.remember = 0;
        copy_text(login_config.server, sizeof(login_config.server), "https://example.invalid/api-root");
        copy_text(login_config.server_id, sizeof(login_config.server_id), "secondary");
        if (integral_config_save_login(argv[2], &login_config) != 0) {
            fprintf(stderr, "login pref config save failed\n");
            return 1;
        }
        memset(&login_config, 0, sizeof(login_config));
        memset(slots, 0, sizeof(slots));
        memset(&local_config, 0, sizeof(local_config));
        memset(&window_config, 0, sizeof(window_config));
        IntegralConfigKeys key_config;
        memset(&key_config, 0, sizeof(key_config));
        integral_keys_defaults(&key_config);
        copy_text(key_config.screenshot, sizeof(key_config.screenshot), "O");
        if (integral_config_save_keys(argv[2], &key_config) != 0) {
            fprintf(stderr, "key config save failed\n");
            return 1;
        }
        AppState login_form_state;
        app_state_init(&login_form_state, argv[2]);
        copy_text(login_form_state.login.server,
                  sizeof(login_form_state.login.server),
                  "https://xxxxx.jp/zzz");
        copy_text(login_form_state.login.server_id,
                  sizeof(login_form_state.login.server_id),
                  INTEGRAL_PRIMARY_SERVER_ID);
        if (save_login_form_config(&login_form_state.login, login_form_state.base_config_path) != 0) {
            fprintf(stderr, "login form config save failed\n");
            return 1;
        }
        memset(&key_config, 0, sizeof(key_config));
        if (integral_config_load_login(argv[2], &login_config) != 0 ||
            integral_config_load_local(argv[2], &local_config) != 0 ||
            integral_config_load_window(argv[2], &window_config) != 0 ||
            integral_config_load_rom_slots(argv[2], slots, INTEGRAL_ROM_SLOTS) != 0 ||
            integral_config_load_keys(argv[2], &key_config) != 0) {
            fprintf(stderr, "config reload failed\n");
            return 1;
        }
        printf("config ok %s %s %s %s local%d window%ux%u\n",
               slots[0].rom_path,
               slots[0].save_id,
               login_config.username,
               key_config.screenshot,
               local_config.slot1_index, window_config.width, window_config.height);
        return strcmp(slots[0].save_id, "save_test") == 0 &&
                       login_config.remember == 0 &&
                       strcmp(login_config.server, "https://xxxxx.jp/zzz") == 0 &&
                       strcmp(login_config.server_id, "primary") == 0 &&
                       login_config.username[0] == '\0' &&
                       strcmp(key_config.screenshot, "O") == 0 &&
                       local_config.slot1_index == 0 &&
                       window_config.width == 777u &&
                       window_config.height == 611u
                   ? 0
                   : 1;
    }
    if (local_navigation_smoke) {
        AppState navigation;
        SDL_KeyboardEvent key;
        char digest_value[65];
        const char *digest_result =
            "sav_sha256=0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
        if (!execution_result_field(digest_result, "sav_sha256",
                                    digest_value, sizeof(digest_value)) ||
            strcmp(digest_value,
                   "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef") != 0) {
            return 1;
        }
        memset(&key, 0, sizeof(key));
        app_state_init(&navigation, diag->config_path);
        const char *launch_notices[] = {
            "GB_RUNTIME LOCAL STARTED", "GB_RUNTIME SERVER2 STARTED",
            "MOBILE MODE STARTED", "N64_RUNTIME STARTED TP:2",
        };
        for (unsigned i = 0; i < sizeof(launch_notices) / sizeof(launch_notices[0]); i++) {
            copy_text(navigation.login.status, sizeof(navigation.login.status), launch_notices[i]);
            clear_local_launch_notice(&navigation);
            if (navigation.login.status[0] != '\0') return 1;
        }
        copy_text(navigation.login.status, sizeof(navigation.login.status), "SAV UPLOAD FAILED");
        clear_local_launch_notice(&navigation);
        if (strcmp(navigation.login.status, "SAV UPLOAD FAILED") != 0) return 1;
        key.keysym.sym = SDLK_F3;
        if (navigation.login.password_visible) return 1;
        handle_login_key(&navigation, &key);
        if (!navigation.login.password_visible) return 1;
        key.repeat = 1;
        handle_login_key(&navigation, &key);
        if (!navigation.login.password_visible) return 1;
        key.repeat = 0;
        handle_login_key(&navigation, &key);
        if (navigation.login.password_visible) return 1;
        copy_text(navigation.catalog.server_rom_slots[0].rom_id,
                  sizeof(navigation.catalog.server_rom_slots[0].rom_id),
                  "stale_other_user_rom");
        load_active_user_config(&navigation);
        if (navigation.catalog.server_rom_slots[0].rom_id[0] != '\0') {
            return 1;
        }
        /* Smoke navigation must not launch a real configured ROM or contact a server. */
        memset(navigation.catalog.rom_slots, 0, sizeof(navigation.catalog.rom_slots));
        navigation.login.token[0] = '\0';
        navigation.ui.screen = SCREEN_MAIN_MENU;
        navigation.ui.main_selected = 0;
        key.keysym.sym = SDLK_RETURN;
        handle_main_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }

        navigation.ui.screen = SCREEN_MAIN_MENU;
        navigation.ui.main_selected = 1;
        key.keysym.sym = SDLK_RETURN;
        handle_main_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_ROOM_MODE ||
            strcmp(create_room_mode_api_name(0u), "link_cable") != 0 ||
            strcmp(create_room_mode_api_name(1u), "n64") != 0 ||
            create_room_mode_api_name(2u) != NULL ||
            !gb_runtime_fixed_host_may_start_for_control_state("host", "READY") ||
            gb_runtime_fixed_host_may_start_for_control_state("remote", "READY") ||
            !gb_runtime_fixed_host_may_start_for_control_state("remote", "WAITING_PEER")) {
            return 1;
        }
        key.keysym.sym = SDLK_UP;
        handle_room_mode_key(&navigation.room, &key);
        key.keysym.sym = SDLK_RETURN;
        handle_room_mode_key(&navigation.room, &key);
        if (navigation.ui.screen != SCREEN_MAIN_MENU) {
            return 1;
        }

        navigation.ui.main_selected = 5;
        handle_main_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_SCREENSHOTS || !navigation.screenshots) return 1;
        integral_screenshots_close(navigation.screenshots);
        navigation.screenshots = NULL;
        navigation.ui.screen = SCREEN_ROOM;
        navigation.room.common.room_number = 1;
        navigation.room.common.current_room.room_number = 1;
        copy_text(navigation.room.common.current_room.room_type,
                  sizeof(navigation.room.common.current_room.room_type),
                  "link_cable");
        copy_text(navigation.login.status,
                  sizeof(navigation.login.status),
                  "FIXED HOST ROM CHECK BOTH ROMS MISSING");
        char room_phase[96];
        format_room_phase(&navigation, &navigation.room.common.current_room, 1, 1,
                          room_phase, sizeof(room_phase));
        if (strcmp(room_phase, navigation.login.status) != 0) {
            return 1;
        }

        navigation.ui.screen = SCREEN_LOCAL_MODE;
        navigation.local.local_mode_selected = 0;
        key.keysym.sym = SDLK_RETURN;
        handle_local_mode_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_LOCAL) {
            return 1;
        }
        key.keysym.sym = SDLK_ESCAPE;
        handle_local_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }

        navigation.local.local_mode_selected = 1;
        key.keysym.sym = SDLK_RETURN;
        handle_local_mode_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_GB_MOBILE) {
            return 1;
        }
        handle_local_key(&navigation, &key);
        if (strstr(navigation.login.status, "SLOT1 ROM REQUIRED") == NULL) {
            return 1;
        }
        navigation.local.local_selected = 2;
        navigation.local.mobile_scenario_count = 3;
        navigation.local.mobile_scenario_selected = 0;
        copy_text(navigation.local.mobile_scenarios[0].display_name,
                  sizeof(navigation.local.mobile_scenarios[0].display_name), "SYNTHETIC ALPHA");
        copy_text(navigation.local.mobile_scenarios[1].display_name,
                  sizeof(navigation.local.mobile_scenarios[1].display_name), "SYNTHETIC BETA");
        copy_text(navigation.local.mobile_scenarios[2].display_name,
                  sizeof(navigation.local.mobile_scenarios[2].display_name), "SYNTHETIC GAMMA");
        key.keysym.sym = SDLK_LEFT;
        handle_local_key(&navigation, &key);
        if (navigation.local.mobile_scenario_selected != 2u ||
            strstr(navigation.login.status, "SYNTHETIC GAMMA") == NULL) {
            return 1;
        }
        key.keysym.sym = SDLK_RIGHT;
        handle_local_key(&navigation, &key);
        if (navigation.local.mobile_scenario_selected != 0u ||
            strstr(navigation.login.status, "SYNTHETIC ALPHA") == NULL) {
            return 1;
        }
        key.keysym.sym = SDLK_RETURN;
        handle_local_key(&navigation, &key);
        if (navigation.local.mobile_scenario_selected != 1u ||
            strstr(navigation.login.status, "SYNTHETIC BETA") == NULL) {
            return 1;
        }
        key.keysym.sym = SDLK_ESCAPE;
        handle_local_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }

        navigation.local.local_mode_selected = 2;
        key.keysym.sym = SDLK_RETURN;
        handle_local_mode_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_N64_RUNTIME) {
            return 1;
        }
        key.keysym.sym = SDLK_ESCAPE;
        handle_n64_runtime_key(&navigation, &key);
        if (navigation.ui.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }
        static const AppScreen keyboard_only_screens[] = {
            SCREEN_LOGIN, SCREEN_PASSWORD_CHANGE, SCREEN_MAIN_MENU,
            SCREEN_LOCAL_MODE, SCREEN_LOCAL, SCREEN_GB_MOBILE,
            SCREEN_N64_RUNTIME, SCREEN_ROOM_MODE, SCREEN_JOIN_ROOM,
            SCREEN_ROOM, SCREEN_ROM_REGISTER, SCREEN_SCREENSHOTS,
        };
        for (size_t index = 0;
             index < sizeof(keyboard_only_screens) / sizeof(keyboard_only_screens[0]);
             index++) {
            navigation.ui.screen = keyboard_only_screens[index];
            navigation.room.n64.n64_runtime_media_authenticated = true;
            if (client_game_input_required(&navigation)) return 1;
        }
        navigation.ui.screen = SCREEN_KEY_CONFIG;
        if (!client_game_input_required(&navigation)) return 1;
        navigation.ui.screen = SCREEN_N64_ROOM;
        navigation.room.n64.n64_runtime_media_authenticated = false;
        if (client_game_input_required(&navigation)) return 1;
        navigation.room.n64.n64_runtime_media_authenticated = true;
        if (!client_game_input_required(&navigation)) return 1;
        IntegralConfigKeys fixed_host_keys;
        memset(&fixed_host_keys, 0, sizeof(fixed_host_keys));
        copy_text(fixed_host_keys.slot1, sizeof(fixed_host_keys.slot1), "SLOT1-JOYCON");
        copy_text(fixed_host_keys.slot2, sizeof(fixed_host_keys.slot2), "SLOT2-JOYCON");
        if (strcmp(gb_runtime_fixed_host_key_spec(&fixed_host_keys),
                   "SLOT1-JOYCON") != 0) {
            return 1;
        }
        if (strcmp(key_config_step_label(KEY_CAPTURE_N64, 6u), "A BUTTON") != 0 ||
            strcmp(key_config_step_label(KEY_CAPTURE_N64, 7u), "B BUTTON") != 0 ||
            n64_key_spec_index_for_capture_step(6u) != 7u ||
            n64_key_spec_index_for_capture_step(7u) != 6u) {
            return 1;
        }
        printf("local navigation ok gb mobile n64\n");
        return 0;
    }
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--screenshot") == 0) {
            diag->screenshot_path = argv[i + 1];
        }
    }
    return -1; /* Continue ordinary startup. */
}


void client_diagnostics_prepare_screen(AppState *state, int argc, char **argv)
{
    if (state->login.server[0] == '\0') {
        copy_text(state->login.server,
                  sizeof(state->login.server),
                  "https://xxxxx.jp/zzz");
    }
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--screen") == 0) {
            if (strcmp(argv[i + 1], "main") == 0) {
                state->ui.screen = SCREEN_MAIN_MENU;
                copy_text(state->login.status, sizeof(state->login.status), "SELECT A MENU ITEM");
            }
            else if (strcmp(argv[i + 1], "screenshots") == 0) {
                state->screenshots = integral_screenshots_open(".");
                state->ui.screen = SCREEN_SCREENSHOTS;
            }
            else if (strcmp(argv[i + 1], "room-mode") == 0) {
                state->ui.screen = SCREEN_ROOM_MODE;
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER1");
                copy_text(state->login.status, sizeof(state->login.status), "SELECT COMMUNICATION MODE");
            }
            else if (strcmp(argv[i + 1], "join-room") == 0) {
                state->ui.screen = SCREEN_JOIN_ROOM;
                state->room.common.room_code_editing = true;
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER2");
                copy_text(state->room.common.room_code_input, sizeof(state->room.common.room_code_input), "483");
                copy_text(state->login.status, sizeof(state->login.status), "ENTER ROOM CODE");
            }
            else if (strcmp(argv[i + 1], "local-mode") == 0) {
                state->ui.screen = SCREEN_LOCAL_MODE;
                state->local.local_mode_selected = 0;
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER1");
                copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            }
            else if (strcmp(argv[i + 1], "local") == 0) {
                state->ui.screen = SCREEN_LOCAL;
                copy_text(state->login.status, sizeof(state->login.status), "GB MODE");
                copy_text(state->catalog.rom_slots[0].rom_path, sizeof(state->catalog.rom_slots[0].rom_path), "roms/sample_game.gbc");
                copy_text(state->catalog.rom_slots[0].rom_id, sizeof(state->catalog.rom_slots[0].rom_id), "rom_test");
                copy_text(state->catalog.rom_slots[0].save_id, sizeof(state->catalog.rom_slots[0].save_id), "save_test");
                state->local.local_slot_indices[0] = 0;
            }
            else if (strcmp(argv[i + 1], "gb-mobile") == 0) {
                state->ui.screen = SCREEN_GB_MOBILE;
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER1");
                copy_text(state->login.status, sizeof(state->login.status), "SELECT A MOBILE-CAPABLE ROM");
                copy_text(state->catalog.rom_slots[0].rom_path, sizeof(state->catalog.rom_slots[0].rom_path), "roms/sample_game.gbc");
                copy_text(state->catalog.rom_slots[0].rom_id, sizeof(state->catalog.rom_slots[0].rom_id), "rom_test");
                copy_text(state->catalog.rom_slots[0].save_id, sizeof(state->catalog.rom_slots[0].save_id), "save_test");
                state->local.local_slot_indices[0] = 0;
            }
            else if (strcmp(argv[i + 1], "n64_runtime") == 0) {
                state->ui.screen = SCREEN_N64_RUNTIME;
                copy_text(state->login.status, sizeof(state->login.status), "N64 MODE");
                copy_text(state->catalog.rom_slots[0].rom_path,
                          sizeof(state->catalog.rom_slots[0].rom_path),
                          "../runtimes/n64/roms/sample_n64.z64");
                state->local.integral_n64_runtime_n64_slot_index = 0;
            }
            else if (strcmp(argv[i + 1], "room") == 0) {
                state->ui.screen = SCREEN_ROOM;
                state->room.common.room_number = 1;
                state->room.link.room_slot_index = 0;
                state->room.common.room_ready_self = false;
                state->room.common.room_ready_peer = true;
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER1");
                IntegralApiRoom *room = &state->room.common.current_room;
                room->room_number = 1;
                copy_text(room->room_code, sizeof(room->room_code), "48321");
                copy_text(room->room_type, sizeof(room->room_type), "link_cable");
                copy_text(room->user1, sizeof(room->user1), "TESTUSER1");
                copy_text(room->user2, sizeof(room->user2), "TESTUSER2");
                copy_text(room->slot1, sizeof(room->slot1), "ROM1");
                copy_text(room->slot2, sizeof(room->slot2), "ROM2");
                copy_text(room->link_mode, sizeof(room->link_mode), "battle");
                room->ready2 = 1;
                copy_text(state->catalog.rom_slots[0].rom_path, sizeof(state->catalog.rom_slots[0].rom_path), "roms/sample_game.gbc");
                copy_text(state->room.common.room_chat_log[27], sizeof(state->room.common.room_chat_log[27]), "TESTUSER1: こんにちは");
                copy_text(state->room.common.room_chat_log[28], sizeof(state->room.common.room_chat_log[28]), "TESTUSER2: じゅんびOK?");
                copy_text(state->room.common.room_chat_log[29], sizeof(state->room.common.room_chat_log[29]), "TESTUSER1: SLOT ROM1");
                copy_text(state->room.common.room_chat_log[30], sizeof(state->room.common.room_chat_log[30]), "TESTUSER2: READY");
                copy_text(state->room.common.room_chat_log[31], sizeof(state->room.common.room_chat_log[31]), "TESTUSER1: 5けんめ");
                copy_text(state->room.common.room_chat_input, sizeof(state->room.common.room_chat_input), "よろしく");
                copy_text(state->login.status, sizeof(state->login.status), "ROOM");
            }
            else if (strcmp(argv[i + 1], "room-chat-active") == 0) {
                state->ui.screen = SCREEN_ROOM;
                state->room.common.room_number = 1;
                state->room.common.room_selected = 3;
                state->room.link.room_slot_index = 0;
                state->room.common.room_chat_editing = true;
                state->room.common.room_ready_self = false;
                state->room.common.room_ready_peer = true;
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER1");
                copy_text(state->catalog.rom_slots[0].rom_path, sizeof(state->catalog.rom_slots[0].rom_path), "roms/sample_game.gbc");
                copy_text(state->room.common.room_chat_log[30], sizeof(state->room.common.room_chat_log[30]), "TESTUSER2: じゅんびOK?");
                copy_text(state->room.common.room_chat_log[31], sizeof(state->room.common.room_chat_log[31]), "TESTUSER1: 5けんめ");
                copy_text(state->room.common.room_chat_input, sizeof(state->room.common.room_chat_input), "よろしく");
                copy_text(state->room.common.room_chat_composition, sizeof(state->room.common.room_chat_composition), "入力中");
                copy_text(state->login.status, sizeof(state->login.status), "CHAT INPUT ACTIVE");
            }
            else if (strcmp(argv[i + 1], "n64-room") == 0 ||
                     strcmp(argv[i + 1], "n64-room-ready-error") == 0) {
                state->ui.screen = SCREEN_N64_ROOM;
                state->room.common.room_number = 65;
                state->room.common.room_selected = 3;
                state->room.n64.n64_room_n64_slot_index = 0;
                state->room.n64.n64_room_user1_gb_slot_index = 1;
                state->room.n64.n64_room_user2_gb_slot_index = 2;
                IntegralApiRoom *room = &state->room.common.current_room;
                room->room_number = 65;
                copy_text(room->room_code, sizeof(room->room_code), "58134");
                copy_text(room->room_type, sizeof(room->room_type), "n64");
                copy_text(room->user1, sizeof(room->user1), "PLAYER001");
                copy_text(room->user2, sizeof(room->user2), "PLAYER002");
                copy_text(room->n64_slot1, sizeof(room->n64_slot1), "ROM1");
                copy_text(room->n64_slot_filename1,
                          sizeof(room->n64_slot_filename1),
                          "SAMPLE_N64.Z64");
                copy_text(room->n64_slot_game_type1,
                          sizeof(room->n64_slot_game_type1),
                          "catalog_n64");
                copy_text(room->n64_slot_header_title1,
                          sizeof(room->n64_slot_header_title1), "SAMPLE N64");
                copy_text(room->slot1, sizeof(room->slot1), "ROM2");
                copy_text(room->slot_filename1,
                          sizeof(room->slot_filename1),
                          "SAMPLE_A.GBC");
                copy_text(room->slot_game_type1,
                          sizeof(room->slot_game_type1),
                          "catalog_alpha");
                copy_text(room->slot_header_title1,
                          sizeof(room->slot_header_title1), "ALPHA CORE");
                copy_text(room->slot2, sizeof(room->slot2), "ROM3");
                copy_text(room->slot_filename2,
                          sizeof(room->slot_filename2),
                          "SAMPLE_B.GBC");
                copy_text(room->slot_game_type2,
                          sizeof(room->slot_game_type2),
                          "catalog_beta");
                copy_text(room->slot_header_title2,
                          sizeof(room->slot_header_title2), "BETA CORE");
                copy_text(state->login.username, sizeof(state->login.username), "TESTUSER1");
                copy_text(state->room.common.room_chat_log[30], sizeof(state->room.common.room_chat_log[30]), "PLAYER001: N64 MODE");
                copy_text(state->room.common.room_chat_log[31], sizeof(state->room.common.room_chat_log[31]), "PLAYER002: READY?");
                copy_text(state->login.status,
                          sizeof(state->login.status),
                          strcmp(argv[i + 1], "n64-room-ready-error") == 0
                              ? "READY FAILED: USER1 NEEDS MATCHING ROM IN ROM1-8"
                              : "ROOM65 N64 MODE");
            }
            else if (strcmp(argv[i + 1], "login-active") == 0) {
                state->ui.screen = SCREEN_LOGIN;
                state->login.selected = FIELD_USERNAME;
                state->login.editing = true;
                copy_text(state->login.username, sizeof(state->login.username), "testuser");
                copy_text(state->login.status, sizeof(state->login.status), "TEXT INPUT ACTIVE");
            }
            else if (strcmp(argv[i + 1], "password-change") == 0) {
                state->ui.screen = SCREEN_PASSWORD_CHANGE;
                copy_text(state->password_change.status,
                          sizeof(state->password_change.status),
                          "PASSWORD CHANGE REQUIRED");
            }
            else if (strcmp(argv[i + 1], "password-change-active") == 0) {
                state->ui.screen = SCREEN_PASSWORD_CHANGE;
                state->password_change.selected = PASSWORD_CHANGE_NEW;
                state->password_change.editing = true;
                copy_text(state->password_change.new_password,
                          sizeof(state->password_change.new_password),
                          "newpass1");
                copy_text(state->password_change.status,
                          sizeof(state->password_change.status),
                          "TEXT INPUT ACTIVE");
            }
            else if (strcmp(argv[i + 1], "keys") == 0) {
                state->ui.screen = SCREEN_KEY_CONFIG;
                copy_text(state->login.status, sizeof(state->login.status), "KEY CONFIG");
            }
            else if (strcmp(argv[i + 1], "rom") == 0 || strcmp(argv[i + 1], "rom-confirm") == 0 ||
                     strcmp(argv[i + 1], "rom-edit") == 0) {
                state->ui.screen = SCREEN_ROM_REGISTER;
                copy_text(state->login.status, sizeof(state->login.status), "ROM REGISTER");
                copy_text(state->catalog.rom_slots[0].rom_path, sizeof(state->catalog.rom_slots[0].rom_path), "roms/銀 試験.gbc");
                copy_text(state->catalog.rom_slots[1].rom_path, sizeof(state->catalog.rom_slots[1].rom_path), "roms/スタジアム 試験.z64");
                if (strcmp(argv[i + 1], "rom-confirm") == 0) {
                    state->catalog.rom_editor.rom_confirm_delete = true;
                    copy_text(state->login.username, sizeof(state->login.username), "TESTUSER");
                    copy_text(state->catalog.server_rom_slots[0].filename, sizeof(state->catalog.server_rom_slots[0].filename), "クリスタル.gbc");
                }
                if (strcmp(argv[i + 1], "rom-edit") == 0) state->catalog.rom_editor.rom_edit_target = ROM_EDIT_ROM;
            }
            else if (strcmp(argv[i + 1], "rom-browser") == 0) {
                state->ui.screen = SCREEN_ROM_REGISTER;
                state->catalog.rom_editor.rom_browser_active = true;
                state->catalog.rom_editor.rom_browser_count = 3;
                state->catalog.rom_editor.rom_browser_selected = 1;
                copy_text(state->login.status, sizeof(state->login.status), "SELECT ROM FROM FOLDER");
                copy_text(state->catalog.rom_editor.rom_browser_entries[0], sizeof(state->catalog.rom_editor.rom_browser_entries[0]), "roms/sample_a.gbc");
                copy_text(state->catalog.rom_editor.rom_browser_entries[1], sizeof(state->catalog.rom_editor.rom_browser_entries[1]), "roms/sample_b.gbc");
                copy_text(state->catalog.rom_editor.rom_browser_entries[2], sizeof(state->catalog.rom_editor.rom_browser_entries[2]), "roms/sample_c.gbc");
            }
        }
    }
}


void client_diagnostics_tick(AppState *state, ClientDiagnostics *diag)
{
    if (diag->n64_auto.role != N64_ROOM_AUTO_NONE) {
        Uint32 now = SDL_GetTicks();
        const IntegralApiRoom *room = NULL;
        if (state->room.common.room_number >= 1u && state->room.common.room_number <= INTEGRAL_API_ROOMS) {
            room = &state->room.common.current_room;
        }
        if (diag->n64_auto_paired_ticks != 0u &&
            now - diag->n64_auto_paired_ticks >= diag->n64_auto.duration_seconds * 1000u) {
            if (state->room.n64.n64_runtime_media_saw_positive_video) {
                client_log(state, "n64_auto_complete", "duration_seconds=%u", diag->n64_auto.duration_seconds);
                write_n64_room_auto_status(&diag->n64_auto, "COMPLETE", state, "measurement window complete");
            }
            else {
                copy_text(diag->n64_auto_error, sizeof(diag->n64_auto_error),
                          "measurement completed without a video frame");
                client_log(state, "n64_auto_failed", "stage=metrics error=%s", diag->n64_auto_error);
                write_n64_room_auto_status(&diag->n64_auto, "FAILED", state, diag->n64_auto_error);
                diag->n64_auto_exit_code = 1;
            }
            state->ui.quit = true;
        }
        else if (state->room.n64.n64_runtime_media_paired) {
            if (diag->n64_auto_paired_ticks == 0u) {
                diag->n64_auto_paired_ticks = now;
                client_log(state, "n64_auto_paired", "duration_seconds=%u", diag->n64_auto.duration_seconds);
                write_n64_room_auto_status(&diag->n64_auto, "MEASURING", state, "media paired");
            }
        }
        else if (diag->n64_auto_paired_ticks != 0u) {
            Uint32 measured_ms = now - diag->n64_auto_paired_ticks;
            Uint32 target_ms = diag->n64_auto.duration_seconds * 1000u;
            if (measured_ms + 1000u >= target_ms) {
                if (state->room.n64.n64_runtime_media_saw_positive_video) {
                    client_log(state,
                               "n64_auto_complete",
                               "duration_seconds=%u peer_closed_at_ms=%u",
                               diag->n64_auto.duration_seconds,
                               measured_ms);
                    write_n64_room_auto_status(&diag->n64_auto, "COMPLETE", state,
                                               "peer closed within final measurement second");
                }
                else {
                    copy_text(diag->n64_auto_error, sizeof(diag->n64_auto_error),
                              "peer closed at boundary without a video frame");
                    client_log(state, "n64_auto_failed", "stage=metrics error=%s", diag->n64_auto_error);
                    write_n64_room_auto_status(&diag->n64_auto, "FAILED", state, diag->n64_auto_error);
                    diag->n64_auto_exit_code = 1;
                }
            }
            else {
                snprintf(diag->n64_auto_error, sizeof(diag->n64_auto_error),
                         "media pair lost after %u milliseconds",
                         measured_ms);
                client_log(state, "n64_auto_failed", "stage=media error=%s", diag->n64_auto_error);
                write_n64_room_auto_status(&diag->n64_auto, "FAILED", state, diag->n64_auto_error);
                diag->n64_auto_exit_code = 1;
            }
            state->ui.quit = true;
        }
        else if (room && room->user1[0] && room->user2[0]) {
            if (diag->n64_auto_both_present_ticks == 0u) {
                diag->n64_auto_both_present_ticks = now;
                write_n64_room_auto_status(&diag->n64_auto, "READY_DELAY", state,
                                           "both users present; waiting for selection propagation");
            }
            if (!state->room.n64.n64_room_ready && now - diag->n64_auto_both_present_ticks >= 2000u) {
                if (!resolve_n64_room_local_outbox(&state->room) || !sync_n64_room_state(&state->room, true)) {
                    snprintf(diag->n64_auto_error, sizeof(diag->n64_auto_error),
                             "READY failed: %.120s", state->login.status);
                    client_log(state, "n64_auto_failed", "stage=ready error=%s", diag->n64_auto_error);
                    write_n64_room_auto_status(&diag->n64_auto, "FAILED", state, diag->n64_auto_error);
                    diag->n64_auto_exit_code = 1;
                    state->ui.quit = true;
                }
                else {
                    state->room.n64.n64_room_ready = true;
                    write_n64_room_auto_status(&diag->n64_auto, "WAITING_PAIR", state,
                                               "READY published; waiting for media pair");
                }
            }
        }
        if (!state->ui.quit &&
            now - diag->n64_auto_started_ticks >= diag->n64_auto.timeout_seconds * 1000u) {
            snprintf(diag->n64_auto_error, sizeof(diag->n64_auto_error),
                     "timeout after %u seconds: %.100s",
                     diag->n64_auto.timeout_seconds, state->login.status);
            client_log(state, "n64_auto_failed", "stage=timeout error=%s", diag->n64_auto_error);
            write_n64_room_auto_status(&diag->n64_auto, "FAILED", state, diag->n64_auto_error);
            diag->n64_auto_exit_code = 1;
            state->ui.quit = true;
        }
    }
}


void client_diagnostics_configure_media(AppState *state, const ClientDiagnostics *diag)
{
    if (diag->n64_auto.role == N64_ROOM_AUTO_REMOTE) {
        integral_n64_runtime_media_stream_set_video_vsync_enabled(
            state->room.n64.n64_runtime_media_stream, diag->n64_auto.remote_vsync_enabled);
        if (diag->n64_auto.remote_renderer_driver_set) {
            integral_n64_runtime_media_stream_set_video_renderer_driver(
                state->room.n64.n64_runtime_media_stream, diag->n64_auto.remote_renderer_driver);
        }
        client_log(state, "n64_auto_remote_vsync",
                   "requested=%s renderer=%s",
                   diag->n64_auto.remote_vsync_enabled ? "on" : "off",
                   diag->n64_auto.remote_renderer_driver ? diag->n64_auto.remote_renderer_driver : "auto");
    }
}

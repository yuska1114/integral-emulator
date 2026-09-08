/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
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

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#elif defined(INTEGRAL_USE_OPENSSL)
#include <openssl/evp.h>
#endif
#include <SDL.h>

#include "client_config.h"
#include "credential_store.h"
#include "http_client.h"
#include "room_poll_worker.h"
#include "media_relay_client.h"
#include "mobile_session_contract.h"
#include "n64_runtime_media_stream.h"
#include "rom_metadata.h"
#include "sdl_text.h"
#include "sdl_unicode_text.h"
#include "gb_runtime_fixed_host_rom_resolver.h"
#include "gb_runtime_fixed_host_snapshot_ipc.h"
#include "gb_runtime_fixed_host_result_ipc.h"
#include "../runtimes/gb/src/common/key_config.h"

#define INTEGRAL_CLIENT_VERSION "0.1BETA"
#define INTEGRAL_WINDOW_WIDTH 480
#define INTEGRAL_WINDOW_HEIGHT 480
#define INTEGRAL_FIELD_COUNT 6
#define INTEGRAL_PASSWORD_CHANGE_ROWS 3
#define INTEGRAL_MAIN_ROWS 6
#define INTEGRAL_LOCAL_MODE_ROWS 3
#define INTEGRAL_GB_RUNTIME_MODE_ROWS 3
#define INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS 3
#define INTEGRAL_ROOM_MODE_ROWS 2
#define INTEGRAL_CHAT_LOG_LINES 32
#define INTEGRAL_CHAT_VISIBLE_LINES 5
#define INTEGRAL_CHAT_MESSAGE_MAX 160
#define INTEGRAL_LINK_ROOM_HEARTBEAT_MS 15000u
#define INTEGRAL_ROOM_HEARTBEAT_MS 5000u
#define INTEGRAL_ROOM_ROWS 5
#define INTEGRAL_N64_RUNTIME_ROOM_ROWS 5
#define INTEGRAL_KEY_ROWS 5
#define INTEGRAL_KEY_BUTTONS 8
#define INTEGRAL_N64_RUNTIME_KEY_BUTTONS 18
#define INTEGRAL_UTIL_KEYS 5
#define INTEGRAL_ROM_SLOTS 8
#define INTEGRAL_ROM_REGISTER_ROW INTEGRAL_ROM_SLOTS
#define INTEGRAL_ROM_EXPORT_ROW (INTEGRAL_ROM_SLOTS + 1)
#define INTEGRAL_ROM_BACK_ROW (INTEGRAL_ROM_SLOTS + 2)
#define INTEGRAL_ROM_ROWS (INTEGRAL_ROM_SLOTS + 3)
#define INTEGRAL_ROM_BROWSER_MAX 24
#define INTEGRAL_PRIMARY_SERVER_ID "primary"
#define INTEGRAL_SECONDARY_SERVER_ID "secondary"
#define INTEGRAL_ROM_FOLDER "roms"
#define INTEGRAL_EXPORT_FOLDER "export"
#define INTEGRAL_GB_RUNTIME_DUAL_SERVER "../runtimes/gb/build_exp/integral_gb_runtime_dual_server"
#ifdef _WIN32
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME "build/integral_gb_runtime_fixed_host.exe"
#else
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME "build/integral_gb_runtime_fixed_host"
#endif
#ifndef INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID
#define INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID "integral-gb-runtime-fixed-host-v2"
#endif
#define INTEGRAL_GB_RUNTIME_MOBILE_RUNTIME "../runtimes/gb/build_exp/integral_gb_runtime_mobile_runtime"
#define INTEGRAL_N64_RUNTIME_HOME "../runtimes/n64"
#define INTEGRAL_N64_RUNTIME_TRANSFER_DIR "runtime/n64_runtime-transfer"
#define INTEGRAL_N64_RUNTIME_N64_SAVE_DIR "runtime/n64_runtime-n64-save"
#define INTEGRAL_N64_RUNTIME_CONFIG_DIR "runtime/n64_runtime-config"
#define INTEGRAL_N64_RUNTIME_SCREENSHOT_DIR "runtime/n64_runtime-screenshots"
#define INTEGRAL_N64_RUNTIME_MEDIA_DIR "runtime/n64_runtime_media"
#define INTEGRAL_N64_RUNTIME_STOP_WAIT_MS 5000u
#define INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE 28u
#define INTEGRAL_N64_RUNTIME_INPUT_IPC_FAILURE_LIMIT_MS 1000u
#define INTEGRAL_SAVE_OUTBOX_DIR "runtime/save-outbox"
#define INTEGRAL_N64_RUNTIME_SYNC_SLOTS 5
#define INTEGRAL_LOCAL_GAME_HEARTBEAT_SECONDS 15
#define INTEGRAL_LOCAL_GAME_HEARTBEAT_FAILURE_LIMIT 3
#define INTEGRAL_GB_RUNTIME_PORT "25100"
#define INTEGRAL_MAX_SAVE_BYTES (128u * 1024u)
#define INTEGRAL_MAX_ROM_BYTES (16u * 1024u * 1024u)

#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
typedef intptr_t IntegralChildProcess;

static struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return localtime_s(result, timep) == 0 ? result : NULL;
}

static void sleep(unsigned seconds)
{
    Sleep(seconds * 1000u);
}

#define WNOHANG 1

static IntegralChildProcess waitpid(IntegralChildProcess pid, int *status, int options)
{
    HANDLE process = (HANDLE)(intptr_t)pid;
    if (!process || process == INVALID_HANDLE_VALUE) {
        errno = ECHILD;
        return -1;
    }
    DWORD wait_ms = options == WNOHANG ? 0 : INFINITE;
    DWORD wait_result = WaitForSingleObject(process, wait_ms);
    if (wait_result == WAIT_TIMEOUT) {
        return 0;
    }
    if (wait_result == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        if (status && GetExitCodeProcess(process, &exit_code)) {
            *status = (int)exit_code;
        }
        CloseHandle(process);
        return pid;
    }
    CloseHandle(process);
    errno = ECHILD;
    return -1;
}

static int kill(IntegralChildProcess pid, int signal_number)
{
    (void)signal_number;
    HANDLE process = (HANDLE)(intptr_t)pid;
    if (!process || process == INVALID_HANDLE_VALUE) {
        errno = ESRCH;
        return -1;
    }
    DWORD wait_result = WaitForSingleObject(process, 0);
    if (wait_result == WAIT_TIMEOUT) {
        return 0;
    }
    errno = ESRCH;
    return -1;
}
#else
typedef pid_t IntegralChildProcess;
#endif

static int child_process_exit_code(int status)
{
#ifdef _WIN32
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

typedef enum LoginField {
    FIELD_SERVER,
    FIELD_ENV,
    FIELD_USERNAME,
    FIELD_PASSWORD,
    FIELD_REMEMBER,
    FIELD_ACTION,
} LoginField;

#ifdef _WIN32
static int integral_winsock_ready(void)
{
    static bool initialized = false;
    if (initialized) {
        return 0;
    }
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return -1;
    }
    initialized = true;
    return 0;
}
#endif

static const char *integral_gb_runtime_server_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER");
    return value && value[0] ? value : INTEGRAL_GB_RUNTIME_DUAL_SERVER;
}

static const char *integral_gb_runtime_fixed_host_runtime_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME");
    return value && value[0] ? value : INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME;
}

static const char *integral_gb_runtime_fixed_host_ca_file(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA_FILE");
    return value && value[0] ? value : "";
}

static const char *integral_gb_runtime_mobile_runtime_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME");
    return value && value[0] ? value : INTEGRAL_GB_RUNTIME_MOBILE_RUNTIME;
}

static const char *integral_n64_runtime_home_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_N64_RUNTIME_HOME");
    return value && value[0] ? value : INTEGRAL_N64_RUNTIME_HOME;
}

static int integral_runtime_frontend_access(const char *path)
{
#ifdef _WIN32
    /* MSVCRT _access() does not support the POSIX X_OK mode. */
    return access(path, F_OK);
#else
    return access(path, X_OK);
#endif
}

static void integral_n64_runtime_path(char *out, size_t out_size, const char *suffix)
{
    snprintf(out, out_size, "%s/%s", integral_n64_runtime_home_path(), suffix);
}

typedef enum AppScreen {
    SCREEN_LOGIN,
    SCREEN_PASSWORD_CHANGE,
    SCREEN_MAIN_MENU,
    SCREEN_LOCAL_MODE,
    SCREEN_LOCAL,
    SCREEN_GB_MOBILE,
    SCREEN_ROOM_MODE,
    SCREEN_JOIN_ROOM,
    SCREEN_ROOM,
    SCREEN_N64_ROOM,
    SCREEN_KEY_CONFIG,
    SCREEN_ROM_REGISTER,
    SCREEN_N64_RUNTIME,
} AppScreen;

typedef enum IntegralRoomLinkMode {
    INTEGRAL_ROOM_MODE_BATTLE,
    INTEGRAL_ROOM_MODE_TRADE,
} IntegralRoomLinkMode;

typedef enum PasswordChangeField {
    PASSWORD_CHANGE_NEW,
    PASSWORD_CHANGE_CONFIRM,
    PASSWORD_CHANGE_SAVE,
} PasswordChangeField;

typedef enum KeyCaptureTarget {
    KEY_CAPTURE_NONE,
    KEY_CAPTURE_SLOT1,
    KEY_CAPTURE_SLOT2,
    KEY_CAPTURE_N64,
    KEY_CAPTURE_UTILS,
} KeyCaptureTarget;

typedef enum RomEditTarget {
    ROM_EDIT_NONE,
    ROM_EDIT_ROM,
    ROM_EDIT_INITIAL_SAVE,
} RomEditTarget;

typedef struct LoginState {
    char server[128];
    char server_id[32];
    char username[64];
    char password[64];
    char status[160];
    char token[160];
    LoginField selected;
    bool editing;
    bool quit;
    bool password_visible;
    bool remember_login;
} LoginState;

typedef struct PasswordChangeState {
    char new_password[64];
    char confirm_password[64];
    char status[160];
    PasswordChangeField selected;
    bool editing;
    bool password_visible;
} PasswordChangeState;

typedef struct AppState {
    AppScreen screen;
    LoginState login;
    PasswordChangeState password_change;
    IntegralConfigRomSlot rom_slots[INTEGRAL_ROM_SLOTS];
    IntegralApiRomSlot server_rom_slots[INTEGRAL_ROM_SLOTS];
    IntegralConfigKeys keys;
    char base_config_path[160];
    char config_path[160];
    unsigned main_selected;
    unsigned local_mode_selected;
    unsigned local_selected;
    unsigned room_mode_selected;
    char room_code_input[INTEGRAL_API_ROOM_CODE_MAX];
    bool room_code_editing;
    int local_slot_indices[2];
    IntegralApiMobileScenario mobile_scenarios[INTEGRAL_API_MOBILE_SCENARIOS_MAX];
    unsigned mobile_scenario_count;
    unsigned mobile_scenario_selected;
    char mobile_scenario_rom_id[96];
    char mobile_scenario_save_id[96];
    unsigned integral_n64_runtime_selected;
    int integral_n64_runtime_n64_slot_index;
    int integral_n64_runtime_transfer_slot_indices[4];
    IntegralApiRoom current_room;
    Uint32 last_room_heartbeat_ticks;
    Uint32 last_room_heartbeat_attempt_ticks;
    bool room_heartbeat_attempted;
    unsigned room_heartbeat_failures;
    char room_lifecycle_status[24];
    char room_termination_reason[64];
    long long room_remaining_seconds;
    IntegralRoomPollWorker *n64_room_poll_worker;
    uint32_t room_poll_epoch;
    uint32_t room_poll_last_ms;
    uint32_t room_poll_max_ms;
    unsigned room_number;
    unsigned room_selected;
    bool n64_room_ready;
    int n64_room_n64_slot_index;
    int n64_room_user1_gb_slot_index;
    int n64_room_user2_gb_slot_index;
    char n64_runtime_media_session_id[96];
    char n64_runtime_media_relay_host[128];
    unsigned n64_runtime_media_relay_port;
    char n64_runtime_media_role[16];
    char n64_runtime_media_scope[32];
    char n64_runtime_media_ticket[128];
    IntegralMediaRelayConnection *n64_runtime_media_connection;
    IntegralN64RuntimeMediaStream *n64_runtime_media_stream;
    bool n64_runtime_media_authenticated;
    bool n64_runtime_media_paired;
    Uint32 n64_runtime_media_retry_after_ticks;
    uint64_t n64_runtime_media_remote_buttons;
    char n64_runtime_media_remote_input_path[INTEGRAL_CONFIG_PATH_MAX];
    char n64_runtime_media_ipc_path[INTEGRAL_CONFIG_PATH_MAX];
    char n64_runtime_stop_request_path[INTEGRAL_CONFIG_PATH_MAX];
    IntegralChildProcess n64_runtime_media_host_pid;
    char n64_runtime_media_launched_session_id[96];
    Uint32 n64_runtime_media_host_launch_retry_after_ticks;
    uint64_t n64_runtime_media_last_sent_buttons;
    uint32_t n64_runtime_media_input_sequence;
    Uint32 n64_runtime_media_last_input_send_ticks;
    Uint32 n64_runtime_media_last_input_receive_ticks;
    Uint32 n64_runtime_media_input_ipc_failure_since_ticks;
    unsigned n64_runtime_media_input_ipc_failures;
    uint64_t n64_runtime_media_input_interval_total_ms;
    uint32_t n64_runtime_media_input_interval_samples;
    uint32_t n64_runtime_media_input_interval_max_ms;
    bool n64_runtime_media_saw_positive_video;
    bool runtime_exit_confirming;
    bool runtime_exit_confirm_yes;
    bool runtime_exit_quit_client;
    IntegralRoomLinkMode room_link_mode;
    bool room_link_mode_local_override;
    int room_slot_index;
    bool room_ready_self;
    bool room_ready_peer;
    bool room_chat_editing;
    unsigned room_chat_scroll;
    char room_chat_input[INTEGRAL_CHAT_MESSAGE_MAX];
    char room_chat_composition[INTEGRAL_CHAT_MESSAGE_MAX];
    char room_chat_log[INTEGRAL_CHAT_LOG_LINES][INTEGRAL_CHAT_MESSAGE_MAX];
    char room_link_session_id[INTEGRAL_API_ROOM_SESSION_MAX];
    char room_game_session_id[96];
    long long room_fencing_token;
    bool room_start_requested;
    bool room_client_started;
    bool room_game_ended;
    IntegralChildProcess room_client_pid;
    bool room_gb_runtime_fixed_host_active;
    char room_gb_runtime_fixed_host_role[16];
    intptr_t room_gb_runtime_fixed_host_result_read;
    SDL_Thread *room_gb_runtime_fixed_host_result_thread;
    IntegralGBRuntimeFixedHostResult room_gb_runtime_fixed_host_result;
    char room_gb_runtime_fixed_host_save_policy[32];
    Uint32 last_room_poll_ticks;
    Uint32 room_session_missing_since_ticks;
    unsigned key_selected;
    KeyCaptureTarget key_capture_target;
    unsigned key_capture_step;
    bool game_input_active;
    bool key_capture_wait_release;
    SDL_Keycode key_capture_release_binding;
    unsigned rom_selected;
    RomEditTarget rom_edit_target;
    bool rom_confirm_delete;
    bool allow_user_initial_save_import;
    int rom_initial_save_import_slot;
    char rom_initial_save_import_path[INTEGRAL_CONFIG_PATH_MAX];
    bool rom_confirm_initial_save_import;
    bool rom_browser_active;
    char rom_browser_entries[INTEGRAL_ROM_BROWSER_MAX][INTEGRAL_CONFIG_PATH_MAX];
    unsigned rom_browser_count;
    unsigned rom_browser_selected;
    bool quit;
} AppState;

static FILE *g_client_log = NULL;
static char g_client_log_path[256] = "integral_client.log";

typedef IntegralRomMetadata RomHeaderInfo;

typedef struct LocalSyncSlot {
    char save_id[96];
    char mobile_session_id[96];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    char last_hash[65];
    int revision;
    bool mobile_guard;
    bool preserve_save_path;
    size_t authoritative_size;
    char mobile_runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
} LocalSyncSlot;

#ifdef _WIN32
typedef struct SaveSyncMonitorArgs {
    intptr_t process_handle;
    char server[128];
    char token[160];
    char game_session_id[96];
    long long fencing_token;
    LocalSyncSlot sync_slots[INTEGRAL_N64_RUNTIME_SYNC_SLOTS];
    unsigned sync_count;
    bool game_session_active;
} SaveSyncMonitorArgs;
#endif

static size_t field_capacity(LoginField field)
{
    switch (field) {
        case FIELD_SERVER:
            return sizeof(((LoginState *)0)->server);
        case FIELD_USERNAME:
            return sizeof(((LoginState *)0)->username);
        case FIELD_PASSWORD:
            return sizeof(((LoginState *)0)->password);
        default:
            return 0;
    }
}

static char *field_value(LoginState *state, LoginField field)
{
    switch (field) {
        case FIELD_SERVER:
            return state->server;
        case FIELD_USERNAME:
            return state->username;
        case FIELD_PASSWORD:
            return state->password;
        default:
            return NULL;
    }
}

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

static long long days_from_civil(int year, unsigned month, unsigned day)
{
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned adjusted_month = month > 2 ? month - 3u : month + 9u;
    const unsigned day_of_year =
        (153u * adjusted_month + 2u) / 5u + day - 1u;
    const unsigned day_of_era =
        year_of_era * 365u + year_of_era / 4u - year_of_era / 100u + day_of_year;
    return (long long)era * 146097LL + (long long)day_of_era - 719468LL;
}

static bool parse_iso8601_unix(const char *value, long long *unix_time_out)
{
    int year = 0;
    unsigned month = 0;
    unsigned day = 0;
    unsigned hour = 0;
    unsigned minute = 0;
    unsigned second = 0;
    if (!value || !unix_time_out ||
        sscanf(value, "%d-%u-%uT%u:%u:%u", &year, &month, &day, &hour, &minute, &second) != 6 ||
        month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
        return false;
    }
    long long offset_seconds = 0;
    const char *zone = value + 19;
    if (*zone == '.') {
        while (*zone && *zone != 'Z' && *zone != '+' && *zone != '-') {
            zone++;
        }
    }
    if (*zone == '+' || *zone == '-') {
        unsigned offset_hour = 0;
        unsigned offset_minute = 0;
        if (sscanf(zone + 1, "%u:%u", &offset_hour, &offset_minute) != 2 ||
            offset_hour > 23 || offset_minute > 59) {
            return false;
        }
        offset_seconds = (long long)(offset_hour * 60u + offset_minute) * 60LL;
        if (*zone == '-') {
            offset_seconds = -offset_seconds;
        }
    }
    else if (*zone != 'Z' && *zone != '\0') {
        return false;
    }
    *unix_time_out = days_from_civil(year, month, day) * 86400LL +
                     (long long)hour * 3600LL + (long long)minute * 60LL +
                     (long long)second - offset_seconds;
    return true;
}

static const char *screen_name(AppScreen screen)
{
    switch (screen) {
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
        case SCREEN_KEY_CONFIG:
            return "keys";
        case SCREEN_ROM_REGISTER:
            return "rom_register";
        case SCREEN_N64_RUNTIME:
            return "n64_runtime";
    }
    return "unknown";
}

static const char *room_link_mode_api_name(IntegralRoomLinkMode mode)
{
    switch (mode) {
        case INTEGRAL_ROOM_MODE_BATTLE:
            return "battle";
        case INTEGRAL_ROOM_MODE_TRADE:
            return "trade";
    }
    return "trade";
}

static const char *room_link_mode_label(IntegralRoomLinkMode mode)
{
    switch (mode) {
        case INTEGRAL_ROOM_MODE_BATTLE:
            return "BATTLE MODE";
        case INTEGRAL_ROOM_MODE_TRADE:
            return "TRADE MODE";
    }
    return "TRADE MODE";
}

static bool room_link_mode_from_api(const char *mode, IntegralRoomLinkMode *mode_out)
{
    if (!mode_out) {
        return false;
    }
    if (mode && strcmp(mode, "battle") == 0) {
        *mode_out = INTEGRAL_ROOM_MODE_BATTLE;
        return true;
    }
    if (mode && strcmp(mode, "trade") == 0) {
        *mode_out = INTEGRAL_ROOM_MODE_TRADE;
        return true;
    }
    return false;
}

static void cycle_room_link_mode(AppState *state, int delta)
{
    static const IntegralRoomLinkMode order[] = {
        INTEGRAL_ROOM_MODE_BATTLE,
        INTEGRAL_ROOM_MODE_TRADE,
    };
    int current = 0;
    for (int i = 0; i < 2; i++) {
        if (order[i] == state->room_link_mode) {
            current = i;
            break;
        }
    }
    int next = current + delta;
    while (next < 0) {
        next += 2;
    }
    next %= 2;
    state->room_link_mode = order[next];
    state->room_ready_self = false;
    state->room_ready_peer = false;
}

static void set_room_link_mode(AppState *state, IntegralRoomLinkMode mode)
{
    state->room_link_mode = mode;
    state->room_ready_self = false;
    state->room_ready_peer = false;
}

static void mark_room_link_mode_local_override(AppState *state)
{
    state->room_link_mode_local_override = true;
    state->room_ready_self = false;
    state->room_ready_peer = false;
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

static void client_log_open(const char *path)
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

static void client_log_close(void)
{
    if (g_client_log) {
        fclose(g_client_log);
        g_client_log = NULL;
    }
}

static void client_log(const AppState *state, const char *event, const char *fmt, ...)
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
    unsigned room = state ? state->room_number : 0;
    const char *screen = state ? screen_name(state->screen) : "-";
    fprintf(g_client_log,
            "%s pid=%ld user=%s screen=%s room=%u event=%s ",
            timestamp,
            (long)getpid(),
            username,
            screen,
            room,
            event ? event : "-");
    va_list args;
    va_start(args, fmt);
    if (fmt && fmt[0]) {
        vfprintf(g_client_log, fmt, args);
    }
    va_end(args);
    fputc('\n', g_client_log);
    fflush(g_client_log);
}

static void redirect_child_output_to_client_log(void)
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

static int run_client_log_permission_smoke(const char *work_dir)
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

static const char *path_file_name(const char *path);
static void cycle_local_rom_slot(AppState *state, unsigned slot_index, int delta);
static void refresh_mobile_scenarios(AppState *state);
static void cycle_mobile_scenario(AppState *state, int delta);
static void cycle_room_rom_slot(AppState *state, int delta);
static void start_local_gb_runtime(AppState *state);
static void start_local_gb_mobile(AppState *state);
static void start_local_n64_runtime(AppState *state);
static int read_supported_rom_header(const char *path, RomHeaderInfo *info);
static int sha256_file_hex(const char *path, char *out, size_t out_size);
static int read_binary_file(const char *path, unsigned char *out, size_t out_capacity, size_t *out_size);
static int write_binary_file(const char *path, const unsigned char *data, size_t data_size);
static int write_private_runtime_file(const char *path, const unsigned char *data, size_t data_size);
static int read_binary_file_alloc(const char *path, unsigned char **out, size_t *out_size, size_t max_size);
static int ensure_directory(const char *path);
static int ensure_private_runtime_directory(const char *path);
static int make_private_runtime_file(const char *path);
static void refresh_room(AppState *state);
static void refresh_room_quiet(AppState *state);
static void activate_matched_room(AppState *state, const IntegralApiRoom *matched_room);
static void sync_room_state(AppState *state);
static void maybe_start_room_session(AppState *state);
static bool server_rtc_offset_text(AppState *state, char *out, size_t out_size);
static bool current_room_game_ended(const AppState *state);
static const IntegralConfigRomSlot *registered_rom_slot_at(const AppState *state, int index);
static bool slot_is_supported_n64(const IntegralConfigRomSlot *slot);
static bool slot_is_supported_gb(const IntegralConfigRomSlot *slot);
static bool n64_room_gb_game_type(const AppState *state, int index, char *out, size_t out_size);
static const IntegralConfigRomSlot *find_n64_room_host_slot2_rom(const AppState *state, int *out_index);
static void init_n64_room_selection(AppState *state);
static void cycle_n64_room_slot(AppState *state, unsigned row, int delta);
static int n64_room_local_user_index(const AppState *state);
static bool sync_n64_room_state(AppState *state, bool ready);
static bool request_n64_runtime_media_session(AppState *state);
static void reset_n64_runtime_media_connection(AppState *state);
static void poll_n64_room_host_process(AppState *state);
static bool maybe_start_n64_room_host_n64_runtime(AppState *state, Uint32 now);
static bool game_controller_input_event(Uint32 type);
static uint64_t n64_remote_keyboard_buttons(const AppState *state);
static int n64_key_name_to_scancode(const char *name);
static bool n64_remote_controller_scancode(const AppState *state, SDL_Scancode scancode);
static void leave_current_room(AppState *state, bool update_status);
static void send_room_heartbeat(AppState *state, Uint32 now);
static void handle_room_heartbeat_result(AppState *state,
                                          const IntegralApiHeartbeatStatus *heartbeat,
                                          bool succeeded,
                                          const char *error);
static int sync_slot_from_server(AppState *state,
                                 const IntegralConfigRomSlot *slot,
                                 const char *session_save_path,
                                 LocalSyncSlot *sync_slot);
static bool upload_changed_save(const char *server,
                                const char *token,
                                const char *game_session_id,
                                long long fencing_token,
                                LocalSyncSlot *sync_slot,
                                bool write_outbox_on_failure);
static int atomic_replace_binary_file(const char *path,
                                      const unsigned char *data,
                                      size_t data_size);
static unsigned replay_save_upload_outbox(const char *server,
                                          const char *token,
                                          const char *only_save_id,
                                          unsigned *pending_out);
static bool resolve_save_upload_outbox(AppState *state, const char *save_id);
static bool stop_current_game_session(AppState *state);
static bool local_file_exists(const char *path);
static bool slot_has_server_registration(const IntegralConfigRomSlot *slot);
static bool slot_has_pending_local_rom(const IntegralConfigRomSlot *slot);
static bool discard_pending_rom_slots(AppState *state);

static void append_text(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(dest);
    if (len >= dest_size) {
        return;
    }
    copy_text(dest + len, dest_size - len, src);
}

static bool execution_result_field(const char *result,
                          const char *key,
                          char *value_out,
                          size_t value_out_size)
{
    size_t key_size = strlen(key);
    const char *line = result;
    while (line && *line) {
        const char *end = strchr(line, '\n');
        size_t line_size = end ? (size_t)(end - line) : strlen(line);
        if (line_size > key_size + 1u && !memcmp(line, key, key_size) &&
            line[key_size] == '=' && line_size - key_size <= value_out_size) {
            size_t value_size = line_size - key_size - 1u;
            memcpy(value_out, line + key_size + 1u, value_size);
            value_out[value_size] = '\0';
            for (size_t i = 0; i < value_size; ++i) {
                char c = value_out[i];
                if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                      c == '_' || c == '-' || c == '.')) {
                    return false;
                }
            }
            return true;
        }
        line = end ? end + 1 : NULL;
    }
    return false;
}

static bool cleanup_mobile_runtime_directory(const char *runtime_dir)
{
    static const char prefix[] = "runtime/gb-mobile/mobile_";
    if (!runtime_dir || strncmp(runtime_dir, prefix, sizeof(prefix) - 1u) != 0) {
        return false;
    }
    const char *id = runtime_dir + sizeof(prefix) - 1u;
    size_t id_length = strlen(id);
    if (id_length < 32u || id_length > 64u) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)id; *p; ++p) {
        if (!(isalnum(*p) || *p == '_' || *p == '-')) {
            return false;
        }
    }
    static const char *files[] = {
        "working.sav", "adapter.bin", "runtime-result.txt", "session.manifest",
    };
    char path[INTEGRAL_CONFIG_PATH_MAX];
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        snprintf(path, sizeof(path), "%s/%s", runtime_dir, files[i]);
        (void)remove(path);
    }
    for (unsigned i = 0; i < INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACTS; ++i) {
        snprintf(path, sizeof(path), "%s/artifact-%02u.bin", runtime_dir, i);
        (void)remove(path);
    }
#ifdef _WIN32
    return _rmdir(runtime_dir) == 0 || errno == ENOENT;
#else
    return rmdir(runtime_dir) == 0 || errno == ENOENT;
#endif
}

static bool complete_mobile_lifecycle(const char *server,
                                      const char *token,
                                      const char *game_session_id,
                                      long long fencing_token,
                                      const LocalSyncSlot *sync_slot)
{
    char error[160];
    for (unsigned attempt = 1; attempt <= 3; ++attempt) {
        if (integral_api_complete_mobile_session(server, token,
                                                 sync_slot->mobile_session_id,
                                                 game_session_id, fencing_token,
                                                 error, sizeof(error)) == 0) {
            client_log(NULL, "mobile_complete_ok", "mobile_session_id=%s attempt=%u", sync_slot->mobile_session_id, attempt);
            return true;
        }
        client_log(NULL, "mobile_complete_retry", "mobile_session_id=%s attempt=%u error=%s", sync_slot->mobile_session_id, attempt, error);
    }
    return false;
}

static bool append_ascii_text(char *dest, size_t dest_size, const char *src)
{
    bool accepted_all = true;
    size_t len = strlen(dest);
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        if (*p < 32 || *p > 126) {
            accepted_all = false;
            continue;
        }
        if (len + 1 >= dest_size) {
            accepted_all = false;
            break;
        }
        dest[len++] = (char)*p;
    }
    dest[len] = '\0';
    return accepted_all;
}

static bool append_alnum_text(char *dest, size_t dest_size, const char *src)
{
    bool accepted_all = true;
    size_t len = strlen(dest);
    for (const unsigned char *p = (const unsigned char *)src; *p; p++) {
        if (*p > 126 || !isalnum(*p)) {
            accepted_all = false;
            continue;
        }
        if (len + 1 >= dest_size) {
            accepted_all = false;
            break;
        }
        dest[len++] = (char)*p;
    }
    dest[len] = '\0';
    return accepted_all;
}

static void remove_last_char(char *text)
{
    size_t len = strlen(text);
    if (len > 0) {
        text[len - 1] = '\0';
    }
}

static void remove_last_utf8_char(char *text)
{
    size_t len = strlen(text);
    if (len == 0) {
        return;
    }
    size_t index = len - 1;
    while (index > 0 && (((unsigned char)text[index] & 0xC0) == 0x80)) {
        index--;
    }
    text[index] = '\0';
}

static void key_name_from_sdl(SDL_Keycode key, char *out, size_t out_size)
{
    if (key == SDLK_RSHIFT) {
        copy_text(out, out_size, "RSHIFT");
        return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        copy_text(out, out_size, "RETURN");
        return;
    }
    const char *name = SDL_GetKeyName(key);
    copy_text(out, out_size, name && name[0] ? name : "UNKNOWN");
}

static void scancode_name_from_sdl(SDL_Scancode scancode, char *out, size_t out_size)
{
    const char *name = SDL_GetScancodeName(scancode);
    if (name && name[0] && strcmp(name, "?") != 0) {
        copy_text(out, out_size, name);
        return;
    }
    snprintf(out, out_size, "SCANCODE %d", (int)scancode);
}

static void integral_keys_defaults(IntegralConfigKeys *keys)
{
    copy_text(keys->slot1, sizeof(keys->slot1), "RIGHT,LEFT,UP,DOWN,Z,X,RSHIFT,RETURN");
    copy_text(keys->slot2, sizeof(keys->slot2), "D,A,W,S,G,H,R,T");
    copy_text(keys->n64_p1, sizeof(keys->n64_p1), "D,A,W,S,RETURN,Z,LCTRL,LSHIFT,L,J,I,K,C,X,RIGHT,LEFT,UP,DOWN");
    copy_text(keys->fast, sizeof(keys->fast), "F");
    copy_text(keys->screenshot, sizeof(keys->screenshot), "P");
    copy_text(keys->escape, sizeof(keys->escape), "ESCAPE");
    copy_text(keys->turbo_hold, sizeof(keys->turbo_hold), "B");
    copy_text(keys->reset, sizeof(keys->reset), "I");
}

static void integral_keys_apply_defaults_for_missing(IntegralConfigKeys *keys)
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

static void key_spec_to_names_count(const char *spec,
                                    char names[][INTEGRAL_CONFIG_KEY_NAME_MAX],
                                    unsigned max_names)
{
    for (unsigned i = 0; i < max_names; i++) {
        names[i][0] = '\0';
    }
    char copy[INTEGRAL_CONFIG_KEY_SPEC_MAX];
    copy_text(copy, sizeof(copy), spec);
    unsigned count = 0;
    char *saveptr = NULL;
    for (char *token = strtok_r(copy, ",", &saveptr);
         token && count < max_names;
         token = strtok_r(NULL, ",", &saveptr)) {
        while (*token == ' ') {
            token++;
        }
        copy_text(names[count++], INTEGRAL_CONFIG_KEY_NAME_MAX, token);
    }
}

static void key_spec_to_names(const char *spec, char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX])
{
    key_spec_to_names_count(spec, names, INTEGRAL_KEY_BUTTONS);
}

static void format_slot_key_summary(const char *spec,
                                    char *line1,
                                    size_t line1_size,
                                    char *line2,
                                    size_t line2_size)
{
    char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    key_spec_to_names(spec, names);
    snprintf(line1,
             line1_size,
             "RIGHT=[%s],LEFT=[%s],UP=[%s],DOWN=[%s]",
             names[0][0] ? names[0] : "UNKNOWN",
             names[1][0] ? names[1] : "UNKNOWN",
             names[2][0] ? names[2] : "UNKNOWN",
             names[3][0] ? names[3] : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "A=[%s],B=[%s],SELECT=[%s],START=[%s]",
             names[4][0] ? names[4] : "UNKNOWN",
             names[5][0] ? names[5] : "UNKNOWN",
             names[6][0] ? names[6] : "UNKNOWN",
             names[7][0] ? names[7] : "UNKNOWN");
}

static void format_util_key_summary(const IntegralConfigKeys *keys,
                                    char *line1,
                                    size_t line1_size,
                                    char *line2,
                                    size_t line2_size,
                                    char *line3,
                                    size_t line3_size)
{
    snprintf(line1,
             line1_size,
             "FAST = [%s],SCREENSHOT = [%s]",
             keys->fast[0] ? keys->fast : "UNKNOWN",
             keys->screenshot[0] ? keys->screenshot : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "ESCAPE = [%s],TURBO = [%s]",
             keys->escape[0] ? keys->escape : "UNKNOWN",
             keys->turbo_hold[0] ? keys->turbo_hold : "UNKNOWN");
    snprintf(line3,
             line3_size,
             "RESET = [%s]",
             keys->reset[0] ? keys->reset : "UNKNOWN");
}

static void format_n64_key_summary(const char *spec,
                                   char *line1,
                                   size_t line1_size,
                                   char *line2,
                                   size_t line2_size,
                                   char *line3,
                                   size_t line3_size)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    key_spec_to_names_count(spec, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    snprintf(line1,
             line1_size,
             "D=[%s/%s/%s/%s] START=[%s]",
             names[0][0] ? names[0] : "UNKNOWN",
             names[1][0] ? names[1] : "UNKNOWN",
             names[2][0] ? names[2] : "UNKNOWN",
             names[3][0] ? names[3] : "UNKNOWN",
             names[4][0] ? names[4] : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "Z=[%s],B=[%s],A=[%s],C=[%s/%s/%s/%s]",
             names[5][0] ? names[5] : "UNKNOWN",
             names[6][0] ? names[6] : "UNKNOWN",
             names[7][0] ? names[7] : "UNKNOWN",
             names[8][0] ? names[8] : "UNKNOWN",
             names[9][0] ? names[9] : "UNKNOWN",
             names[10][0] ? names[10] : "UNKNOWN",
             names[11][0] ? names[11] : "UNKNOWN");
    snprintf(line3,
             line3_size,
             "R=[%s],L=[%s],ANALOG=[%s/%s/%s/%s]",
             names[12][0] ? names[12] : "UNKNOWN",
             names[13][0] ? names[13] : "UNKNOWN",
             names[14][0] ? names[14] : "UNKNOWN",
             names[15][0] ? names[15] : "UNKNOWN",
             names[16][0] ? names[16] : "UNKNOWN",
             names[17][0] ? names[17] : "UNKNOWN");
}

static void key_names_to_spec(char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX], char *out, size_t out_size)
{
    out[0] = '\0';
    for (unsigned i = 0; i < INTEGRAL_KEY_BUTTONS; i++) {
        if (i > 0) {
            append_text(out, out_size, ",");
        }
        append_text(out, out_size, names[i][0] ? names[i] : "UNKNOWN");
    }
}

static void key_names_to_spec_count(char names[][INTEGRAL_CONFIG_KEY_NAME_MAX],
                                    unsigned count,
                                    char *out,
                                    size_t out_size)
{
    out[0] = '\0';
    for (unsigned i = 0; i < count; i++) {
        if (i > 0) {
            append_text(out, out_size, ",");
        }
        append_text(out, out_size, names[i][0] ? names[i] : "UNKNOWN");
    }
}

static const char *key_config_step_label(KeyCaptureTarget target, unsigned step)
{
    static const char *buttons[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN"};
    static const char *n64_buttons[] = {
        "D-PAD RIGHT", "D-PAD LEFT", "D-PAD UP", "D-PAD DOWN",
        "START", "Z TRIGGER", "A BUTTON", "B BUTTON",
        "C RIGHT", "C LEFT", "C UP", "C DOWN",
        "R TRIGGER", "L TRIGGER", "ANALOG RIGHT", "ANALOG LEFT",
        "ANALOG UP", "ANALOG DOWN",
    };
    static const char *utils[] = {"FAST", "SCREENSHOT", "ESCAPE", "TURBO HOLD", "RESET"};
    if (target == KEY_CAPTURE_UTILS) {
        return step < INTEGRAL_UTIL_KEYS ? utils[step] : "DONE";
    }
    if (target == KEY_CAPTURE_N64) {
        return step < INTEGRAL_N64_RUNTIME_KEY_BUTTONS ? n64_buttons[step] : "DONE";
    }
    return step < INTEGRAL_KEY_BUTTONS ? buttons[step] : "DONE";
}

static unsigned key_spec_index_for_capture_step(unsigned step)
{
    static const unsigned order[] = {4, 5, 6, 7, 0, 1, 2, 3};
    return step < INTEGRAL_KEY_BUTTONS ? order[step] : 0;
}

static unsigned n64_key_spec_index_for_capture_step(unsigned step)
{
    if (step == 6u) return 7u; /* present A before B; canonical index 7 is A */
    if (step == 7u) return 6u; /* canonical index 6 is B */
    return step;
}

static void mask_password(const char *password, char *out, size_t out_size)
{
    size_t len = strlen(password);
    if (len >= out_size) {
        len = out_size - 1;
    }
    for (size_t i = 0; i < len; i++) {
        out[i] = '*';
    }
    out[len] = '\0';
}

static const char *login_server_id_or_default(const LoginState *state)
{
    return strcasecmp(state->server_id, INTEGRAL_SECONDARY_SERVER_ID) == 0
               ? INTEGRAL_SECONDARY_SERVER_ID
               : INTEGRAL_PRIMARY_SERVER_ID;
}

static void normalize_login_server_selection(LoginState *state)
{
    copy_text(state->server_id, sizeof(state->server_id), login_server_id_or_default(state));
}

static const char *login_server_label(const LoginState *state)
{
    return strcasecmp(login_server_id_or_default(state), INTEGRAL_SECONDARY_SERVER_ID) == 0
               ? "SECONDARY"
               : "PRIMARY";
}

static void cycle_login_server(LoginState *state)
{
    normalize_login_server_selection(state);
    const char *next_id = strcasecmp(
                              login_server_id_or_default(state),
                              INTEGRAL_PRIMARY_SERVER_ID
                          ) == 0
                              ? INTEGRAL_SECONDARY_SERVER_ID
                              : INTEGRAL_PRIMARY_SERVER_ID;
    copy_text(state->server_id, sizeof(state->server_id), next_id);
    snprintf(state->status, sizeof(state->status), "%s SELECTED", login_server_label(state));
}

static void login_state_init(LoginState *state)
{
    memset(state, 0, sizeof(*state));
    copy_text(state->server_id, sizeof(state->server_id), INTEGRAL_PRIMARY_SERVER_ID);
    copy_text(state->status, sizeof(state->status), "ENTER SERVER URL");
    state->selected = FIELD_SERVER;
}

static void password_change_state_init(PasswordChangeState *state)
{
    memset(state, 0, sizeof(*state));
    copy_text(state->status, sizeof(state->status), "SET NEW PASSWORD");
    state->selected = PASSWORD_CHANGE_NEW;
}

static void sanitize_config_name(const char *src, char *out, size_t out_size)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && used + 1 < out_size; p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            out[used++] = (char)tolower(*p);
        }
    }
    if (used == 0) {
        copy_text(out, out_size, "user");
        return;
    }
    out[used] = '\0';
}

static void build_user_config_path(const char *base_path, const char *username, const char *server_id, char *out, size_t out_size)
{
    char safe_user[64];
    char safe_server[32];
    sanitize_config_name(username, safe_user, sizeof(safe_user));
    sanitize_config_name(server_id && server_id[0] ? server_id : "primary", safe_server, sizeof(safe_server));
    const char *slash = strrchr(base_path, '/');
    const char *dot = strrchr(base_path, '.');
    if (dot && (!slash || dot > slash)) {
        size_t prefix_len = (size_t)(dot - base_path);
        snprintf(out, out_size, "%.*s_%s_%s%s", (int)prefix_len, base_path, safe_server, safe_user, dot);
    }
    else {
        snprintf(out, out_size, "%s_%s_%s", base_path, safe_server, safe_user);
    }
}

static void validate_local_indices(AppState *state)
{
    for (unsigned i = 0; i < 2; i++) {
        int index = state->local_slot_indices[i];
        if (index < 0 || index >= INTEGRAL_ROM_SLOTS || !slot_is_supported_gb(&state->rom_slots[index])) {
            state->local_slot_indices[i] = -1;
        }
    }
    if (state->local_slot_indices[0] >= 0 &&
        state->local_slot_indices[0] == state->local_slot_indices[1]) {
        state->local_slot_indices[1] = -1;
    }
    if (state->room_slot_index < 0 ||
        state->room_slot_index >= INTEGRAL_ROM_SLOTS ||
        !slot_is_supported_gb(&state->rom_slots[state->room_slot_index])) {
        state->room_slot_index = -1;
    }
}

static void init_n64_runtime_selection(AppState *state)
{
    state->integral_n64_runtime_selected = 0;
    state->integral_n64_runtime_n64_slot_index = -1;
    for (unsigned i = 0; i < 4; i++) {
        state->integral_n64_runtime_transfer_slot_indices[i] = -1;
    }
    for (int i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (slot_is_supported_n64(&state->rom_slots[i])) {
            state->integral_n64_runtime_n64_slot_index = i;
            break;
        }
    }
    for (unsigned i = 0; i < 2; i++) {
        int index = state->local_slot_indices[i];
        if (index >= 0 && index < INTEGRAL_ROM_SLOTS && slot_is_supported_gb(&state->rom_slots[index])) {
            state->integral_n64_runtime_transfer_slot_indices[i] = index;
        }
    }
}

static void load_active_user_config(AppState *state)
{
    memset(state->rom_slots, 0, sizeof(state->rom_slots));
    state->local_slot_indices[0] = -1;
    state->local_slot_indices[1] = -1;
    state->room_slot_index = -1;
    state->n64_room_n64_slot_index = -1;
    state->n64_room_user1_gb_slot_index = -1;
    state->n64_room_user2_gb_slot_index = -1;

    IntegralConfigLocal local_config;
    if (integral_config_load_local(state->config_path, &local_config) == 0) {
        state->local_slot_indices[0] = local_config.slot1_index;
        state->local_slot_indices[1] = local_config.slot2_index;
    }
    integral_keys_defaults(&state->keys);
    if (integral_config_load_keys(state->config_path, &state->keys) == 0) {
        integral_keys_apply_defaults_for_missing(&state->keys);
    }
    if (integral_config_load_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "CONFIG LOAD FAILED");
    }
    if (discard_pending_rom_slots(state)) {
        (void)integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS);
    }
    validate_local_indices(state);
    init_n64_runtime_selection(state);
}

static void activate_user_config(AppState *state)
{
    build_user_config_path(state->base_config_path,
                           state->login.username,
                           login_server_id_or_default(&state->login),
                           state->config_path,
                           sizeof(state->config_path));
    load_active_user_config(state);
}

static void app_state_init(AppState *state, const char *config_path)
{
    memset(state, 0, sizeof(*state));
    state->screen = SCREEN_LOGIN;
    login_state_init(&state->login);
    password_change_state_init(&state->password_change);
    copy_text(state->base_config_path, sizeof(state->base_config_path), config_path);
    copy_text(state->config_path, sizeof(state->config_path), config_path);
    state->local_slot_indices[0] = -1;
    state->local_slot_indices[1] = -1;
    state->room_slot_index = -1;
    state->n64_room_n64_slot_index = -1;
    state->n64_room_user1_gb_slot_index = -1;
    state->n64_room_user2_gb_slot_index = -1;
    init_n64_runtime_selection(state);
    integral_keys_defaults(&state->keys);
    if (integral_config_load_keys(state->base_config_path, &state->keys) == 0) {
        integral_keys_apply_defaults_for_missing(&state->keys);
    }
    IntegralConfigLogin login_config;
    if (integral_config_load_login(state->base_config_path, &login_config) == 0) {
        if (login_config.server[0] != '\0') {
            copy_text(state->login.server, sizeof(state->login.server), login_config.server);
        }
        if (login_config.server_id[0] != '\0') {
            copy_text(state->login.server_id, sizeof(state->login.server_id), login_config.server_id);
        }
        if (login_config.remember) {
            copy_text(state->login.username, sizeof(state->login.username), login_config.username);
            bool credential_loaded = integral_credential_store_load(
                                         state->login.server,
                                         state->login.username,
                                         state->login.password,
                                         sizeof(state->login.password)
                                     ) == 0;
            state->login.remember_login = credential_loaded;
            copy_text(
                state->login.status,
                sizeof(state->login.status),
                credential_loaded ? "SAVED LOGIN LOADED" : "SAVED CREDENTIAL UNAVAILABLE"
            );
        }
    }
    normalize_login_server_selection(&state->login);
    if (state->login.server[0] != '\0') {
        state->login.selected = FIELD_USERNAME;
    }
}

static void draw_panel(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color border)
{
    SDL_SetRenderDrawColor(renderer, 8, 12, 16, 255);
    SDL_Rect fill = {.x = x, .y = y, .w = w, .h = h};
    SDL_RenderFillRect(renderer, &fill);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderDrawRect(renderer, &fill);
}

static void draw_field(SDL_Renderer *renderer,
                       int y,
                       const char *label,
                       const char *value,
                       bool selected,
                       bool editing)
{
    SDL_Color label_color = {160, 180, 196, 255};
    SDL_Color value_color = {238, 238, 238, 255};
    SDL_Color selected_color = {86, 162, 126, 255};
    SDL_Color cursor_color = {230, 92, 76, 255};
    SDL_Color active_fill = {112, 38, 44, 255};
    SDL_Color box_color = selected ? selected_color : (SDL_Color){55, 64, 70, 255};

    if (selected) {
        if (editing) {
            SDL_SetRenderDrawColor(renderer, active_fill.r, active_fill.g, active_fill.b, active_fill.a);
        }
        else {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        }
        SDL_Rect highlight = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 50};
        SDL_RenderFillRect(renderer, &highlight);
        integral_sdl_draw_text(renderer, 24, y + 8, ">", 2, selected_color);
    }

    integral_sdl_draw_text(renderer, 48, y, label, 2, selected ? selected_color : label_color);
    SDL_SetRenderDrawColor(renderer, box_color.r, box_color.g, box_color.b, box_color.a);
    SDL_Rect box = {.x = 48, .y = y + 22, .w = 384, .h = 24};
    SDL_RenderDrawRect(renderer, &box);
    integral_sdl_draw_text_fit(renderer, 56, y + 28, value[0] ? value : "<EMPTY>", 1, value_color, 368);
    if (selected && editing) {
        SDL_SetRenderDrawColor(renderer, cursor_color.r, cursor_color.g, cursor_color.b, cursor_color.a);
        SDL_Rect cursor = {.x = 420, .y = y + 26, .w = 3, .h = 17};
        SDL_RenderFillRect(renderer, &cursor);
    }
}

static void draw_marquee_text_fit(SDL_Renderer *renderer,
                                  int x,
                                  int y,
                                  const char *text,
                                  int scale,
                                  SDL_Color color,
                                  int max_width,
                                  unsigned phase_seed);

static void draw_header(SDL_Renderer *renderer, const char *subtitle, const char *login_id, const char *server_url)
{
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color user = {160, 180, 196, 255};

    integral_sdl_draw_text(renderer, 22, 20, "INTEGRAL EMULATOR", 3, title);
    integral_sdl_draw_text(renderer, 330, 26, "VER " INTEGRAL_CLIENT_VERSION, 2, muted);
    if (login_id && login_id[0] != '\0') {
        char user_text[96];
        snprintf(user_text, sizeof(user_text), "ID %s", login_id);
        integral_sdl_draw_text_fit(renderer, 330, 50, user_text, 1, user, 128);
    }
    if (server_url && server_url[0] != '\0') {
        char server_text[160];
        snprintf(server_text, sizeof(server_text), "SERVER %s", server_url);
        draw_marquee_text_fit(renderer, 330, 68, server_text, 1, user, 128, 0u);
    }
    integral_sdl_draw_text(renderer, 24, 62, subtitle, 2, muted);
}

static void draw_login(SDL_Renderer *renderer, const LoginState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color value = {238, 238, 238, 255};

    draw_header(renderer, "ACCOUNT LOGIN", NULL, NULL);

    LoginState mutable_state = *state;
    char password_display[80];
    if (state->password_visible) {
        copy_text(password_display, sizeof(password_display), state->password);
    }
    else {
        mask_password(state->password, password_display, sizeof(password_display));
    }

    draw_field(renderer, 88, "SERVER", mutable_state.server, state->selected == FIELD_SERVER, state->editing);
    draw_field(renderer, 142, "ENV", login_server_label(state), state->selected == FIELD_ENV, false);
    draw_field(renderer, 196, "USERNAME", mutable_state.username, state->selected == FIELD_USERNAME, state->editing);
    draw_field(renderer, 250, "PASSWORD", password_display, state->selected == FIELD_PASSWORD, state->editing);

    if (state->selected == FIELD_ACTION) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect highlight = {.x = 112, .y = 352, .w = 180, .h = 42};
        SDL_RenderFillRect(renderer, &highlight);
        integral_sdl_draw_text(renderer, 122, 364, ">", 3, selected);
        integral_sdl_draw_text(renderer, 140, 364, "LOGIN", 3, selected);
    }
    else {
        draw_panel(renderer, 112, 352, 180, 42, (SDL_Color){55, 64, 70, 255});
        integral_sdl_draw_text(renderer, 140, 364, "LOGIN", 3, value);
    }

    if (state->selected == FIELD_REMEMBER) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect highlight = {.x = 14, .y = 306, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 34};
        SDL_RenderFillRect(renderer, &highlight);
        integral_sdl_draw_text(renderer, 24, 316, ">", 1, selected);
    }
    integral_sdl_draw_text(renderer,
                      48,
                      316,
                      state->remember_login ? "[ON] REMEMBER LOGIN" : "[OFF] REMEMBER LOGIN",
                      1,
                      state->selected == FIELD_REMEMBER ? selected : value);

    integral_sdl_draw_text_fit(renderer, 22, 408, state->status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  LEFT/RIGHT ENV  F2 EDIT  F3 SHOW PASS", 1, muted);
    integral_sdl_draw_text(renderer, 22, 458, "ESC CANCEL", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_password_change(SDL_Renderer *renderer, const AppState *app)
{
    const PasswordChangeState *state = &app->password_change;
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color value = {238, 238, 238, 255};

    draw_header(renderer, "CHANGE PASSWORD", app->login.username, app->login.server);
    integral_sdl_draw_text(renderer, 24, 88, "INITIAL PASSWORD MUST BE CHANGED", 1, muted);

    char new_display[80];
    char confirm_display[80];
    if (state->password_visible) {
        copy_text(new_display, sizeof(new_display), state->new_password);
        copy_text(confirm_display, sizeof(confirm_display), state->confirm_password);
    }
    else {
        mask_password(state->new_password, new_display, sizeof(new_display));
        mask_password(state->confirm_password, confirm_display, sizeof(confirm_display));
    }

    draw_field(renderer,
               132,
               "NEW PASSWORD",
               new_display,
               state->selected == PASSWORD_CHANGE_NEW,
               state->editing);
    draw_field(renderer,
               202,
               "CONFIRM",
               confirm_display,
               state->selected == PASSWORD_CHANGE_CONFIRM,
               state->editing);

    if (state->selected == PASSWORD_CHANGE_SAVE) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect highlight = {.x = 14, .y = 304, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 42};
        SDL_RenderFillRect(renderer, &highlight);
        integral_sdl_draw_text(renderer, 132, 316, "> SAVE", 3, selected);
    }
    else {
        draw_panel(renderer, 112, 304, 180, 42, (SDL_Color){55, 64, 70, 255});
        integral_sdl_draw_text(renderer, 144, 316, "SAVE", 3, value);
    }
    integral_sdl_draw_text_fit(renderer, 22, 392, state->status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 420, "ALNUM ONLY  8+ CHARS", 1, muted);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT  F2 EDIT", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_main_menu(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "MAIN MENU", state->login.username, state->login.server);
    const char *labels[] = {
        "LOCAL",
        "CREATE ROOM",
        "JOIN ROOM",
        "ROM REGISTER",
        "KEY CONFIG",
        "SCREENSHOTS",
    };
    const char *values[] = {
        "PLAY ON THIS MACHINE",
        "GET A 5 DIGIT CODE",
        "ENTER A 5 DIGIT CODE",
        "MANAGE 8 ROM SLOTS",
        "",
        "",
    };

    for (unsigned i = 0; i < INTEGRAL_MAIN_ROWS; i++) {
        int y = 88 + (int)i * 51;
        if (state->main_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 46};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 6, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y, labels[i], 2, state->main_selected == i ? selected : label);
        integral_sdl_draw_text_fit(renderer, 48, y + 24, values[i], 1, value, 380);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC LOGOUT", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_room_mode(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    const char *mode = state->room_mode_selected == 0 ? "LINK CABLE" : "N64";

    draw_header(renderer, "CREATE ROOM", state->login.username, state->login.server);
    integral_sdl_draw_text(renderer, 48, 150, "MODE", 2, label);
    integral_sdl_draw_text(renderer, 144, 150, ":", 2, label);
    integral_sdl_draw_text(renderer, 176, 150, mode, 2, value);

    SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
    SDL_Rect create = {.x = 14, .y = 222, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 58};
    SDL_RenderFillRect(renderer, &create);
    integral_sdl_draw_text(renderer, 24, 239, ">", 2, selected);
    integral_sdl_draw_text(renderer, 48, 234, "CREATE", 3, selected);

    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "LEFT/RIGHT MODE  ENTER CREATE", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MAIN MENU", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_join_room(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    draw_header(renderer, "JOIN ROOM", state->login.username, state->login.server);
    integral_sdl_draw_text(renderer, 48, 150, "ROOM CODE", 2, label);
    SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
    SDL_Rect input = {.x = 46, .y = 188, .w = 388, .h = 72};
    SDL_RenderFillRect(renderer, &input);
    char display[16];
    snprintf(display, sizeof(display), "%s%s", state->room_code_input,
             state->room_code_editing ? "_" : "");
    integral_sdl_draw_text_fit(renderer, 154, 208,
                               display[0] ? display : "-----", 4,
                               state->room_code_editing ? selected : value, 220);
    integral_sdl_draw_text(renderer, 48, 272, "5 DIGITS", 1, muted);
    integral_sdl_draw_text(renderer, 48, 294, "SAVE DATA NOTICE", 1, label);
    const char *notice_lines[] = {
        "WHEN LINK PLAY STARTS, YOUR SELECTED SAV DATA",
        "WILL BE SENT TEMPORARILY TO THE ROOM HOST.",
        "THE OFFICIAL CLIENT USES IT ONLY IN EMULATOR",
        "MEMORY AND DOES NOT SAVE IT AS A FILE ON THE",
        "HOST PC. ONLY JOIN A ROOM CODE RECEIVED FROM",
        "SOMEONE YOU TRUST.",
    };
    for (size_t i = 0; i < sizeof(notice_lines) / sizeof(notice_lines[0]); i++) {
        integral_sdl_draw_text_fit(renderer, 48, 314 + (int)i * 14,
                                   notice_lines[i], 1, muted,
                                   INTEGRAL_WINDOW_WIDTH - 72);
    }
    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted,
                               INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "ENTER JOIN  BACKSPACE CLEAR", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MAIN MENU", 1, muted);
    SDL_RenderPresent(renderer);
}

static const IntegralConfigRomSlot *registered_rom_slot_at(const AppState *state, int index)
{
    if (index < 0 || index >= INTEGRAL_ROM_SLOTS) {
        return NULL;
    }
    if (state->rom_slots[index].rom_path[0] == '\0') {
        return NULL;
    }
    if (state->rom_slots[index].rom_id[0] == '\0' || state->rom_slots[index].save_id[0] == '\0') {
        return NULL;
    }
    return &state->rom_slots[index];
}

static bool slot_is_supported_n64(const IntegralConfigRomSlot *slot)
{
    RomHeaderInfo header;
    return slot && slot->rom_path[0] != '\0' &&
           read_supported_rom_header(slot->rom_path, &header) == 0 &&
           strcmp(header.platform, "n64") == 0;
}

static bool slot_is_supported_gb(const IntegralConfigRomSlot *slot)
{
    RomHeaderInfo header;
    return slot && slot->rom_path[0] != '\0' &&
           read_supported_rom_header(slot->rom_path, &header) == 0 &&
           strcmp(header.platform, "gb") == 0;
}

static const IntegralConfigRomSlot *local_selected_rom_slot(const AppState *state, unsigned local_slot)
{
    if (local_slot >= 2) {
        return NULL;
    }
    int index = state->local_slot_indices[local_slot];
    if (index < 0 || index >= INTEGRAL_ROM_SLOTS || !slot_is_supported_gb(&state->rom_slots[index])) {
        return NULL;
    }
    return registered_rom_slot_at(state, index);
}

static const IntegralConfigRomSlot *selected_n64_rom_slot(const AppState *state)
{
    int index = state->integral_n64_runtime_n64_slot_index;
    if (index >= 0 && index < INTEGRAL_ROM_SLOTS && slot_is_supported_n64(&state->rom_slots[index])) {
        return &state->rom_slots[index];
    }
    return NULL;
}

static const IntegralConfigRomSlot *selected_transfer_rom_slot(const AppState *state, unsigned transfer_slot)
{
    if (transfer_slot >= 4) {
        return NULL;
    }
    int index = state->integral_n64_runtime_transfer_slot_indices[transfer_slot];
    for (unsigned i = 0; i < 4; i++) {
        if (i != transfer_slot && state->integral_n64_runtime_transfer_slot_indices[i] == index) {
            return NULL;
        }
    }
    if (index >= 0 && index < INTEGRAL_ROM_SLOTS && registered_rom_slot_at(state, index) &&
        slot_is_supported_gb(&state->rom_slots[index])) {
        return &state->rom_slots[index];
    }
    return NULL;
}

static void format_local_slot_label(const AppState *state, unsigned local_slot, const char *label, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = local_selected_rom_slot(state, local_slot);
    if (!slot) {
        snprintf(out, out_size, "%s EMPTY", label);
        return;
    }
    snprintf(out, out_size, "%s ROM%d", label, state->local_slot_indices[local_slot] + 1);
}

static void format_local_slot_detail(const AppState *state, unsigned local_slot, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = local_selected_rom_slot(state, local_slot);
    if (!slot) {
        copy_text(out, out_size, "LEFT/RIGHT SELECT ROM1-8");
        return;
    }
    copy_text(out, out_size, path_file_name(slot->rom_path));
}

static void refresh_mobile_scenarios(AppState *state)
{
    const IntegralConfigRomSlot *slot = local_selected_rom_slot(state, 0);
    state->mobile_scenario_count = 0;
    state->mobile_scenario_selected = 0;
    state->mobile_scenario_rom_id[0] = '\0';
    state->mobile_scenario_save_id[0] = '\0';
    if (!slot || !slot->rom_id[0] || !slot->save_id[0] || !state->login.token[0]) {
        copy_text(state->login.status, sizeof(state->login.status), "SELECT A REGISTERED MOBILE ROM");
        return;
    }
    char error[160];
    if (integral_api_list_mobile_scenarios(
            state->login.server, state->login.token, slot->save_id, slot->rom_id,
            state->mobile_scenarios, &state->mobile_scenario_count,
            error, sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "SCENARIO UNAVAILABLE %s", error);
        return;
    }
    for (unsigned i = 0; i < state->mobile_scenario_count; i++) {
        if (state->mobile_scenarios[i].is_default) {
            state->mobile_scenario_selected = i;
            break;
        }
    }
    copy_text(state->mobile_scenario_rom_id, sizeof(state->mobile_scenario_rom_id), slot->rom_id);
    copy_text(state->mobile_scenario_save_id, sizeof(state->mobile_scenario_save_id), slot->save_id);
    snprintf(state->login.status, sizeof(state->login.status), "SCENARIO %s",
             state->mobile_scenarios[state->mobile_scenario_selected].display_name);
}

static void cycle_mobile_scenario(AppState *state, int delta)
{
    if (state->mobile_scenario_count == 0u) {
        refresh_mobile_scenarios(state);
        return;
    }
    int next = (int)state->mobile_scenario_selected + delta;
    if (next < 0) next = (int)state->mobile_scenario_count - 1;
    if (next >= (int)state->mobile_scenario_count) next = 0;
    state->mobile_scenario_selected = (unsigned)next;
    snprintf(state->login.status, sizeof(state->login.status), "SCENARIO %s",
             state->mobile_scenarios[state->mobile_scenario_selected].display_name);
}

static void format_room_slot_label(const AppState *state, char *out, size_t out_size)
{
    if (!registered_rom_slot_at(state, state->room_slot_index)) {
        copy_text(out, out_size, "SLOT <EMPTY>");
        return;
    }
    snprintf(out, out_size, "SLOT ROM%d", state->room_slot_index + 1);
}

static void format_room_slot_detail(const AppState *state, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = registered_rom_slot_at(state, state->room_slot_index);
    if (!slot) {
        copy_text(out, out_size, "LEFT/RIGHT SELECT ROM1-8");
        return;
    }
    copy_text(out, out_size, path_file_name(slot->rom_path));
}

static int current_room_user_position(const AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS ||
        state->login.username[0] == '\0') {
        return 0;
    }
    const IntegralApiRoom *room = &state->current_room;
    if (room->user1[0] != '\0' && strcasecmp(state->login.username, room->user1) == 0) {
        return 1;
    }
    if (room->user2[0] != '\0' && strcasecmp(state->login.username, room->user2) == 0) {
        return 2;
    }
    return 0;
}

static bool current_room_is_user1(const AppState *state)
{
    return current_room_user_position(state) == 1;
}

static void set_room_link_session_id(AppState *state, const char *session_id)
{
    if (!session_id || session_id[0] == '\0') {
        return;
    }
    if (strcmp(state->room_link_session_id, session_id) == 0) {
        return;
    }
    client_log(state,
               "room_session_set",
               "old_session=%s new_session=%s",
               state->room_link_session_id[0] ? state->room_link_session_id : "-",
               session_id);
    copy_text(state->room_link_session_id, sizeof(state->room_link_session_id), session_id);
    state->room_game_session_id[0] = '\0';
    state->room_fencing_token = 0;
    state->room_client_started = false;
    state->room_gb_runtime_fixed_host_active = false;
    state->room_gb_runtime_fixed_host_role[0] = '\0';
    state->room_gb_runtime_fixed_host_result_read = -1;
    state->room_gb_runtime_fixed_host_save_policy[0] = '\0';
    state->room_game_ended = false;
    state->room_link_mode_local_override = false;
    state->room_client_pid = 0;
    state->room_session_missing_since_ticks = 0;
    state->room_heartbeat_failures = 0;
    state->room_heartbeat_attempted = false;
    state->room_lifecycle_status[0] = '\0';
    state->room_termination_reason[0] = '\0';
    state->room_remaining_seconds = -1;
}

static void clear_room_link_session_id(AppState *state)
{
    if (state->room_link_session_id[0] == '\0') {
        return;
    }
    client_log(state, "room_session_clear", "session=%s", state->room_link_session_id);
    state->room_link_session_id[0] = '\0';
    state->room_game_session_id[0] = '\0';
    state->room_fencing_token = 0;
    state->room_start_requested = false;
    state->room_client_started = false;
    state->room_gb_runtime_fixed_host_active = false;
    state->room_gb_runtime_fixed_host_role[0] = '\0';
    state->room_gb_runtime_fixed_host_result_read = -1;
    state->room_gb_runtime_fixed_host_save_policy[0] = '\0';
    state->room_game_ended = false;
    state->room_link_mode_local_override = false;
    state->room_client_pid = 0;
    state->room_session_missing_since_ticks = 0;
}

static bool link_session_status_is_active(const char *status)
{
    return strcmp(status, "CREATED") == 0 || strcmp(status, "WAITING_PLAYER_A") == 0 ||
           strcmp(status, "WAITING_PLAYER_B") == 0 || strcmp(status, "PREPARING") == 0 ||
           strcmp(status, "RUNNING") == 0 ||
           strcmp(status, "FINALIZING") == 0 || strcmp(status, "RECOVERING") == 0;
}

static void mark_room_game_ended_if_used(AppState *state);

static void load_room_chat_from_api(AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS) {
        return;
    }
    const IntegralApiRoom *room = &state->current_room;
    mark_room_game_ended_if_used(state);
    if (room->link_session_id[0] != '\0') {
        set_room_link_session_id(state, room->link_session_id);
        state->room_session_missing_since_ticks = 0;
    }
    else if (state->room_link_session_id[0] == '\0') {
        clear_room_link_session_id(state);
    }
    else if (state->room_game_ended) {
        client_log(state,
                   "room_session_clear_server_ended",
                   "session=%s game_ended=%d",
                   state->room_link_session_id,
                   state->room_game_ended ? 1 : 0);
        clear_room_link_session_id(state);
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED");
    }
    else {
        Uint32 now = SDL_GetTicks();
        if (state->room_session_missing_since_ticks == 0) {
            state->room_session_missing_since_ticks = now ? now : 1;
        }
        char status[32];
        char error[160];
        int session_room_number = 0;
        int get_rc = integral_api_get_link_session_info(state->login.server,
                                                   state->login.token,
                                                   state->room_link_session_id,
                                                   status,
                                                   sizeof(status),
                                                   &session_room_number,
                                                   error,
                                                   sizeof(error));
        Uint32 missing_for = now - state->room_session_missing_since_ticks;
        if (get_rc != 0) {
            client_log(state,
                       "room_session_verify_failed",
                       "session=%s missing_ms=%u error=%s",
                       state->room_link_session_id,
                       (unsigned)missing_for,
                       error);
            if (missing_for >= 5000u) {
                clear_room_link_session_id(state);
            }
        }
        else if (!link_session_status_is_active(status) || session_room_number != (int)state->room_number) {
            client_log(state,
                       "room_session_clear_verified_missing",
                       "session=%s status=%s session_room=%d current_room=%u missing_ms=%u",
                       state->room_link_session_id,
                       status,
                       session_room_number,
                       state->room_number,
                       (unsigned)missing_for);
            clear_room_link_session_id(state);
        }
        else {
            state->room_session_missing_since_ticks = 0;
            client_log(state,
                       "room_session_keep_verified_active",
                       "session=%s status=%s room=%d missing_ms=%u",
                       state->room_link_session_id,
                       status,
                       session_room_number,
                       (unsigned)missing_for);
        }
    }
    memset(state->room_chat_log, 0, sizeof(state->room_chat_log));
    state->room_chat_scroll = 0;
    if (room->chat_count == 0) {
        return;
    }
    unsigned start = room->chat_count > INTEGRAL_CHAT_LOG_LINES ? room->chat_count - INTEGRAL_CHAT_LOG_LINES : 0;
    unsigned out = INTEGRAL_CHAT_LOG_LINES - (room->chat_count - start);
    for (unsigned i = start; i < room->chat_count && out < INTEGRAL_CHAT_LOG_LINES; i++) {
        copy_text(state->room_chat_log[out++], INTEGRAL_CHAT_MESSAGE_MAX, room->chat[i]);
    }
}

static void sync_room_ready_flags_from_api(AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS || state->login.username[0] == '\0') {
        return;
    }
    const IntegralApiRoom *room = &state->current_room;
    int user_position = current_room_user_position(state);
    if (user_position != 1) {
        state->room_link_mode_local_override = false;
    }
    if (room->link_mode[0] != '\0') {
        IntegralRoomLinkMode server_mode;
        if (!room_link_mode_from_api(room->link_mode, &server_mode)) {
            copy_text(state->login.status, sizeof(state->login.status), "ROOM MODE INVALID");
            client_log(state, "room_mode_invalid", "value=%s", room->link_mode);
            return;
        }
        if (state->room_link_mode_local_override && server_mode == state->room_link_mode) {
            state->room_link_mode_local_override = false;
        }
        if (!state->room_link_mode_local_override || current_room_game_ended(state) || room->link_session_id[0] != '\0') {
            state->room_link_mode = server_mode;
        }
    }
    if (user_position == 1) {
        state->room_ready_self = room->ready1 != 0;
        state->room_ready_peer = room->ready2 != 0;
    }
    else if (user_position == 2) {
        state->room_ready_self = room->ready2 != 0;
        state->room_ready_peer = room->ready1 != 0;
    }
    if (state->room_number >= 65 && state->room_number <= 128) {
        state->n64_room_ready = state->room_ready_self;
    }
}

static unsigned chat_message_count(char log[INTEGRAL_CHAT_LOG_LINES][INTEGRAL_CHAT_MESSAGE_MAX])
{
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES; i++) {
        if (log[i][0] != '\0') {
            count++;
        }
    }
    return count;
}

static void draw_local_mode(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "LOCAL", state->login.username, state->login.server);
    const char *labels[] = {
        "GB MODE",
        "MOBILE MODE",
        "N64 MODE",
    };
    const char *details[] = {
        "GAME BOY / GAME BOY COLOR",
        "SERVER-SELECTED GAME PROFILE",
        "NINTENDO 64",
    };

    for (unsigned i = 0; i < INTEGRAL_LOCAL_MODE_ROWS; i++) {
        int y = 126 + (int)i * 76;
        if (state->local_mode_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 10, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 58};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 6, ">", 2, selected);
        }
        integral_sdl_draw_text_fit(renderer,
                                   48,
                                   y,
                                   labels[i],
                                   2,
                                   state->local_mode_selected == i ? selected : label,
                                   390);
        integral_sdl_draw_text_fit(renderer, 48, y + 28, details[i], 1, value, 390);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER SELECT", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC MENU", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_gb_slot_screen(SDL_Renderer *renderer, const AppState *state, bool mobile_mode)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer,
                mobile_mode ? "MOBILE MODE" : "GB MODE",
                state->login.username,
                state->login.server);

    char slot1[220];
    char slot1_detail[220];
    format_local_slot_label(state, 0, "SLOT1", slot1, sizeof(slot1));
    format_local_slot_detail(state, 0, slot1_detail, sizeof(slot1_detail));
    char slot2[220] = "";
    char slot2_detail[220] = "";
    if (!mobile_mode) {
        format_local_slot_label(state, 1, "SLOT2", slot2, sizeof(slot2));
        format_local_slot_detail(state, 1, slot2_detail, sizeof(slot2_detail));
    }
    char scenario_detail[96] = "SELECT SLOT1 FIRST";
    if (mobile_mode && state->mobile_scenario_count > 0u &&
        state->mobile_scenario_selected < state->mobile_scenario_count) {
        copy_text(scenario_detail, sizeof(scenario_detail),
                  state->mobile_scenarios[state->mobile_scenario_selected].display_name);
    }
    else if (mobile_mode && local_selected_rom_slot(state, 0)) {
        copy_text(scenario_detail, sizeof(scenario_detail), "NOT AVAILABLE");
    }
    const char *labels[] = {"START", slot1, mobile_mode ? "SCENARIO" : slot2};
    const char *details[] = {
        mobile_mode ? "MOBILE ADAPTER GB" : (local_selected_rom_slot(state, 1) ? "SERVER2 MODE" : "SELF MODE"),
        slot1_detail,
        mobile_mode ? scenario_detail : slot2_detail,
    };
    unsigned row_count = mobile_mode ? INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS : INTEGRAL_GB_RUNTIME_MODE_ROWS;

    for (unsigned i = 0; i < row_count; i++) {
        int y = 118 + (int)i * 76;
        if (state->local_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 10, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 58};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 6, ">", 2, selected);
        }
        integral_sdl_draw_text_fit(renderer, 48, y, labels[i], 2, state->local_selected == i ? selected : label, 390);
        integral_sdl_draw_text_fit(renderer, 48, y + 28, details[i], 1, value, 390);
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER START  LEFT/RIGHT ROM1-8", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "BACKSPACE CLEAR SLOT  ESC LOCAL MODES", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_local(SDL_Renderer *renderer, const AppState *state)
{
    draw_gb_slot_screen(renderer, state, false);
}

static void draw_gb_mobile(SDL_Renderer *renderer, const AppState *state)
{
    draw_gb_slot_screen(renderer, state, true);
}

static void draw_n64_runtime(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "N64 MODE", state->login.username, state->login.server);

    char n64_label[240];
    const IntegralConfigRomSlot *n64_slot = selected_n64_rom_slot(state);
    snprintf(n64_label,
             sizeof(n64_label),
             "N64 SLOT %s",
             n64_slot ? path_file_name(n64_slot->rom_path) : "<EMPTY>");
    char transfer_labels[4][240];
    for (unsigned i = 0; i < 4; i++) {
        int index = state->integral_n64_runtime_transfer_slot_indices[i];
        const IntegralConfigRomSlot *slot = selected_transfer_rom_slot(state, i);
        if (slot) {
            snprintf(transfer_labels[i], sizeof(transfer_labels[i]), "SLOT%u ROM%d", i + 1, index + 1);
        }
        else {
            snprintf(transfer_labels[i], sizeof(transfer_labels[i]), "SLOT%u <EMPTY>", i + 1);
        }
    }

    const char *labels[] = {
        "START",
        n64_label,
        transfer_labels[0],
        transfer_labels[1],
        transfer_labels[2],
        transfer_labels[3],
    };

    for (unsigned i = 0; i < 6; i++) {
        int y = 102 + (int)i * 48;
        if (state->integral_n64_runtime_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 38};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 2, ">", 2, selected);
        }
        integral_sdl_draw_text_fit(renderer, 48, y, labels[i], 2, state->integral_n64_runtime_selected == i ? selected : label, 390);
        if (i == 1 && n64_slot) {
            RomHeaderInfo header;
            if (read_supported_rom_header(n64_slot->rom_path, &header) == 0) {
                integral_sdl_draw_text_fit(renderer, 48, y + 24, header.header_title, 1, value, 390);
            }
        }
        else if (i >= 2) {
            const IntegralConfigRomSlot *slot = selected_transfer_rom_slot(state, i - 2);
            if (slot) {
                integral_sdl_draw_text_fit(renderer, 48, y + 24, path_file_name(slot->rom_path), 1, value, 390);
            }
        }
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER START/SELECT  LEFT/RIGHT ROM1-8", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC LOCAL MODES", 1, muted);
    SDL_RenderPresent(renderer);
}

static bool room_status_is_actionable_error(const char *status)
{
    if (!status || status[0] == '\0') {
        return false;
    }
    return ((strncmp(status, "ROOM ", 5) == 0 &&
             (strstr(status, "FAILED") || strstr(status, "REQUIRED") || strstr(status, "NOT MATCHED"))) ||
            strncmp(status, "HOST START FAILED", 17) == 0 ||
            strncmp(status, "LINK START FAILED", 17) == 0 ||
            strncmp(status, "LINK NODE", 9) == 0 ||
            strncmp(status, "PROTOCOL CHECK FAILED", 21) == 0 ||
            strcmp(status, "CLIENT CAPABILITY MISMATCH") == 0 ||
            strcmp(status, "WAITING PEER ROM") == 0);
}

static bool current_room_game_ended(const AppState *state)
{
    if (state->room_game_ended) {
        return true;
    }
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS) {
        return false;
    }
    const IntegralApiRoom *room = &state->current_room;
    return room->game_started && room->link_session_id[0] == '\0';
}

static void mark_room_game_ended_if_used(AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS) {
        return;
    }
    const IntegralApiRoom *room = &state->current_room;
    if (state->screen == SCREEN_N64_ROOM || strcmp(room->room_type, "n64") == 0) {
        /* N64 media sessions do not use a Link Session ID. Their terminal
         * lifecycle is fenced by the media session ID in the heartbeat. */
        return;
    }
    if (!room->game_started || room->link_session_id[0] != '\0') {
        return;
    }
    if (state->room_link_session_id[0] != '\0') {
        clear_room_link_session_id(state);
    }
    if (!state->room_game_ended) {
        client_log(state, "room_mark_game_ended", "room=%u", state->room_number);
    }
    state->room_game_ended = true;
    state->room_start_requested = false;
    copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  USE ANOTHER ROOM");
}

static void format_room_phase(const AppState *state,
                              const IntegralApiRoom *room,
                              int ready1,
                              int ready2,
                              char *out,
                              size_t out_size)
{
    if (room_status_is_actionable_error(state->login.status)) {
        copy_text(out, out_size, state->login.status);
    }
    else if (current_room_game_ended(state)) {
        copy_text(out, out_size, "GAME ENDED");
    }
    else if (state->room_client_started) {
        if (state->room_remaining_seconds >= 0) {
            long long minutes = state->room_remaining_seconds / 60;
            long long seconds = state->room_remaining_seconds % 60;
            snprintf(out,
                     out_size,
                     "%s RUNNING %02lld:%02lld%s",
                     room_link_mode_label(state->room_link_mode),
                     minutes,
                     seconds,
                     state->room_remaining_seconds <= 120 ? " 2MIN WARNING" :
                     (state->room_remaining_seconds <= 600 ? " 10MIN WARNING" : ""));
        }
        else {
            snprintf(out, out_size, "%s RUNNING", room_link_mode_label(state->room_link_mode));
        }
    }
    else if (strcmp(state->login.status, "CONNECTING") == 0) {
        copy_text(out, out_size, "CONNECTING");
    }
    else if (strcmp(state->login.status, "SERVER STARTING") == 0) {
        copy_text(out, out_size, "SERVER STARTING");
    }
    else if (state->room_link_session_id[0] != '\0') {
        copy_text(out, out_size, "CONNECTING");
    }
    else if ((room && ready1 && ready2) || (state->room_ready_self && state->room_ready_peer)) {
        copy_text(out, out_size, "BOTH OK");
    }
    else if (state->room_ready_self) {
        copy_text(out, out_size, "WAIT PEER");
    }
    else {
        copy_text(out, out_size, "ROOM");
    }
}

static void draw_room(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color cursor = {230, 92, 76, 255};
    SDL_Color muted = {112, 122, 130, 255};

    char subtitle[32];
    const IntegralApiRoom *header_room = state->room_number >= 1 && state->room_number <= INTEGRAL_API_ROOMS
                                              ? &state->current_room : NULL;
    snprintf(subtitle, sizeof(subtitle), "LINK CODE %s",
             header_room && header_room->room_code[0] ? header_room->room_code : "-----");
    draw_header(renderer, subtitle, state->login.username, state->login.server);

    const IntegralApiRoom *room = NULL;
    if (state->room_number >= 1 && state->room_number <= INTEGRAL_API_ROOMS) {
        room = &state->current_room;
    }
    const char *user1 = room && room->user1[0] ? room->user1 : (state->login.username[0] ? state->login.username : "USER1");
    const char *user2 = room && room->user2[0] ? room->user2 : "";
    int ready1 = room ? room->ready1 : 0;
    int ready2 = room ? room->ready2 : 0;
    char user_line[128];
    snprintf(user_line,
             sizeof(user_line),
             "USER1 : %s%s",
             user1,
             ready1 ? " READY" : "");
    integral_sdl_draw_text_fit(renderer, 48, 104, user_line, 2, value, 380);
    snprintf(user_line,
             sizeof(user_line),
             "USER2 : %s%s",
             user2[0] ? user2 : "<EMPTY>",
             ready2 ? " READY" : "");
    integral_sdl_draw_text_fit(renderer, 48, 140, user_line, 2, value, 380);

    char slot_label[80];
    char slot_detail[220];
    char room_phase[160];
    format_room_slot_label(state, slot_label, sizeof(slot_label));
    format_room_slot_detail(state, slot_detail, sizeof(slot_detail));
    format_room_phase(state, room, ready1, ready2, room_phase, sizeof(room_phase));
    bool game_ended = current_room_game_ended(state);
    const char *labels[] = {
        game_ended ? "SLOT LOCKED" : slot_label,
        game_ended ? "MODE LOCKED" : room_link_mode_label(state->room_link_mode),
        game_ended ? "GAME ENDED" : (state->room_ready_self ? "READY OK" : "READY"),
        "CHAT LOG",
        "CHAT INPUT",
    };
    const char *details[] = {
        game_ended ? "GAME INSTANCE USED" : slot_detail,
        current_room_is_user1(state) ? "LEFT/RIGHT SELECT" : "USER1 SELECTS",
        room_phase,
        "LEFT/RIGHT SCROLL",
        state->room_chat_editing ? "TEXT INPUT ACTIVE" : "ENTER EDIT",
    };
    for (unsigned i = 0; i < 3; i++) {
        int y = 170 + (int)i * 32;
        if (state->room_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 6, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 28};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 2, ">", 2, selected);
        }
        integral_sdl_draw_text_fit(renderer, 58, y, labels[i], 2, state->room_selected == i ? selected : label, 190);
        integral_sdl_draw_text_fit(renderer, 230, y + 6, details[i], 1, i == 0 ? value : muted, 210);
    }

    int log_y = 276;
    if (state->room_selected == 3) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = log_y - 22, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 122};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, log_y - 12, ">", 2, selected);
    }
    integral_sdl_draw_text(renderer, 48, log_y - 16, labels[3], 1, state->room_selected == 3 ? selected : label);
    integral_sdl_draw_text(renderer, 140, log_y - 16, details[3], 1, muted);
    draw_panel(renderer, 22, log_y + 2, INTEGRAL_WINDOW_WIDTH - 44, 100, (SDL_Color){55, 64, 70, 255});
    unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room_chat_log);
    unsigned start = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
    if (state->room_chat_scroll > 0 && message_count > INTEGRAL_CHAT_VISIBLE_LINES) {
        unsigned max_scroll = message_count - INTEGRAL_CHAT_VISIBLE_LINES;
        unsigned offset = state->room_chat_scroll > max_scroll ? max_scroll : state->room_chat_scroll;
        start = message_count - INTEGRAL_CHAT_VISIBLE_LINES - offset;
    }
    unsigned seen = 0;
    unsigned drawn = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES && drawn < INTEGRAL_CHAT_VISIBLE_LINES; i++) {
        if (state->room_chat_log[i][0] == '\0') {
            continue;
        }
        if (seen++ < start) {
            continue;
        }
        integral_sdl_draw_utf8_text(renderer, 34, log_y + 8 + (int)drawn * 18, state->room_chat_log[i], 14, value, INTEGRAL_WINDOW_WIDTH - 68);
        drawn++;
    }
    if (message_count == 0) {
        integral_sdl_draw_text(renderer, 34, log_y + 42, "NO MESSAGES", 1, muted);
    }

    int input_y = 402;
    if (state->room_selected == 4) {
        if (state->room_chat_editing) {
            SDL_SetRenderDrawColor(renderer, 112, 38, 44, 255);
        }
        else {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        }
        SDL_Rect rect = {.x = 14, .y = input_y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 36};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, input_y + 1, ">", 2, selected);
    }
    integral_sdl_draw_text(renderer, 48, input_y, labels[4], 1, state->room_selected == 4 ? selected : label);
    draw_panel(renderer, 142, input_y - 6, INTEGRAL_WINDOW_WIDTH - 164, 30, (SDL_Color){55, 64, 70, 255});
    char chat_display[INTEGRAL_CHAT_MESSAGE_MAX * 2];
    if (state->room_chat_input[0] || state->room_chat_composition[0]) {
        snprintf(chat_display,
                 sizeof(chat_display),
                 "%s%s",
                 state->room_chat_input,
                 state->room_chat_composition);
    }
    else {
        copy_text(chat_display, sizeof(chat_display), "<EMPTY>");
    }
    integral_sdl_draw_utf8_text(renderer, 154, input_y - 1, chat_display, 14, value, INTEGRAL_WINDOW_WIDTH - 202);
    if (state->room_chat_composition[0]) {
        integral_sdl_draw_utf8_text(renderer, 154, input_y + 14, state->room_chat_composition, 12, selected, INTEGRAL_WINDOW_WIDTH - 202);
    }
    if (state->room_chat_editing) {
        SDL_SetRenderDrawColor(renderer, cursor.r, cursor.g, cursor.b, cursor.a);
        SDL_Rect cursor_rect = {.x = INTEGRAL_WINDOW_WIDTH - 42, .y = input_y - 2, .w = 3, .h = 22};
        SDL_RenderFillRect(renderer, &cursor_rect);
    }

    char footer_status[160];
    if (state->room_chat_editing) {
        copy_text(footer_status, sizeof(footer_status), "CHAT INPUT ACTIVE");
    }
    else {
        copy_text(footer_status, sizeof(footer_status), room_phase);
        if (game_ended) {
            copy_text(footer_status, sizeof(footer_status), "GAME ENDED  ESC MAIN MENU");
        }
    }
    integral_sdl_draw_text_fit(renderer, 22, 438, footer_status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer,
                      22,
                      456,
                      game_ended ? "TAB MOVE  ENTER CHAT/SEND  LEFT/RIGHT LOG  ESC MAIN"
                                 : "TAB MOVE  ENTER EDIT/SEND  LEFT/RIGHT SLOT/MODE/LOG  ESC MAIN",
                      1,
                      muted);
    SDL_RenderPresent(renderer);
}

static void format_n64_room_selection_line(char *out,
                                           size_t out_size,
                                           const char *owner,
                                           const char *system,
                                           const char *slot,
                                           const char *filename,
                                           const char *header_title)
{
    if (!slot || !slot[0]) {
        snprintf(out, out_size, "%s %s SLOT : <EMPTY>", owner, system);
        return;
    }
    snprintf(out,
             out_size,
             "%s %s SLOT : %s  %s (%s)",
             owner,
             system,
             slot,
             filename && filename[0] ? path_file_name(filename) : "<FILE UNKNOWN>",
             header_title && header_title[0] ? header_title : "HEADER UNKNOWN");
}

static void draw_n64_room(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    if (state->n64_runtime_media_paired && strcmp(state->n64_runtime_media_role, "remote") == 0) {
        SDL_Rect video_bounds = {.x = 0, .y = 0, .w = INTEGRAL_WINDOW_WIDTH, .h = INTEGRAL_WINDOW_HEIGHT};
        (void)integral_n64_runtime_media_stream_render(state->n64_runtime_media_stream,
                                                renderer,
                                                &video_bounds);
    }

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color warning = {236, 142, 108, 255};
    char subtitle[32];
    const IntegralApiRoom *header_room = state->room_number >= 65 && state->room_number <= 128
                                              ? &state->current_room : NULL;
    snprintf(subtitle, sizeof(subtitle), "N64 CODE %s",
             header_room && header_room->room_code[0] ? header_room->room_code : "-----");
    draw_header(renderer, subtitle, state->login.username, state->login.server);

    const IntegralApiRoom *room = NULL;
    if (state->room_number >= 65 && state->room_number <= 128) {
        room = &state->current_room;
    }
    char user_line[128];
    snprintf(user_line,
             sizeof(user_line),
             "USER1 : %s  HOST",
             room && room->user1[0] ? room->user1 : "PLAYER001");
    integral_sdl_draw_text_fit(renderer, 48, 88, user_line, 2, value, 380);
    snprintf(user_line,
             sizeof(user_line),
             "USER2 : %s  REMOTE",
             room && room->user2[0] ? room->user2 : "<EMPTY>");
    integral_sdl_draw_text_fit(renderer, 48, 116, user_line, 2, value, 380);

    char n64_label[384];
    char user1_gb_label[384];
    char user2_gb_label[384];
    const char *n64_slot = room && room->n64_slot1[0] ? room->n64_slot1 : "";
    const char *n64_filename = room ? room->n64_slot_filename1 : "";
    const char *n64_header_title = room ? room->n64_slot_header_title1 : "";
    const char *user1_gb_slot = room && room->slot1[0] ? room->slot1 : "";
    const char *user1_gb_filename = room ? room->slot_filename1 : "";
    const char *user1_gb_header_title = room ? room->slot_header_title1 : "";
    const char *user2_gb_slot = room && room->slot2[0] ? room->slot2 : "";
    const char *user2_gb_filename = room ? room->slot_filename2 : "";
    const char *user2_gb_header_title = room ? room->slot_header_title2 : "";

    char fallback_n64_slot[16] = "";
    char fallback_user1_slot[16] = "";
    char fallback_user2_slot[16] = "";
    RomHeaderInfo fallback_header;
    int local_user_index = n64_room_local_user_index(state);
    if (!n64_slot[0] && local_user_index == 0 && state->n64_room_n64_slot_index >= 0) {
        snprintf(fallback_n64_slot, sizeof(fallback_n64_slot), "ROM%d", state->n64_room_n64_slot_index + 1);
        n64_slot = fallback_n64_slot;
        n64_filename = state->rom_slots[state->n64_room_n64_slot_index].rom_path;
        if (read_supported_rom_header(n64_filename, &fallback_header) == 0) {
            n64_header_title = fallback_header.header_title;
        }
    }
    if (!user1_gb_slot[0] && local_user_index == 0 && state->n64_room_user1_gb_slot_index >= 0) {
        snprintf(fallback_user1_slot, sizeof(fallback_user1_slot), "ROM%d", state->n64_room_user1_gb_slot_index + 1);
        user1_gb_slot = fallback_user1_slot;
        user1_gb_filename = state->rom_slots[state->n64_room_user1_gb_slot_index].rom_path;
        if (read_supported_rom_header(user1_gb_filename, &fallback_header) == 0) {
            user1_gb_header_title = fallback_header.header_title;
        }
    }
    if (!user2_gb_slot[0] && local_user_index == 1 && state->n64_room_user2_gb_slot_index >= 0) {
        snprintf(fallback_user2_slot, sizeof(fallback_user2_slot), "ROM%d", state->n64_room_user2_gb_slot_index + 1);
        user2_gb_slot = fallback_user2_slot;
        user2_gb_filename = state->rom_slots[state->n64_room_user2_gb_slot_index].rom_path;
        if (read_supported_rom_header(user2_gb_filename, &fallback_header) == 0) {
            user2_gb_header_title = fallback_header.header_title;
        }
    }
    format_n64_room_selection_line(n64_label,
                                   sizeof(n64_label),
                                   "USER1",
                                   "N64",
                                   n64_slot,
                                   n64_filename,
                                   n64_header_title);
    format_n64_room_selection_line(user1_gb_label,
                                   sizeof(user1_gb_label),
                                   "USER1",
                                   "GB",
                                   user1_gb_slot,
                                   user1_gb_filename,
                                   user1_gb_header_title);
    format_n64_room_selection_line(user2_gb_label,
                                   sizeof(user2_gb_label),
                                   "USER2",
                                   "GB",
                                   user2_gb_slot,
                                   user2_gb_filename,
                                   user2_gb_header_title);

    const char *labels[INTEGRAL_N64_RUNTIME_ROOM_ROWS] = {
        n64_label,
        user1_gb_label,
        user2_gb_label,
        state->n64_room_ready ? "READY OK" : "READY",
        "CHAT LOG",
    };
    const char *details[INTEGRAL_N64_RUNTIME_ROOM_ROWS] = {
        "",
        "",
        "",
        state->n64_room_ready ? "NO SAV OVERWRITE" : "ENTER READY",
        "LEFT/RIGHT SCROLL",
    };
    for (unsigned i = 0; i < 4; i++) {
        int y = 150 + (int)i * 32;
        if (state->room_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 7, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 30};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 1, ">", 2, selected);
        }
        if (i < 3) {
            integral_sdl_draw_text_fit(renderer,
                                  48,
                                  y,
                                  labels[i],
                                  1,
                                  state->room_selected == i ? selected : value,
                                  INTEGRAL_WINDOW_WIDTH - 70);
        }
        else {
            integral_sdl_draw_text_fit(renderer, 48, y, labels[i], 1, state->room_selected == i ? selected : label, 190);
            integral_sdl_draw_text_fit(renderer, 240, y, details[i], 1, muted, 210);
        }
    }

    int log_y = 294;
    if (state->room_selected == 4) {
        SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
        SDL_Rect rect = {.x = 14, .y = log_y - 22, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 122};
        SDL_RenderFillRect(renderer, &rect);
        integral_sdl_draw_text(renderer, 24, log_y - 12, ">", 2, selected);
    }
    integral_sdl_draw_text(renderer, 48, log_y - 16, labels[4], 1, state->room_selected == 4 ? selected : label);
    integral_sdl_draw_text(renderer, 140, log_y - 16, details[4], 1, muted);
    draw_panel(renderer, 22, log_y + 2, INTEGRAL_WINDOW_WIDTH - 44, 100, (SDL_Color){55, 64, 70, 255});
    unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room_chat_log);
    unsigned start = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
    if (state->room_chat_scroll > 0 && message_count > INTEGRAL_CHAT_VISIBLE_LINES) {
        unsigned max_scroll = message_count - INTEGRAL_CHAT_VISIBLE_LINES;
        unsigned offset = state->room_chat_scroll > max_scroll ? max_scroll : state->room_chat_scroll;
        start = message_count - INTEGRAL_CHAT_VISIBLE_LINES - offset;
    }
    unsigned seen = 0;
    unsigned drawn = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES && drawn < INTEGRAL_CHAT_VISIBLE_LINES; i++) {
        if (state->room_chat_log[i][0] == '\0') {
            continue;
        }
        if (seen++ < start) {
            continue;
        }
        integral_sdl_draw_utf8_text(renderer, 34, log_y + 8 + (int)drawn * 18, state->room_chat_log[i], 14, value, INTEGRAL_WINDOW_WIDTH - 68);
        drawn++;
    }
    if (message_count == 0) {
        integral_sdl_draw_text(renderer, 34, log_y + 42, "NO MESSAGES", 1, muted);
    }

    bool status_is_warning = strstr(state->login.status, "FAILED") != NULL ||
                             strstr(state->login.status, "REQUIRED") != NULL ||
                             strstr(state->login.status, "INVALID") != NULL ||
                             strstr(state->login.status, "BLOCKED") != NULL ||
                             strstr(state->login.status, "ERROR") != NULL;
    char n64_status[192];
    if (!status_is_warning && state->room_remaining_seconds >= 0 && state->n64_runtime_media_authenticated) {
        snprintf(n64_status,
                 sizeof(n64_status),
                 "%s  REMAIN %02lld:%02lld%s",
                 state->login.status,
                 state->room_remaining_seconds / 60,
                 state->room_remaining_seconds % 60,
                 state->room_remaining_seconds <= 120 ? " 2MIN WARNING" :
                 (state->room_remaining_seconds <= 600 ? " 10MIN WARNING" : ""));
    }
    else {
        copy_text(n64_status, sizeof(n64_status), state->login.status);
    }
    integral_sdl_draw_text_fit(renderer,
                          22,
                          410,
                          n64_status,
                          1,
                          status_is_warning ? warning : muted,
                          INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text_fit(renderer, 22, 438, "TRANSFER PAK  NO SAV OVERWRITE", 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 456, "TAB MOVE  LEFT/RIGHT ROM1-8  ENTER READY  ESC MAIN", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_runtime_exit_confirmation(SDL_Renderer *renderer,
                                           const AppState *state)
{
    if (!state->runtime_exit_confirming) return;
    SDL_Rect panel = {.x = INTEGRAL_WINDOW_WIDTH / 2 - 210,
                      .y = INTEGRAL_WINDOW_HEIGHT / 2 - 70,
                      .w = 420,
                      .h = 140};
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color text = {185, 205, 216, 255};
    SDL_Color selected = {86, 220, 150, 255};
    SDL_SetRenderDrawColor(renderer, 10, 14, 18, 245);
    SDL_RenderFillRect(renderer, &panel);
    SDL_SetRenderDrawColor(renderer, 86, 162, 126, 255);
    SDL_RenderDrawRect(renderer, &panel);
    integral_sdl_draw_text(renderer, panel.x + 36, panel.y + 28,
                           "EXIT GAME?", 3, title);
    integral_sdl_draw_text(renderer, panel.x + 86, panel.y + 84,
                           state->runtime_exit_confirm_yes ? "> YES" : "  YES", 3,
                           state->runtime_exit_confirm_yes ? selected : text);
    integral_sdl_draw_text(renderer, panel.x + 244, panel.y + 84,
                           state->runtime_exit_confirm_yes ? "  NO" : "> NO", 3,
                           state->runtime_exit_confirm_yes ? text : selected);
    SDL_RenderPresent(renderer);
}

static void draw_key_config(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "KEY CONFIG", state->login.username, state->login.server);
    char slot1_line1[192];
    char slot1_line2[192];
    char slot2_line1[192];
    char slot2_line2[192];
    char n64_line1[192];
    char n64_line2[192];
    char n64_line3[192];
    char util_line1[192];
    char util_line2[192];
    char util_line3[192];
    format_slot_key_summary(state->keys.slot1, slot1_line1, sizeof(slot1_line1), slot1_line2, sizeof(slot1_line2));
    format_slot_key_summary(state->keys.slot2, slot2_line1, sizeof(slot2_line1), slot2_line2, sizeof(slot2_line2));
    format_n64_key_summary(state->keys.n64_p1,
                           n64_line1,
                           sizeof(n64_line1),
                           n64_line2,
                           sizeof(n64_line2),
                           n64_line3,
                           sizeof(n64_line3));
    format_util_key_summary(&state->keys,
                            util_line1,
                            sizeof(util_line1),
                            util_line2,
                            sizeof(util_line2),
                            util_line3,
                            sizeof(util_line3));

    const char *labels[] = {"SLOT 1 KEYS", "SLOT 2 KEYS", "N64 KEYS", "UTIL KEYS", "RESET DEFAULTS"};
    const char *details1[] = {slot1_line1, slot2_line1, n64_line1, util_line1, "RESTORE DEFAULTS"};
    const char *details2[] = {slot1_line2, slot2_line2, n64_line2, util_line2, ""};
    const char *details3[] = {"", "", n64_line3, util_line3, ""};

    for (unsigned i = 0; i < INTEGRAL_KEY_ROWS; i++) {
        int y = 86 + (int)i * 62;
        if (state->key_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 56};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y + 6, ">", 2, selected);
        }
        integral_sdl_draw_text(renderer, 48, y, labels[i], 2, state->key_selected == i ? selected : label);
        integral_sdl_draw_text_fit(renderer, 48, y + 22, details1[i], 1, value, 400);
        if (details2[i][0] != '\0') {
            integral_sdl_draw_text_fit(renderer, 48, y + 36, details2[i], 1, value, 400);
        }
        if (details3[i][0] != '\0') {
            integral_sdl_draw_text_fit(renderer, 48, y + 50, details3[i], 1, value, 400);
        }
    }

    if (state->key_capture_target != KEY_CAPTURE_NONE) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 42, .y = 320, .w = 396, .h = 88};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 62, 342, "PRESS KEY FOR", 2, selected);
        integral_sdl_draw_text(renderer,
                          62,
                          372,
                          key_config_step_label(state->key_capture_target, state->key_capture_step),
                          2,
                          value);
        if (state->key_capture_target != KEY_CAPTURE_UTILS) {
            integral_sdl_draw_text(renderer, 62, 394, "KEYBOARD / JOY-CON", 1, muted);
        }
    }

    integral_sdl_draw_text_fit(renderer, 22, 410, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    integral_sdl_draw_text(renderer, 22, 438, "TAB MOVE  ENTER CONFIGURE  USB / BLUETOOTH", 1, muted);
    integral_sdl_draw_text(renderer, 22, 456, "ESC BACK / CANCEL", 1, muted);
    SDL_RenderPresent(renderer);
}

static void format_rom_slot_summary(const IntegralConfigRomSlot *slot,
                                    const IntegralApiRomSlot *server_slot,
                                    char *out,
                                    size_t out_size)
{
    const char *filename = slot->rom_path[0] ? path_file_name(slot->rom_path) : "";
    if (!filename[0] && server_slot && server_slot->filename[0]) {
        filename = server_slot->filename;
    }
    if (slot_has_pending_local_rom(slot)) {
        RomHeaderInfo header;
        if (read_supported_rom_header(slot->rom_path, &header) == 0) {
            snprintf(out,
                     out_size,
                     "%s  %s  LOCAL READY",
                     path_file_name(slot->rom_path),
                     header.header_title);
            return;
        }
        snprintf(out,
                 out_size,
                 "%s  LOCAL UNSUPPORTED",
                 path_file_name(slot->rom_path));
        return;
    }

    bool registered = slot_has_server_registration(slot) ||
                      (server_slot && server_slot->rom_id[0] != '\0' && server_slot->save_id[0] != '\0');
    if (!registered && slot->rom_path[0] == '\0') {
        copy_text(out, out_size, "<EMPTY>");
        return;
    }
    if (registered) {
        const char *local = local_file_exists(slot->rom_path) ? "LOCAL OK" : "LOCAL MISSING";
        if (!server_slot || !server_slot->game_type[0]) {
            snprintf(out,
                     out_size,
                     "%s  SERVER METADATA INVALID  %s",
                     filename[0] ? filename : "SERVER ROM",
                     local);
            return;
        }
        snprintf(out,
                 out_size,
                 "%s  SERVER REGISTERED  %s  %s",
                 filename[0] ? filename : "SERVER ROM",
                 local,
                 server_slot->game_type);
        return;
    }
    RomHeaderInfo header;
    if (read_supported_rom_header(slot->rom_path, &header) == 0) {
        snprintf(out,
                 out_size,
                 "%s  %s  LOCAL READY",
                 path_file_name(slot->rom_path),
                 header.header_title);
        return;
    }
    snprintf(out,
             out_size,
             "%s  UNSUPPORTED",
             path_file_name(slot->rom_path));
}

static void draw_marquee_text_fit(SDL_Renderer *renderer,
                                  int x,
                                  int y,
                                  const char *text,
                                  int scale,
                                  SDL_Color color,
                                  int max_width,
                                  unsigned phase_seed)
{
    if (max_width <= 0 || scale <= 0) {
        return;
    }
    size_t visible_chars = (size_t)(max_width / (6 * scale));
    size_t len = strlen(text);
    if (visible_chars == 0) {
        return;
    }
    if (len <= visible_chars) {
        integral_sdl_draw_text(renderer, x, y, text, scale, color);
        return;
    }

    size_t max_offset = len - visible_chars;
    const Uint32 hold_ms = 1000u;
    const Uint32 step_ms = 120u;
    Uint32 scroll_ms = (Uint32)(max_offset * step_ms);
    Uint32 cycle_ms = hold_ms + scroll_ms + hold_ms;
    Uint32 tick = (SDL_GetTicks() + phase_seed * 37u) % cycle_ms;
    size_t offset = 0;
    if (tick < hold_ms) {
        offset = 0;
    }
    else if (tick < hold_ms + scroll_ms) {
        offset = (size_t)((tick - hold_ms) / step_ms);
        if (offset > max_offset) {
            offset = max_offset;
        }
    }
    else {
        offset = max_offset;
    }
    char window[256];
    if (visible_chars >= sizeof(window)) {
        visible_chars = sizeof(window) - 1;
    }
    for (size_t i = 0; i < visible_chars; i++) {
        window[i] = text[offset + i];
    }
    window[visible_chars] = '\0';
    integral_sdl_draw_text(renderer, x, y, window, scale, color);
}

static const char *path_file_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;
    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

static bool rom_extension_matches(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return strcmp(dot, ".gb") == 0 || strcmp(dot, ".gbc") == 0 || strcmp(dot, ".GB") == 0 ||
           strcmp(dot, ".GBC") == 0 || strcmp(dot, ".z64") == 0 || strcmp(dot, ".Z64") == 0 ||
           strcmp(dot, ".n64") == 0 || strcmp(dot, ".N64") == 0 || strcmp(dot, ".v64") == 0 ||
           strcmp(dot, ".V64") == 0;
}

/* Generic ROM metadata parsing lives in rom_metadata.c. */
static int read_supported_rom_header(const char *path, RomHeaderInfo *info)
{
    return integral_rom_metadata_read(path, info);
}

static bool local_file_exists(const char *path)
{
    return path && path[0] && access(path, R_OK) == 0;
}

static bool local_regular_file(const char *path)
{
    struct stat info;
    return path && path[0] && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool slot_has_server_registration(const IntegralConfigRomSlot *slot)
{
    return slot->rom_id[0] != '\0' && slot->save_id[0] != '\0';
}

static bool slot_has_pending_local_rom(const IntegralConfigRomSlot *slot)
{
    return slot->rom_path[0] != '\0' && !slot_has_server_registration(slot) && !slot_is_supported_n64(slot);
}

static bool discard_pending_rom_slots(AppState *state)
{
    bool changed = false;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (slot_has_pending_local_rom(&state->rom_slots[i])) {
            memset(&state->rom_slots[i], 0, sizeof(state->rom_slots[i]));
            changed = true;
        }
    }
    return changed;
}

static void merge_server_rom_slots(AppState *state, const IntegralApiRomSlot *server_slots)
{
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        const IntegralApiRomSlot *server_slot = &server_slots[i];
        IntegralConfigRomSlot *slot = &state->rom_slots[i];
        if (server_slot->rom_id[0] == '\0' || server_slot->save_id[0] == '\0') {
            if (!slot_is_supported_n64(slot)) {
                memset(slot, 0, sizeof(*slot));
            }
            continue;
        }
        copy_text(slot->rom_id, sizeof(slot->rom_id), server_slot->rom_id);
        copy_text(slot->save_id, sizeof(slot->save_id), server_slot->save_id);
        if (server_slot->filename[0] != '\0') {
            snprintf(slot->rom_path, sizeof(slot->rom_path), INTEGRAL_ROM_FOLDER "/%s", server_slot->filename);
        }
    }
}

static bool refresh_rom_slots_from_server(AppState *state)
{
    if (state->login.token[0] == '\0') {
        return false;
    }
    IntegralApiRomSlot server_slots[INTEGRAL_ROM_SLOTS];
    char error[160];
    if (integral_api_get_rom_slots(state->login.server,
                              state->login.token,
                              server_slots,
                              INTEGRAL_ROM_SLOTS,
                              error,
                              sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "ROM SLOT SYNC FAILED %s", error);
        client_log(state, "rom_slots_sync_failed", "error=%s", error);
        return false;
    }
    memcpy(state->server_rom_slots, server_slots, sizeof(state->server_rom_slots));
    merge_server_rom_slots(state, server_slots);
    if (integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "ROM SLOT SYNC SAVE FAILED");
        return false;
    }
    client_log(state, "rom_slots_synced", "server=%s", login_server_id_or_default(&state->login));
    return true;
}

static void enter_rom_register(AppState *state)
{
    (void)discard_pending_rom_slots(state);
    (void)refresh_rom_slots_from_server(state);
    state->screen = SCREEN_ROM_REGISTER;
    state->rom_selected = 0;
    state->rom_edit_target = ROM_EDIT_NONE;
    state->rom_browser_active = false;
    state->rom_confirm_delete = false;
    state->rom_confirm_initial_save_import = false;
    state->rom_initial_save_import_slot = -1;
    state->rom_initial_save_import_path[0] = '\0';
    copy_text(state->login.status, sizeof(state->login.status), "ROM REGISTER");
}

static void leave_rom_register(AppState *state)
{
    bool changed = discard_pending_rom_slots(state);
    merge_server_rom_slots(state, state->server_rom_slots);
    if (changed) {
        (void)integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS);
    }
    state->rom_edit_target = ROM_EDIT_NONE;
    state->rom_browser_active = false;
    state->rom_confirm_delete = false;
    state->rom_confirm_initial_save_import = false;
    state->rom_initial_save_import_slot = -1;
    state->rom_initial_save_import_path[0] = '\0';
    state->screen = SCREEN_MAIN_MENU;
    copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
}

static int scan_rom_folder(AppState *state)
{
    state->rom_browser_count = 0;
    state->rom_browser_selected = 0;
    DIR *dir = opendir(INTEGRAL_ROM_FOLDER);
    if (!dir) {
        copy_text(state->login.status, sizeof(state->login.status), "PUT ROMS IN ./roms THEN PRESS F4");
        return -1;
    }
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && state->rom_browser_count < INTEGRAL_ROM_BROWSER_MAX) {
        if (!rom_extension_matches(entry->d_name)) {
            continue;
        }
        snprintf(state->rom_browser_entries[state->rom_browser_count],
                 sizeof(state->rom_browser_entries[state->rom_browser_count]),
                 INTEGRAL_ROM_FOLDER "/%s",
                 entry->d_name);
        state->rom_browser_count++;
    }
    closedir(dir);
    if (state->rom_browser_count == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "NO ROM FILES IN ./roms");
        return -1;
    }
    state->rom_browser_active = true;
    copy_text(state->login.status, sizeof(state->login.status), "SELECT ROM FROM FOLDER");
    return 0;
}

static void apply_rom_path_to_slot(AppState *state, const char *path)
{
    if (state->rom_selected >= INTEGRAL_ROM_SLOTS || state->rom_browser_count == 0) {
        return;
    }
    IntegralConfigRomSlot *slot = &state->rom_slots[state->rom_selected];
    copy_text(slot->rom_path, sizeof(slot->rom_path), path);
    if (state->rom_initial_save_import_slot == (int)state->rom_selected) {
        state->rom_initial_save_import_slot = -1;
        state->rom_initial_save_import_path[0] = '\0';
    }
    slot->rom_id[0] = '\0';
    slot->save_id[0] = '\0';
    snprintf(state->login.status, sizeof(state->login.status), "ROM%u LOCAL READY", state->rom_selected + 1);
}

static void apply_rom_path_to_slot_index(AppState *state, unsigned slot_index, const char *path)
{
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        return;
    }
    IntegralConfigRomSlot *slot = &state->rom_slots[slot_index];
    copy_text(slot->rom_path, sizeof(slot->rom_path), path);
    if (state->rom_initial_save_import_slot == (int)slot_index) {
        state->rom_initial_save_import_slot = -1;
        state->rom_initial_save_import_path[0] = '\0';
    }
    slot->rom_id[0] = '\0';
    slot->save_id[0] = '\0';
    snprintf(state->login.status, sizeof(state->login.status), "ROM%u LOCAL READY", slot_index + 1);
}

static void apply_browser_rom(AppState *state)
{
    if (state->rom_browser_count == 0) {
        return;
    }
    const char *path = state->rom_browser_entries[state->rom_browser_selected];
    state->rom_browser_active = false;
    apply_rom_path_to_slot(state, path);
}

static void cycle_rom_for_selected_slot(AppState *state, int delta)
{
    unsigned slot_index = state->rom_selected;
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        return;
    }
    bool was_active = state->rom_browser_active;
    if (scan_rom_folder(state) != 0 || state->rom_browser_count == 0) {
        return;
    }
    state->rom_browser_active = was_active;

    int current = -1;
    const char *slot_path = state->rom_slots[slot_index].rom_path;
    for (unsigned i = 0; i < state->rom_browser_count; i++) {
        if (strcmp(slot_path, state->rom_browser_entries[i]) == 0) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)state->rom_browser_count - 1 : 0;
    }
    if (next < 0) {
        next = (int)state->rom_browser_count - 1;
    }
    if (next >= (int)state->rom_browser_count) {
        next = 0;
    }
    apply_rom_path_to_slot_index(state, slot_index, state->rom_browser_entries[next]);
}

static void cycle_local_rom_slot(AppState *state, unsigned slot_index, int delta)
{
    if (slot_index >= 2) {
        return;
    }
    int candidates[INTEGRAL_ROM_SLOTS];
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (slot_is_supported_gb(&state->rom_slots[i]) &&
            state->local_slot_indices[slot_index == 0 ? 1 : 0] != (int)i) {
            candidates[count++] = (int)i;
        }
    }
    if (count == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "NO ROM1-8 SET");
        return;
    }

    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if (state->local_slot_indices[slot_index] == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)count - 1 : 0;
    }
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    state->local_slot_indices[slot_index] = candidates[next];
    IntegralConfigLocal local = {
        .slot1_index = state->local_slot_indices[0],
        .slot2_index = state->local_slot_indices[1],
    };
    if (integral_config_save_local(state->config_path, &local) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "LOCAL CONFIG SAVE FAILED");
        return;
    }
    snprintf(state->login.status,
             sizeof(state->login.status),
             "SLOT%u SELECTED ROM%d",
             slot_index + 1,
             state->local_slot_indices[slot_index] + 1);
}

static void cycle_n64_runtime_n64_slot(AppState *state, int delta)
{
    int candidates[INTEGRAL_ROM_SLOTS];
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (slot_is_supported_n64(&state->rom_slots[i])) {
            candidates[count++] = (int)i;
        }
    }
    if (count == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "NO N64 ROM SET");
        return;
    }
    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if (state->integral_n64_runtime_n64_slot_index == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)count - 1 : 0;
    }
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    state->integral_n64_runtime_n64_slot_index = candidates[next];
    snprintf(state->login.status,
             sizeof(state->login.status),
             "N64 SELECTED ROM%d",
             state->integral_n64_runtime_n64_slot_index + 1);
}

static void cycle_n64_runtime_transfer_slot(AppState *state, unsigned transfer_slot, int delta)
{
    if (transfer_slot >= 4) {
        return;
    }
    int candidates[INTEGRAL_ROM_SLOTS + 1];
    unsigned count = 0;
    candidates[count++] = -1;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        bool already_selected = false;
        for (unsigned j = 0; j < 4; j++) {
            if (j != transfer_slot && state->integral_n64_runtime_transfer_slot_indices[j] == (int)i) {
                already_selected = true;
                break;
            }
        }
        if (!already_selected && registered_rom_slot_at(state, (int)i) && slot_is_supported_gb(&state->rom_slots[i])) {
            candidates[count++] = (int)i;
        }
    }
    if (count <= 1) {
        copy_text(state->login.status, sizeof(state->login.status), "NO GB/GBC ROM SET");
        return;
    }
    int current = 0;
    for (unsigned i = 0; i < count; i++) {
        if (state->integral_n64_runtime_transfer_slot_indices[transfer_slot] == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    state->integral_n64_runtime_transfer_slot_indices[transfer_slot] = candidates[next];
    if (candidates[next] < 0) {
        snprintf(state->login.status, sizeof(state->login.status), "SLOT%u CLEARED", transfer_slot + 1);
    }
    else {
        snprintf(state->login.status,
                 sizeof(state->login.status),
                 "SLOT%u SELECTED ROM%d",
                 transfer_slot + 1,
                 candidates[next] + 1);
    }
}

static bool n64_room_gb_game_type(const AppState *state, int index, char *out, size_t out_size)
{
    const IntegralConfigRomSlot *slot = registered_rom_slot_at(state, index);
    const IntegralApiRomSlot *server_slot =
        index >= 0 && index < INTEGRAL_ROM_SLOTS ? &state->server_rom_slots[index] : NULL;
    RomHeaderInfo header;
    if (!slot || !server_slot || !server_slot->game_type[0] ||
        strcmp(server_slot->platform, "gb") != 0 ||
        !server_slot->rom_header_title[0] ||
        read_supported_rom_header(slot->rom_path, &header) != 0 ||
        strcmp(header.platform, server_slot->platform) != 0 ||
        strcmp(header.header_title, server_slot->rom_header_title) != 0) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return false;
    }
    copy_text(out, out_size, server_slot->game_type);
    return true;
}

static const IntegralConfigRomSlot *find_n64_room_host_slot2_rom(const AppState *state, int *out_index)
{
    if (out_index) *out_index = -1;
    if (!state || state->room_number < 65 || state->room_number > 128) return NULL;
    const char *remote_game_type = state->current_room.slot_game_type2;
    const char *remote_header = state->current_room.slot_header_title2;
    if (!remote_game_type[0] || !remote_header[0]) return NULL;
    for (int index = 0; index < INTEGRAL_ROM_SLOTS; index++) {
        char local_game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
        const IntegralConfigRomSlot *slot = registered_rom_slot_at(state, index);
        bool local_metadata_valid =
            slot && n64_room_gb_game_type(state, index, local_game_type, sizeof(local_game_type));
        bool game_type_matches =
            local_metadata_valid && strcmp(local_game_type, remote_game_type) == 0;
        bool header_matches =
            local_metadata_valid &&
            strcmp(state->server_rom_slots[index].rom_header_title, remote_header) == 0;
        if (slot && game_type_matches && header_matches) {
            if (out_index) *out_index = index;
            return slot;
        }
    }
    return NULL;
}

static void init_n64_room_selection(AppState *state)
{
    state->n64_room_n64_slot_index = -1;
    state->n64_room_user1_gb_slot_index = -1;
    state->n64_room_user2_gb_slot_index = -1;
    for (int i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (state->n64_room_n64_slot_index < 0 && registered_rom_slot_at(state, i) &&
            slot_is_supported_n64(&state->rom_slots[i])) {
            state->n64_room_n64_slot_index = i;
        }
        if (state->n64_room_user1_gb_slot_index < 0 && registered_rom_slot_at(state, i) &&
            slot_is_supported_gb(&state->rom_slots[i])) {
            state->n64_room_user1_gb_slot_index = i;
            state->n64_room_user2_gb_slot_index = i;
        }
    }
}

static int n64_room_local_user_index(const AppState *state)
{
    if (state->room_number < 65 || state->room_number > 128 || state->login.username[0] == '\0') {
        return -1;
    }
    const IntegralApiRoom *room = &state->current_room;
    if (room->user1[0] && strcasecmp(state->login.username, room->user1) == 0) {
        return 0;
    }
    if (room->user2[0] && strcasecmp(state->login.username, room->user2) == 0) {
        return 1;
    }
    return -1;
}

static void set_n64_room_ready_error(AppState *state, const char *error)
{
    snprintf(state->login.status,
             sizeof(state->login.status),
             "READY FAILED: %s",
             error && error[0] ? error : "ROOM SYNC ERROR");
}

static bool sync_n64_room_state(AppState *state, bool ready)
{
    int user_index = n64_room_local_user_index(state);
    if (user_index < 0) {
        refresh_room_quiet(state);
        user_index = n64_room_local_user_index(state);
    }
    int gb_index = user_index == 0 ? state->n64_room_user1_gb_slot_index
                                  : state->n64_room_user2_gb_slot_index;
    if (user_index >= 0 &&
        (!registered_rom_slot_at(state, gb_index) ||
         !slot_is_supported_gb(registered_rom_slot_at(state, gb_index)))) {
        init_n64_room_selection(state);
        gb_index = user_index == 0 ? state->n64_room_user1_gb_slot_index
                                  : state->n64_room_user2_gb_slot_index;
    }
    if (user_index < 0 || gb_index < 0 || (user_index == 0 && state->n64_room_n64_slot_index < 0)) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 ROOM ROM SELECTION REQUIRED");
        client_log(state,
                   "n64_room_sync_blocked",
                   "ready=%d user_index=%d gb_index=%d n64_index=%d",
                   ready ? 1 : 0,
                   user_index,
                   gb_index,
                   state->n64_room_n64_slot_index);
        return false;
    }
    char slot[16];
    char n64_slot[16] = "";
    snprintf(slot, sizeof(slot), "ROM%d", gb_index + 1);
    if (user_index == 0) {
        snprintf(n64_slot, sizeof(n64_slot), "ROM%d", state->n64_room_n64_slot_index + 1);
    }
    char error[160];
    if (integral_api_update_n64_room_state(state->login.server,
                                            state->login.token,
                                            state->room_number,
                                            slot,
                                            n64_slot,
                                            ready ? 1 : 0,
                                            error,
                                            sizeof(error)) != 0) {
        set_n64_room_ready_error(state, error);
        client_log(state,
                   "n64_room_sync_failed",
                   "ready=%d user_index=%d slot=%s n64_slot=%s error=%s",
                   ready ? 1 : 0,
                   user_index,
                   slot,
                   n64_slot[0] ? n64_slot : "-",
                   error);
        return false;
    }
    refresh_room_quiet(state);
    copy_text(state->login.status,
              sizeof(state->login.status),
              ready ? "N64 ROOM READY" : "N64 ROOM SELECTION UPDATED");
    client_log(state,
               "n64_room_sync_ok",
               "ready=%d user_index=%d slot=%s n64_slot=%s ready_self=%d ready_peer=%d",
               ready ? 1 : 0,
               user_index,
               slot,
               n64_slot[0] ? n64_slot : "-",
               state->room_ready_self ? 1 : 0,
               state->room_ready_peer ? 1 : 0);
    return true;
}

static bool resolve_n64_room_local_outbox(AppState *state)
{
    int user_index = n64_room_local_user_index(state);
    int gb_index = user_index == 0 ? state->n64_room_user1_gb_slot_index
                                  : state->n64_room_user2_gb_slot_index;
    const IntegralConfigRomSlot *gb_slot = registered_rom_slot_at(state, gb_index);
    if (user_index < 0 || !gb_slot || !resolve_save_upload_outbox(state, gb_slot->save_id)) {
        return false;
    }
    if (user_index == 0) {
        const IntegralConfigRomSlot *n64_slot = registered_rom_slot_at(state, state->n64_room_n64_slot_index);
        if (!n64_slot || !resolve_save_upload_outbox(state, n64_slot->save_id)) return false;
    }
    return true;
}

static void clear_secret(char *value, size_t value_size)
{
    volatile unsigned char *cursor = (volatile unsigned char *)value;
    while (value_size-- > 0) {
        *cursor++ = 0;
    }
}

static const char *n64_runtime_media_ca_file(void)
{
    const char *configured = getenv("INTEGRAL_EMULATOR_N64_RUNTIME_MEDIA_RELAY_CA_FILE");
    return configured && configured[0] ? configured : NULL;
}

static void n64_write_be32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24u);
    data[1] = (unsigned char)(value >> 16u);
    data[2] = (unsigned char)(value >> 8u);
    data[3] = (unsigned char)value;
}

static void n64_write_be64(unsigned char *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) {
        data[index] = (unsigned char)value;
        value >>= 8u;
    }
}

static uint64_t n64_wall_clock_ms(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

static bool runtime_session_id_is_path_safe(const char *session_id)
{
    if (!session_id || !session_id[0]) return false;
    size_t length = strlen(session_id);
    if (length > 95u) return false;
    for (const unsigned char *cursor = (const unsigned char *)session_id; *cursor; cursor++) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') return false;
    }
    return true;
}

static bool format_runtime_session_path(char *out,
                                        size_t out_size,
                                        const char *root,
                                        const char *session_id,
                                        const char *relative_path)
{
    if (!out || out_size == 0 || !root ||
        !runtime_session_id_is_path_safe(session_id)) {
        return false;
    }
    int length;
    if (relative_path && relative_path[0] != '\0') {
        length = snprintf(out, out_size, "%s/%s/%s", root, session_id, relative_path);
    }
    else {
        length = snprintf(out, out_size, "%s/%s", root, session_id);
    }
    return length > 0 && (size_t)length < out_size;
}

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

static bool managed_runtime_file_path(const char *path)
{
    static const char prefix[] = "runtime/";
    if (!path || strncmp(path, prefix, sizeof(prefix) - 1u) != 0) return false;
    const char *component = path + sizeof(prefix) - 1u;
    while (*component) {
        const char *end = strchr(component, '/');
        size_t length = end ? (size_t)(end - component) : strlen(component);
        if (length == 0 ||
            (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (!end) return true;
        component = end + 1;
    }
    return false;
}

static bool prepare_n64_remote_input_path(AppState *state)
{
    if (!runtime_session_id_is_path_safe(state->n64_runtime_media_session_id) ||
        ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_N64_RUNTIME_MEDIA_DIR) != 0) {
        return false;
    }
    char session_dir[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(session_dir,
                                     sizeof(session_dir),
                                     INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id,
                                     NULL)) {
        return false;
    }
    if (ensure_private_runtime_directory(session_dir) != 0) return false;
    return format_runtime_session_path(state->n64_runtime_media_remote_input_path,
                                       sizeof(state->n64_runtime_media_remote_input_path),
                                       INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                       state->n64_runtime_media_session_id,
                                       "controller2.bin");
}

static bool write_n64_remote_input_state(AppState *state,
                                         uint32_t sequence,
                                         uint64_t buttons)
{
    unsigned char record[INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE] = {0};
    uint64_t written_ms = n64_wall_clock_ms();
    if (!state->n64_runtime_media_remote_input_path[0] || sequence == 0 ||
        written_ms == 0 || (buttons & ~UINT64_C(0x3ffff)) != 0) {
        return false;
    }
    memcpy(record, "S64C", 4);
    record[4] = 1;
    n64_write_be32(record + 8, sequence);
    n64_write_be64(record + 12, written_ms);
    n64_write_be64(record + 20, buttons);
    return atomic_replace_binary_file(state->n64_runtime_media_remote_input_path,
                                      record,
                                      sizeof(record)) == 0;
}

typedef struct N64RuntimeStopResult {
    bool request_created;
    bool graceful;
    bool forced;
    bool stopped;
} N64RuntimeStopResult;

static N64RuntimeStopResult stop_n64_runtime_media_host_process(AppState *state)
{
    N64RuntimeStopResult result = {false, false, false, false};
    if (!state || state->n64_runtime_media_host_pid == 0) return result;
    IntegralChildProcess pid = state->n64_runtime_media_host_pid;
    static const unsigned char stop_record[] = "S64STOP1\n";
    if (state->n64_runtime_stop_request_path[0] != '\0') {
        if (atomic_replace_binary_file(state->n64_runtime_stop_request_path,
                                       stop_record,
                                       sizeof(stop_record) - 1u) == 0) {
            result.request_created = true;
            client_log(state,
                       "n64_room_host_stop_requested",
                       "session=%s pid=%ld timeout_ms=%u",
                       state->n64_runtime_media_launched_session_id,
                       (long)pid,
                       INTEGRAL_N64_RUNTIME_STOP_WAIT_MS);
        }
        else {
            client_log(state,
                       "n64_room_host_stop_request_failed",
                       "session=%s pid=%ld errno=%d",
                       state->n64_runtime_media_launched_session_id,
                       (long)pid,
                       errno);
        }
    }
#ifdef _WIN32
    HANDLE process = (HANDLE)(intptr_t)pid;
    if (process && process != INVALID_HANDLE_VALUE) {
        DWORD wait_result = WaitForSingleObject(process,
                                                INTEGRAL_N64_RUNTIME_STOP_WAIT_MS);
        result.graceful = wait_result == WAIT_OBJECT_0;
        result.stopped = result.graceful;
        if (!result.graceful) {
            if (TerminateProcess(process, 1) != 0) {
                result.forced = true;
                result.stopped = WaitForSingleObject(process, 2000u) == WAIT_OBJECT_0;
            }
        }
        if (result.stopped) CloseHandle(process);
    }
#else
    for (unsigned attempt = 0;
         attempt < INTEGRAL_N64_RUNTIME_STOP_WAIT_MS / 100u;
         attempt++) {
        int status = 0;
        IntegralChildProcess waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) {
            result.graceful = true;
            result.stopped = true;
            break;
        }
        usleep(100000);
    }
    if (!result.graceful && kill(pid, 0) == 0) {
        if (kill(pid, SIGKILL) == 0 && waitpid(pid, NULL, 0) == pid) {
            result.forced = true;
            result.stopped = true;
        }
    }
#endif
    client_log(state,
               "n64_room_host_stopped",
               "session=%s pid=%ld graceful=%d forced=%d stopped=%d",
               state->n64_runtime_media_launched_session_id,
               (long)pid,
               result.graceful ? 1 : 0,
               result.forced ? 1 : 0,
               result.stopped ? 1 : 0);
    if (!result.stopped) {
        return result;
    }
    if (state->n64_runtime_stop_request_path[0] != '\0') {
        (void)remove(state->n64_runtime_stop_request_path);
    }
    state->n64_runtime_media_host_pid = 0;
    state->n64_runtime_media_launched_session_id[0] = '\0';
    state->n64_runtime_stop_request_path[0] = '\0';
    return result;
}

#ifdef _WIN32
typedef struct N64RuntimeStopTestObservation {
    N64RuntimeStopResult stop;
    ULONGLONG elapsed_ms;
    bool process_running;
    bool handle_open;
    bool stop_file_exists;
    bool state_cleared;
} N64RuntimeStopTestObservation;

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
    copy_text(state->login.username,
              sizeof(state->login.username),
              "N64_STOP_TEST");
    copy_text(state->n64_runtime_media_launched_session_id,
              sizeof(state->n64_runtime_media_launched_session_id),
              behavior);
    copy_text(state->n64_runtime_stop_request_path,
              sizeof(state->n64_runtime_stop_request_path),
              stop_path);
    state->n64_runtime_media_host_pid =
        (IntegralChildProcess)(intptr_t)process_info.hProcess;

    ULONGLONG started_ms = GetTickCount64();
    memset(observation, 0, sizeof(*observation));
    observation->stop = stop_n64_runtime_media_host_process(state);
    observation->elapsed_ms = GetTickCount64() - started_ms;
    observation->state_cleared = state->n64_runtime_media_host_pid == 0 &&
                                 state->n64_runtime_media_launched_session_id[0] == '\0' &&
                                 state->n64_runtime_stop_request_path[0] == '\0';
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

static void cleanup_n64_room_session_files(const char *session_id)
{
    if (!runtime_session_id_is_path_safe(session_id)) {
        return;
    }
    static const char *relative_paths[] = {
        "transfer/slot1.gbc",
        "transfer/slot2.gbc",
        "transfer/slot1.sav",
        "transfer/slot1.sav.rtc",
        "transfer/slot2.sav",
        "transfer/slot2.sav.rtc",
        "n64-save/n64.sav",
        "controller2.bin",
        "remote-media.ipc",
        "stop.request",
    };
    char path[INTEGRAL_CONFIG_PATH_MAX];
    for (size_t i = 0; i < sizeof(relative_paths) / sizeof(relative_paths[0]); ++i) {
        if (format_runtime_session_path(path,
                                        sizeof(path),
                                        INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                        session_id,
                                        relative_paths[i])) {
            (void)remove(path);
        }
    }
    static const char *relative_directories[] = {
        "transfer",
        "n64-save",
        "config",
        "screenshots",
    };
    for (size_t i = 0; i < sizeof(relative_directories) / sizeof(relative_directories[0]); ++i) {
        if (format_runtime_session_path(path,
                                        sizeof(path),
                                        INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                        session_id,
                                        relative_directories[i])) {
            (void)rmdir(path);
        }
    }
    if (format_runtime_session_path(path,
                                    sizeof(path),
                                    INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                    session_id,
                                    NULL)) {
        (void)rmdir(path);
    }
}

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

static void reset_n64_runtime_media_connection(AppState *state)
{
    if (state->n64_runtime_media_connection && state->n64_runtime_media_paired &&
        strcmp(state->n64_runtime_media_role, "remote") == 0) {
        char ignored[32];
        (void)integral_media_relay_send_controller(state->n64_runtime_media_connection,
                                              ++state->n64_runtime_media_input_sequence,
                                              0,
                                              ignored,
                                              sizeof(ignored));
    }
    if (state->n64_runtime_media_remote_input_path[0]) {
        (void)write_n64_remote_input_state(state,
                                           state->n64_runtime_media_input_sequence + 1u,
                                           0);
    }
    if (state->n64_runtime_media_connection) {
        integral_media_relay_close(state->n64_runtime_media_connection);
        state->n64_runtime_media_connection = NULL;
    }
    integral_n64_runtime_media_stream_reset(state->n64_runtime_media_stream);
    bool host_was_running = state->n64_runtime_media_host_pid != 0;
    N64RuntimeStopResult stop_result = stop_n64_runtime_media_host_process(state);
    if (host_was_running && !stop_result.stopped) {
        client_log(state,
                   "n64_room_cleanup_deferred",
                   "session=%s reason=process_not_stopped",
                   state->n64_runtime_media_session_id);
        return;
    }
    cleanup_n64_room_session_files(state->n64_runtime_media_session_id);
    clear_secret(state->n64_runtime_media_ticket, sizeof(state->n64_runtime_media_ticket));
    state->n64_runtime_media_session_id[0] = '\0';
    state->n64_runtime_media_relay_host[0] = '\0';
    state->n64_runtime_media_relay_port = 0;
    state->n64_runtime_media_role[0] = '\0';
    state->n64_runtime_media_scope[0] = '\0';
    state->n64_runtime_media_authenticated = false;
    state->n64_runtime_media_paired = false;
    state->n64_runtime_media_retry_after_ticks = 0;
    state->n64_runtime_media_remote_buttons = 0;
    state->n64_runtime_media_remote_input_path[0] = '\0';
    state->n64_runtime_media_ipc_path[0] = '\0';
    state->n64_runtime_stop_request_path[0] = '\0';
    state->n64_runtime_media_last_sent_buttons = 0;
    state->n64_runtime_media_input_sequence = 0;
    state->n64_runtime_media_last_input_send_ticks = 0;
    state->n64_runtime_media_last_input_receive_ticks = 0;
    state->n64_runtime_media_input_ipc_failure_since_ticks = 0;
    state->n64_runtime_media_input_ipc_failures = 0;
    state->n64_runtime_media_input_interval_total_ms = 0;
    state->n64_runtime_media_input_interval_samples = 0;
    state->n64_runtime_media_input_interval_max_ms = 0;
    state->n64_runtime_media_host_launch_retry_after_ticks = 0;
}

static bool request_n64_runtime_media_session(AppState *state)
{
    if (state->n64_runtime_media_connection) {
        return true;
    }
    char error[160];
    if (integral_api_start_n64_room(state->login.server,
                                     state->login.token,
                                     state->room_number,
                                     state->n64_runtime_media_session_id,
                                     sizeof(state->n64_runtime_media_session_id),
                                     state->n64_runtime_media_relay_host,
                                     sizeof(state->n64_runtime_media_relay_host),
                                     &state->n64_runtime_media_relay_port,
                                     state->n64_runtime_media_role,
                                     sizeof(state->n64_runtime_media_role),
                                     state->n64_runtime_media_scope,
                                     sizeof(state->n64_runtime_media_scope),
                                     state->n64_runtime_media_ticket,
                                     sizeof(state->n64_runtime_media_ticket),
                                     error,
                                     sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "N64 MEDIA AUTH FAILED %s", error);
        state->n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (!runtime_session_id_is_path_safe(state->n64_runtime_media_session_id)) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 MEDIA SESSION ID INVALID");
        clear_secret(state->n64_runtime_media_ticket, sizeof(state->n64_runtime_media_ticket));
        state->n64_runtime_media_session_id[0] = '\0';
        state->n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (strcmp(state->n64_runtime_media_scope, "n64_runtime_media") != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 MEDIA SCOPE INVALID");
        clear_secret(state->n64_runtime_media_ticket, sizeof(state->n64_runtime_media_ticket));
        state->n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (strcmp(state->n64_runtime_media_role, "host") == 0 &&
        (!prepare_n64_remote_input_path(state) ||
         !write_n64_remote_input_state(state, 1u, 0))) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 INPUT IPC CREATE FAILED");
        clear_secret(state->n64_runtime_media_ticket, sizeof(state->n64_runtime_media_ticket));
        state->n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        return false;
    }
    if (integral_media_relay_connect(state->n64_runtime_media_relay_host,
                                state->n64_runtime_media_relay_port,
                                state->n64_runtime_media_session_id,
                                state->n64_runtime_media_role,
                                state->n64_runtime_media_scope,
                                state->n64_runtime_media_ticket,
                                n64_runtime_media_ca_file(),
                                &state->n64_runtime_media_connection,
                                error,
                                sizeof(error)) != 0) {
        if (state->n64_runtime_media_remote_input_path[0]) {
            (void)write_n64_remote_input_state(state, 1u, 0);
            state->n64_runtime_media_remote_input_path[0] = '\0';
        }
        clear_secret(state->n64_runtime_media_ticket, sizeof(state->n64_runtime_media_ticket));
        state->n64_runtime_media_session_id[0] = '\0';
        state->n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 5000u;
        snprintf(state->login.status, sizeof(state->login.status), "N64 MEDIA TLS FAILED %s", error);
        return false;
    }
    clear_secret(state->n64_runtime_media_ticket, sizeof(state->n64_runtime_media_ticket));
    state->n64_runtime_media_authenticated = true;
    state->n64_runtime_media_paired = false;
    state->n64_runtime_media_retry_after_ticks = 0;
    client_log(state,
               "n64_runtime_media_authenticated",
               "session=%s role=%s",
               state->n64_runtime_media_session_id,
               state->n64_runtime_media_role);
    snprintf(state->login.status,
             sizeof(state->login.status),
             "MEDIA TLS AUTH %s  WAITING PEER  NO SAV OVERWRITE",
             state->n64_runtime_media_role);
    return true;
}

static uint32_t media_rate_x100(uint32_t frames, uint32_t window_ms)
{
    if (!window_ms) return 0;
    uint64_t rate = (uint64_t)frames * 100000u / window_ms;
    return rate > UINT32_MAX ? UINT32_MAX : (uint32_t)rate;
}

static void log_n64_runtime_media_metrics(AppState *state, Uint32 now)
{
    IntegralN64RuntimeMediaMetrics metrics;
    if (!integral_n64_runtime_media_stream_take_metrics(state->n64_runtime_media_stream,
                                                  (uint64_t)now * 1000u,
                                                  &metrics)) return;
    if (strcmp(state->n64_runtime_media_role, "host") == 0) {
        uint32_t capture_rate = media_rate_x100(metrics.capture_frames, metrics.window_ms);
        uint32_t encode_rate = media_rate_x100(metrics.encoded_frames, metrics.window_ms);
        uint32_t core_callback_rate = media_rate_x100(
            metrics.source_core_callbacks, metrics.window_ms);
        uint32_t source_due_rate = media_rate_x100(
            metrics.source_capture_due, metrics.window_ms);
        uint32_t source_success_rate = media_rate_x100(
            metrics.source_capture_success, metrics.window_ms);
        uint32_t audio_source_rate = media_rate_x100(
            metrics.audio_source_frames, metrics.window_ms);
        uint32_t audio_sent_rate = media_rate_x100(
            metrics.audio_sent_frames, metrics.window_ms);
        uint32_t input_age_ms = state->n64_runtime_media_last_input_receive_ticks
                                    ? now - state->n64_runtime_media_last_input_receive_ticks
                                    : 0;
        if (metrics.capture_frames > 0u) state->n64_runtime_media_saw_positive_video = true;
        client_log(state,
                   "n64_runtime_media_metrics",
                   "role=host window_ms=%u capture_fps=%u.%02u capture_age_avg_ms=%u.%03u "
                   "capture_age_max_ms=%u.%03u encode_fps=%u.%02u encode_avg_ms=%u.%03u "
                   "encode_max_ms=%u.%03u relay_pending_ms=%u relay_pending_max_ms=%u "
                   "core_callback_fps=%u.%02u source_capture_due_fps=%u.%02u "
                   "source_capture_success_fps=%u.%02u source_capture_failures=%u "
                   "source_callback_interval_avg_ms=%u.%03u "
                   "source_callback_interval_max_session_ms=%u.%03u "
                   "source_readback_avg_ms=%u.%03u source_readback_max_session_ms=%u.%03u "
                   "source_ipc_write_avg_ms=%u.%03u source_ipc_write_max_session_ms=%u.%03u "
                   "audio_source_fps=%u.%02u audio_sent_fps=%u.%02u "
                   "controller_input_age_ms=%u room_poll_ms=%u room_poll_max_ms=%u",
                   metrics.window_ms,
                   capture_rate / 100u,
                   capture_rate % 100u,
                   metrics.capture_age_avg_us / 1000u,
                   metrics.capture_age_avg_us % 1000u,
                   metrics.capture_age_max_us / 1000u,
                   metrics.capture_age_max_us % 1000u,
                   encode_rate / 100u,
                   encode_rate % 100u,
                   metrics.encode_avg_us / 1000u,
                   metrics.encode_avg_us % 1000u,
                   metrics.encode_max_us / 1000u,
                   metrics.encode_max_us % 1000u,
                   metrics.relay_pending_ms,
                   metrics.relay_pending_max_ms,
                   core_callback_rate / 100u,
                   core_callback_rate % 100u,
                   source_due_rate / 100u,
                   source_due_rate % 100u,
                   source_success_rate / 100u,
                   source_success_rate % 100u,
                   metrics.source_capture_failures,
                   metrics.source_callback_interval_avg_us / 1000u,
                   metrics.source_callback_interval_avg_us % 1000u,
                   metrics.source_callback_interval_max_us / 1000u,
                   metrics.source_callback_interval_max_us % 1000u,
                   metrics.source_readback_avg_us / 1000u,
                   metrics.source_readback_avg_us % 1000u,
                   metrics.source_readback_max_us / 1000u,
                   metrics.source_readback_max_us % 1000u,
                   metrics.source_ipc_write_avg_us / 1000u,
                   metrics.source_ipc_write_avg_us % 1000u,
                   metrics.source_ipc_write_max_us / 1000u,
                   metrics.source_ipc_write_max_us % 1000u,
                   audio_source_rate / 100u,
                   audio_source_rate % 100u,
                   audio_sent_rate / 100u,
                   audio_sent_rate % 100u,
                   input_age_ms,
                   state->room_poll_last_ms,
                   state->room_poll_max_ms);
    }
    else {
        uint32_t receive_rate = media_rate_x100(metrics.received_frames, metrics.window_ms);
        uint32_t decode_rate = media_rate_x100(metrics.decoded_frames, metrics.window_ms);
        uint32_t present_rate = media_rate_x100(metrics.presented_frames, metrics.window_ms);
        uint32_t audio_receive_rate = media_rate_x100(
            metrics.audio_received_frames, metrics.window_ms);
        uint32_t input_interval_avg_ms = state->n64_runtime_media_input_interval_samples
                                             ? (uint32_t)(state->n64_runtime_media_input_interval_total_ms /
                                                          state->n64_runtime_media_input_interval_samples)
                                             : 0;
        if (metrics.received_frames > 0u) state->n64_runtime_media_saw_positive_video = true;
        client_log(state,
                   "n64_runtime_media_metrics",
                   "role=remote window_ms=%u receive_fps=%u.%02u receive_age_avg_ms=%u.%03u "
                   "receive_age_max_ms=%u.%03u receive_age_invalid=%u decode_fps=%u.%02u "
                   "decode_avg_ms=%u.%03u decode_max_ms=%u.%03u decode_queue_peak=%u "
                   "display_overwrites=%u present_fps=%u.%02u "
                   "present_p50_ms=%u.%03u present_p95_ms=%u.%03u present_max_ms=%u.%03u "
                   "present_call_avg_ms=%u.%03u present_call_max_ms=%u.%03u "
                   "video_refresh_hz=%u "
                   "audio_jitter=%u audio_jitter_peak=%u audio_conceals=%u "
                   "audio_receive_fps=%u.%02u "
                   "audio_queue_ms=%u audio_queue_max_ms=%u audio_queue_clears=%u "
                   "controller_send_avg_ms=%u controller_send_max_ms=%u "
                   "room_poll_ms=%u room_poll_max_ms=%u video_vsync=%u video_renderer=%s",
                   metrics.window_ms,
                   receive_rate / 100u,
                   receive_rate % 100u,
                   metrics.receive_age_avg_us / 1000u,
                   metrics.receive_age_avg_us % 1000u,
                   metrics.receive_age_max_us / 1000u,
                   metrics.receive_age_max_us % 1000u,
                   metrics.receive_age_invalid,
                   decode_rate / 100u,
                   decode_rate % 100u,
                   metrics.decode_avg_us / 1000u,
                   metrics.decode_avg_us % 1000u,
                   metrics.decode_max_us / 1000u,
                   metrics.decode_max_us % 1000u,
                   metrics.decode_queue_peak,
                   metrics.display_overwrites,
                   present_rate / 100u,
                   present_rate % 100u,
                   metrics.present_p50_us / 1000u,
                   metrics.present_p50_us % 1000u,
                   metrics.present_p95_us / 1000u,
                   metrics.present_p95_us % 1000u,
                   metrics.present_max_us / 1000u,
                   metrics.present_max_us % 1000u,
                   metrics.present_call_avg_us / 1000u,
                   metrics.present_call_avg_us % 1000u,
                   metrics.present_call_max_us / 1000u,
                   metrics.present_call_max_us % 1000u,
                   metrics.video_refresh_hz,
                   metrics.audio_jitter_packets,
                   metrics.audio_jitter_peak_packets,
                   metrics.audio_conceals,
                   audio_receive_rate / 100u,
                   audio_receive_rate % 100u,
                   metrics.audio_queue_ms,
                   metrics.audio_queue_max_ms,
                   metrics.audio_queue_clears,
                   input_interval_avg_ms,
                   state->n64_runtime_media_input_interval_max_ms,
                   state->room_poll_last_ms,
                   state->room_poll_max_ms,
                   integral_n64_runtime_media_stream_is_video_vsync_paced(
                       state->n64_runtime_media_stream) ? 1u : 0u,
                   integral_n64_runtime_media_stream_video_renderer_driver(
                       state->n64_runtime_media_stream));
    }
    state->n64_runtime_media_input_interval_total_ms = 0;
    state->n64_runtime_media_input_interval_samples = 0;
    state->n64_runtime_media_input_interval_max_ms = 0;
    state->room_poll_max_ms = 0;
}

static bool poll_n64_runtime_media_transport(AppState *state, Uint32 now)
{
    char error[160];
    poll_n64_room_host_process(state);
    if ((!state->room_ready_self || !state->room_ready_peer) && state->n64_runtime_media_connection) {
        reset_n64_runtime_media_connection(state);
        copy_text(state->login.status, sizeof(state->login.status), "N64 MEDIA PEER NOT READY");
        return true;
    }
    if (!state->n64_runtime_media_connection) {
        if (state->room_ready_self && state->room_ready_peer &&
            (state->n64_runtime_media_retry_after_ticks == 0 ||
             (Sint32)(now - state->n64_runtime_media_retry_after_ticks) >= 0)) {
            return request_n64_runtime_media_session(state);
        }
        return true;
    }
    if (!state->n64_runtime_media_paired) {
        int media_status = integral_media_relay_poll(state->n64_runtime_media_connection, error, sizeof(error));
        if (media_status > 0) {
            state->n64_runtime_media_paired = true;
            state->n64_runtime_media_last_input_receive_ticks = now;
            client_log(state,
                       "n64_runtime_media_paired",
                       "session=%s role=%s",
                       state->n64_runtime_media_session_id,
                       state->n64_runtime_media_role);
            snprintf(state->login.status,
                     sizeof(state->login.status),
                     "MEDIA TLS PAIRED %s  NO SAV OVERWRITE",
                     state->n64_runtime_media_role);
            if (strcmp(state->n64_runtime_media_role, "host") == 0) {
                (void)maybe_start_n64_room_host_n64_runtime(state, now);
            }
            return true;
        }
        if (media_status == 0) return true;
        client_log(state,
                   "n64_runtime_media_transport_error",
                   "stage=pair role=%s error=%s",
                   state->n64_runtime_media_role,
                   error);
        reset_n64_runtime_media_connection(state);
        state->n64_runtime_media_retry_after_ticks = now + 5000u;
        snprintf(state->login.status, sizeof(state->login.status), "N64 MEDIA LOST %s", error);
        return false;
    }
    if (strcmp(state->n64_runtime_media_role, "remote") == 0) {
        uint64_t buttons = state->runtime_exit_confirming
                               ? 0
                               : n64_remote_keyboard_buttons(state);
        if (buttons != state->n64_runtime_media_last_sent_buttons ||
            now - state->n64_runtime_media_last_input_send_ticks >= 50u) {
            int sent = integral_media_relay_send_controller(state->n64_runtime_media_connection,
                                                       ++state->n64_runtime_media_input_sequence,
                                                       buttons,
                                                       error,
                                                       sizeof(error));
            if (sent < 0) {
                client_log(state,
                           "n64_runtime_media_transport_error",
                           "stage=remote-input role=remote error=%s",
                           error);
                reset_n64_runtime_media_connection(state);
                state->n64_runtime_media_retry_after_ticks = now + 5000u;
                snprintf(state->login.status, sizeof(state->login.status), "N64 INPUT LOST %s", error);
                return false;
            }
            if (sent > 0) {
                if (state->n64_runtime_media_last_input_send_ticks) {
                    uint32_t interval_ms = now - state->n64_runtime_media_last_input_send_ticks;
                    state->n64_runtime_media_input_interval_total_ms += interval_ms;
                    state->n64_runtime_media_input_interval_samples++;
                    if (interval_ms > state->n64_runtime_media_input_interval_max_ms) {
                        state->n64_runtime_media_input_interval_max_ms = interval_ms;
                    }
                }
                state->n64_runtime_media_last_sent_buttons = buttons;
                state->n64_runtime_media_last_input_send_ticks = now;
            }
        }
        int media_result = integral_n64_runtime_media_stream_pump_remote(
            state->n64_runtime_media_stream,
            state->n64_runtime_media_connection,
            (uint64_t)now * 1000u,
            error,
            sizeof(error));
        if (media_result < 0) {
            client_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=remote-decode role=remote error=%s",
                       error);
            reset_n64_runtime_media_connection(state);
            state->n64_runtime_media_retry_after_ticks = now + 5000u;
            snprintf(state->login.status, sizeof(state->login.status),
                     "N64 MEDIA DECODE LOST %s", error);
            return false;
        }
        log_n64_runtime_media_metrics(state, now);
        return true;
    }
    (void)maybe_start_n64_room_host_n64_runtime(state, now);
    for (unsigned count = 0; count < 8; count++) {
        uint32_t sequence = 0;
        uint64_t buttons = 0;
        int received = integral_media_relay_poll_controller(state->n64_runtime_media_connection,
                                                       &sequence,
                                                       &buttons,
                                                       error,
                                                       sizeof(error));
        if (received < 0) {
            client_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=host-input role=host error=%s",
                       error);
            reset_n64_runtime_media_connection(state);
            state->n64_runtime_media_retry_after_ticks = now + 5000u;
            snprintf(state->login.status, sizeof(state->login.status), "N64 INPUT LOST %s", error);
            return false;
        }
        if (received == 2) {
            client_log(state,
                       "n64_controller_frame_dropped",
                       "role=host error=%s",
                       error);
            continue;
        }
        if (received == 0) break;
        if (!write_n64_remote_input_state(state, sequence, buttons)) {
#ifdef _WIN32
            unsigned long os_error = (unsigned long)GetLastError();
#else
            unsigned long os_error = (unsigned long)errno;
#endif
            if (state->n64_runtime_media_input_ipc_failure_since_ticks == 0) {
                state->n64_runtime_media_input_ipc_failure_since_ticks = now ? now : 1u;
                state->n64_runtime_media_input_ipc_failures = 1u;
                client_log(state,
                           "n64_input_ipc_retry",
                           "role=host os_error=%lu limit_ms=%u",
                           os_error,
                           INTEGRAL_N64_RUNTIME_INPUT_IPC_FAILURE_LIMIT_MS);
            }
            else {
                state->n64_runtime_media_input_ipc_failures++;
            }
            Uint32 failure_ms = now - state->n64_runtime_media_input_ipc_failure_since_ticks;
            if (failure_ms < INTEGRAL_N64_RUNTIME_INPUT_IPC_FAILURE_LIMIT_MS) {
                copy_text(state->login.status,
                          sizeof(state->login.status),
                          "N64 INPUT IPC RETRYING");
                break;
            }
            client_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=host-input-ipc role=host error=write-failed "
                       "os_error=%lu failures=%u duration_ms=%u",
                       os_error,
                       state->n64_runtime_media_input_ipc_failures,
                       failure_ms);
            reset_n64_runtime_media_connection(state);
            state->n64_runtime_media_retry_after_ticks = now + 5000u;
            copy_text(state->login.status, sizeof(state->login.status), "N64 INPUT IPC FAILED");
            return false;
        }
        if (state->n64_runtime_media_input_ipc_failure_since_ticks != 0) {
            client_log(state,
                       "n64_input_ipc_recovered",
                       "role=host failures=%u duration_ms=%u",
                       state->n64_runtime_media_input_ipc_failures,
                       now - state->n64_runtime_media_input_ipc_failure_since_ticks);
            state->n64_runtime_media_input_ipc_failure_since_ticks = 0;
            state->n64_runtime_media_input_ipc_failures = 0;
        }
        uint64_t previous_buttons = state->n64_runtime_media_remote_buttons;
        state->n64_runtime_media_remote_buttons = buttons;
        state->n64_runtime_media_last_input_receive_ticks = now;
        if (buttons != previous_buttons) {
            client_log(state,
                       "n64_remote_input",
                       "session=%s sequence=%u buttons=0x%04llx",
                       state->n64_runtime_media_session_id,
                       sequence,
                       (unsigned long long)buttons);
        }
    }
    if (state->n64_runtime_media_remote_buttons != 0 &&
        now - state->n64_runtime_media_last_input_receive_ticks >= 500u) {
        state->n64_runtime_media_remote_buttons = 0;
        (void)write_n64_remote_input_state(state,
                                           state->n64_runtime_media_input_sequence + 1u,
                                           0);
        client_log(state, "n64_remote_input_neutral", "reason=timeout");
    }
    if (state->n64_runtime_media_ipc_path[0]) {
        int opened = integral_n64_runtime_media_stream_open_host(state->n64_runtime_media_stream,
                                                          state->n64_runtime_media_ipc_path,
                                                          error,
                                                          sizeof(error));
        if (opened < 0 ||
            (opened == 0 &&
             integral_n64_runtime_media_stream_pump_host(state->n64_runtime_media_stream,
                                                   state->n64_runtime_media_connection,
                                                   (uint64_t)now * 1000u,
                                                   error,
                                                   sizeof(error)) < 0)) {
            client_log(state,
                       "n64_runtime_media_transport_error",
                       "stage=host-encode role=host opened=%d error=%s",
                       opened,
                       error);
            reset_n64_runtime_media_connection(state);
            state->n64_runtime_media_retry_after_ticks = now + 5000u;
            snprintf(state->login.status, sizeof(state->login.status),
                     "N64 MEDIA ENCODE LOST %s", error);
            return false;
        }
    }
    log_n64_runtime_media_metrics(state, now);
    return true;
}

static void cycle_n64_room_slot(AppState *state, unsigned row, int delta)
{
    int candidates[INTEGRAL_ROM_SLOTS];
    unsigned count = 0;
    for (int i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        const IntegralConfigRomSlot *slot = registered_rom_slot_at(state, i);
        if (!slot) {
            continue;
        }
        if (row == 0) {
            if (slot_is_supported_n64(slot)) {
                candidates[count++] = i;
            }
            continue;
        }
        char game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
        if (!n64_room_gb_game_type(state, i, game_type, sizeof(game_type))) {
            continue;
        }
        candidates[count++] = i;
    }
    if (count == 0) {
        copy_text(state->login.status,
                  sizeof(state->login.status),
                  row == 0 ? "USER1 N64 ROM REQUIRED" : "GB ROM REQUIRED");
        return;
    }

    int *selected_index = row == 0 ? &state->n64_room_n64_slot_index
                                   : (row == 1 ? &state->n64_room_user1_gb_slot_index
                                               : &state->n64_room_user2_gb_slot_index);
    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if (*selected_index == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current < 0 ? (delta < 0 ? (int)count - 1 : 0) : current + delta;
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    *selected_index = candidates[next];

    state->n64_room_ready = false;
    reset_n64_runtime_media_connection(state);
    snprintf(state->login.status,
             sizeof(state->login.status),
             "%s SELECTED ROM%d",
             row == 0 ? "USER1 N64" : (row == 1 ? "USER1 GB" : "USER2 GB"),
             *selected_index + 1);
}

static void cycle_room_rom_slot(AppState *state, int delta)
{
    int candidates[INTEGRAL_ROM_SLOTS];
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (slot_is_supported_gb(&state->rom_slots[i])) {
            candidates[count++] = (int)i;
        }
    }
    if (count == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "NO ROM1-8 SET");
        return;
    }

    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if (state->room_slot_index == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)count - 1 : 0;
    }
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    state->room_slot_index = candidates[next];
    snprintf(state->login.status,
             sizeof(state->login.status),
             "ROOM SLOT ROM%d",
             state->room_slot_index + 1);
}

static void clear_selected_rom_slot(AppState *state)
{
    if (state->rom_selected >= INTEGRAL_ROM_SLOTS) {
        return;
    }
    if (state->rom_initial_save_import_slot == (int)state->rom_selected) {
        state->rom_initial_save_import_slot = -1;
        state->rom_initial_save_import_path[0] = '\0';
    }
    memset(&state->rom_slots[state->rom_selected], 0, sizeof(state->rom_slots[state->rom_selected]));
    if (integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "ROM CLEAR CONFIG SAVE FAILED");
        return;
    }
    snprintf(state->login.status, sizeof(state->login.status), "ROM%u CLEARED", state->rom_selected + 1);
}

static void draw_rom_register(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    draw_header(renderer, "ROM REGISTER", state->login.username, state->login.server);
    integral_sdl_draw_text(renderer, 24, 88, "MAX 8 ROMS  SAV IS SERVER MANAGED", 1, muted);

    for (unsigned i = 0; i < INTEGRAL_ROM_ROWS; i++) {
        int y = 112 + (int)i * 27;
        if (state->rom_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 6, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 27};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
        }
        if (i < INTEGRAL_ROM_SLOTS) {
            char label_text[24];
            char summary[320];
            snprintf(label_text, sizeof(label_text), "ROM%u", i + 1);
            format_rom_slot_summary(&state->rom_slots[i], &state->server_rom_slots[i], summary, sizeof(summary));
            integral_sdl_draw_text(renderer, 48, y, label_text, 2, state->rom_selected == i ? selected : label);
            if (state->rom_selected == i) {
                draw_marquee_text_fit(renderer, 146, y + 4, summary, 1, value, 300, i * 7u);
            }
            else {
                integral_sdl_draw_text_fit(renderer, 146, y + 4, summary, 1, value, 300);
            }
        }
        else if (i == INTEGRAL_ROM_REGISTER_ROW) {
            integral_sdl_draw_text(renderer, 48, y, "REGISTER", 2, state->rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "APPLY CHANGES", 1, value);
        }
        else if (i == INTEGRAL_ROM_EXPORT_ROW) {
            integral_sdl_draw_text(renderer, 48, y, "EXPORT", 2, state->rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "DOWNLOAD SAVS", 1, value);
        }
        else {
            integral_sdl_draw_text(renderer, 48, y, "BACK", 2, state->rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "RETURN MENU", 1, value);
        }
    }

    if (state->rom_edit_target != ROM_EDIT_NONE && state->rom_selected < INTEGRAL_ROM_SLOTS) {
        const IntegralConfigRomSlot *slot = &state->rom_slots[state->rom_selected];
        const char *label_text = state->rom_edit_target == ROM_EDIT_INITIAL_SAVE
                                      ? "SELECT INITIAL SAV PATH"
                                      : "EDIT ROM PATH";
        const char *value_text = state->rom_edit_target == ROM_EDIT_INITIAL_SAVE
                                     ? state->rom_initial_save_import_path
                                     : slot->rom_path;
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 240);
        SDL_Rect overlay = {.x = 42, .y = 320, .w = 396, .h = 88};
        SDL_RenderFillRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 62, 342, label_text, 2, selected);
        integral_sdl_draw_text_fit(renderer, 62, 372, value_text[0] ? value_text : "<EMPTY>", 1, value, 340);
        integral_sdl_draw_text(renderer, 394, 372, "_", 1, selected);
    }
    else if (state->rom_browser_active) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 86, .w = 412, .h = 322};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 106, "ROMS FOLDER", 2, selected);
        unsigned visible = state->rom_browser_count < 8 ? state->rom_browser_count : 8;
        unsigned start = 0;
        if (state->rom_browser_selected >= visible) {
            start = state->rom_browser_selected - visible + 1;
        }
        for (unsigned i = 0; i < visible; i++) {
            unsigned index = start + i;
            int y = 142 + (int)i * 28;
            if (index == state->rom_browser_selected) {
                SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
                SDL_Rect row = {.x = 46, .y = y - 6, .w = 388, .h = 24};
                SDL_RenderFillRect(renderer, &row);
                integral_sdl_draw_text(renderer, 56, y, ">", 1, selected);
            }
            integral_sdl_draw_text_fit(renderer,
                                  76,
                                  y,
                                  path_file_name(state->rom_browser_entries[index]),
                                  1,
                                  value,
                                  340);
        }
        integral_sdl_draw_text(renderer, 54, 374, "LEFT/RIGHT MOVE  ENTER CHOOSE  ESC CLOSE", 1, muted);
    }
    else if (state->rom_confirm_delete) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 316, .w = 412, .h = 92};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 338, "DELETE SERVER SAV?", 2, selected);
        integral_sdl_draw_text(renderer, 54, 368, "ENTER YES   ESC NO", 1, value);
    }
    else if (state->rom_confirm_initial_save_import) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 316, .w = 412, .h = 92};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 338, "SEND SELECTED INITIAL SAV?", 2, selected);
        integral_sdl_draw_text(renderer, 54, 368, "ENTER YES   ESC NO", 1, value);
    }

    integral_sdl_draw_text_fit(renderer, 22, 424, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    if (state->allow_user_initial_save_import) {
        integral_sdl_draw_text(renderer, 22, 442, "LEFT/RIGHT SET ROM  ENTER EDIT  F4 LIST  F5 SAV", 1, muted);
        if (state->rom_initial_save_import_slot >= 0) {
            char import_status[64];
            snprintf(import_status, sizeof(import_status),
                     "INITIAL SAV SELECTED FOR ROM%d  REGISTER APPLIES",
                     state->rom_initial_save_import_slot + 1);
            integral_sdl_draw_text(renderer, 22, 460, import_status, 1, muted);
        }
        else {
            integral_sdl_draw_text(renderer, 22, 460, "REGISTER ROW APPLIES  BACKSPACE DELETE", 1, muted);
        }
    }
    else {
        integral_sdl_draw_text(renderer, 22, 442, "LEFT/RIGHT SET ROM  ENTER EDIT  F4 LIST", 1, muted);
        integral_sdl_draw_text(renderer, 22, 460, "REGISTER ROW APPLIES  BACKSPACE DELETE", 1, muted);
    }
    SDL_RenderPresent(renderer);
}

static void submit_login(AppState *app)
{
    LoginState *state = &app->login;
    if (state->server[0] == '\0') {
        copy_text(state->status, sizeof(state->status), "SERVER URL REQUIRED");
        client_log(app, "login_blocked", "reason=missing_server_config");
        return;
    }
    if (state->username[0] == '\0' || state->password[0] == '\0') {
        copy_text(state->status, sizeof(state->status), "LOGIN NEEDS USERNAME PASSWORD");
        client_log(app, "login_blocked", "reason=missing_field server=%s username=%s", state->server, state->username);
        return;
    }
    client_log(app, "login_start", "server=%s server_id=%s username=%s", state->server, login_server_id_or_default(state), state->username);
    char error[160];
    char authenticated_username[64];
    int must_change_password = 0;
    int allow_user_initial_save_import = 0;
    if (integral_api_login(state->server,
                      state->username,
                      state->password,
                      login_server_id_or_default(state),
                      state->token,
                      sizeof(state->token),
                      authenticated_username,
                      sizeof(authenticated_username),
                      &must_change_password,
                      &allow_user_initial_save_import,
                      error,
                      sizeof(error)) != 0) {
        snprintf(state->status, sizeof(state->status), "LOGIN FAILED %s", error);
        client_log(app, "login_failed", "error=%s", error);
        return;
    }
    copy_text(state->username, sizeof(state->username), authenticated_username);
    app->allow_user_initial_save_import = allow_user_initial_save_import != 0;
    copy_text(state->status, sizeof(state->status), "LOGIN OK");
    client_log(app, "login_ok", "must_change_password=%d", must_change_password);
    IntegralConfigLogin login_config;
    memset(&login_config, 0, sizeof(login_config));
    login_config.remember = must_change_password ? 0 : state->remember_login;
    copy_text(login_config.server, sizeof(login_config.server), state->server);
    copy_text(login_config.server_id, sizeof(login_config.server_id), login_server_id_or_default(state));
    if (state->remember_login && !must_change_password) {
        copy_text(login_config.username, sizeof(login_config.username), state->username);
        if (integral_credential_store_save(
                state->server, state->username, state->password
            ) != 0) {
            login_config.remember = 0;
            state->remember_login = false;
            copy_text(state->status, sizeof(state->status), "LOGIN OK  CREDENTIAL SAVE FAILED");
        }
    }
    if (must_change_password) {
        (void)integral_credential_store_delete(state->server, state->username);
    }
    if (integral_config_save_login(app->base_config_path, &login_config) != 0) {
        copy_text(state->status, sizeof(state->status), "LOGIN OK  CONFIG SAVE FAILED");
    }
    if (must_change_password) {
        password_change_state_init(&app->password_change);
        copy_text(app->password_change.status, sizeof(app->password_change.status), "PASSWORD CHANGE REQUIRED");
        app->screen = SCREEN_PASSWORD_CHANGE;
        SDL_StopTextInput();
        client_log(app, "password_change_required", "");
        return;
    }
    activate_user_config(app);
    app->screen = SCREEN_MAIN_MENU;
    IntegralApiRoom current_room;
    int has_current_room = 0;
    if (integral_api_get_current_room(app->login.server,
                                app->login.token,
                                &current_room,
                                &has_current_room,
                                error,
                                sizeof(error)) == 0 && has_current_room) {
        activate_matched_room(app, &current_room);
        copy_text(app->login.status, sizeof(app->login.status), "RETURNED TO ACTIVE ROOM");
        client_log(app, "room_restore_ok", "room=%u mode=%s",
                   current_room.room_number, current_room.room_type);
        return;
    }
    client_log(app, "screen_change", "to=main config=%s", app->config_path);
}

static int save_login_form_config(AppState *app)
{
    IntegralConfigLogin login_config;
    memset(&login_config, 0, sizeof(login_config));
    login_config.remember = app->login.remember_login;
    copy_text(login_config.server, sizeof(login_config.server), app->login.server);
    copy_text(login_config.server_id, sizeof(login_config.server_id), login_server_id_or_default(&app->login));
    if (app->login.remember_login) {
        copy_text(login_config.username, sizeof(login_config.username), app->login.username);
    }
    return integral_config_save_login(app->base_config_path, &login_config);
}

static bool password_is_valid_client_side(const char *password)
{
    if (strlen(password) < 8) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)password; *p; p++) {
        if (*p > 126 || !isalnum(*p)) {
            return false;
        }
    }
    return true;
}

static void submit_password_change(AppState *app)
{
    PasswordChangeState *state = &app->password_change;
    if (!password_is_valid_client_side(state->new_password)) {
        copy_text(state->status, sizeof(state->status), "PASSWORD MUST BE 8+ ALNUM");
        return;
    }
    if (strcmp(state->new_password, state->confirm_password) != 0) {
        copy_text(state->status, sizeof(state->status), "CONFIRM DOES NOT MATCH");
        return;
    }
    char error[160];
    if (integral_api_change_password(app->login.server,
                                app->login.token,
                                state->new_password,
                                error,
                                sizeof(error)) != 0) {
        snprintf(state->status, sizeof(state->status), "CHANGE FAILED %s", error);
        return;
    }

    if (app->login.remember_login) {
        copy_text(app->login.password, sizeof(app->login.password), state->new_password);
        IntegralConfigLogin login_config;
        memset(&login_config, 0, sizeof(login_config));
        login_config.remember = app->login.remember_login;
        copy_text(login_config.server, sizeof(login_config.server), app->login.server);
        copy_text(login_config.server_id, sizeof(login_config.server_id), login_server_id_or_default(&app->login));
        copy_text(login_config.username, sizeof(login_config.username), app->login.username);
        if (integral_credential_store_save(
                app->login.server, app->login.username, app->login.password
            ) != 0 ||
            integral_config_save_login(app->base_config_path, &login_config) != 0) {
            copy_text(app->login.status, sizeof(app->login.status), "PASSWORD CHANGED  CONFIG SAVE FAILED");
        }
        else {
            copy_text(app->login.status, sizeof(app->login.status), "PASSWORD CHANGED");
        }
    }
    else {
        copy_text(app->login.status, sizeof(app->login.status), "PASSWORD CHANGED");
    }
    password_change_state_init(state);
    activate_user_config(app);
    app->screen = SCREEN_MAIN_MENU;
    SDL_StopTextInput();
}

static void move_selection(LoginState *state, int delta)
{
    int selected = (int)state->selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_FIELD_COUNT - 1;
    }
    if (selected >= INTEGRAL_FIELD_COUNT) {
        selected = 0;
    }
    state->selected = (LoginField)selected;
    state->editing = false;
}

static void handle_text_input(LoginState *state, const SDL_TextInputEvent *text)
{
    char *value = field_value(state, state->selected);
    size_t capacity = field_capacity(state->selected);
    if (!value || capacity == 0) {
        return;
    }
    if (!append_ascii_text(value, capacity, text->text)) {
        copy_text(state->status, sizeof(state->status), "ASCII INPUT ONLY");
    }
}

static void handle_key(LoginState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            if (state->editing) {
                state->editing = false;
                SDL_StopTextInput();
                copy_text(state->status, sizeof(state->status), "EDIT CANCELLED");
            }
            else {
                state->quit = true;
            }
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_selection(state, 1);
            SDL_StopTextInput();
            break;
        case SDLK_UP:
            move_selection(state, -1);
            SDL_StopTextInput();
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
            if (state->selected == FIELD_ENV) {
                cycle_login_server(state);
            }
            else if (state->selected == FIELD_REMEMBER) {
                state->remember_login = !state->remember_login;
                copy_text(state->status, sizeof(state->status), state->remember_login ? "REMEMBER LOGIN ON" : "REMEMBER LOGIN OFF");
            }
            break;
        case SDLK_F2:
            if (state->selected == FIELD_REMEMBER) {
                state->remember_login = !state->remember_login;
                copy_text(state->status, sizeof(state->status), state->remember_login ? "REMEMBER LOGIN ON" : "REMEMBER LOGIN OFF");
            }
            else if (state->selected == FIELD_ENV) {
                cycle_login_server(state);
            }
            else if (state->selected != FIELD_ACTION) {
                state->editing = true;
                SDL_StartTextInput();
                copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
            }
            break;
        case SDLK_BACKSPACE:
            if (state->editing) {
                char *value = field_value(state, state->selected);
                if (value) {
                    remove_last_char(value);
                }
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->selected == FIELD_ACTION) {
                copy_text(state->status, sizeof(state->status), "PRESS ENTER TO LOGIN");
            }
            else if (state->selected == FIELD_REMEMBER) {
                state->remember_login = !state->remember_login;
                copy_text(state->status, sizeof(state->status), state->remember_login ? "REMEMBER LOGIN ON" : "REMEMBER LOGIN OFF");
            }
            else if (state->selected == FIELD_ENV) {
                cycle_login_server(state);
            }
            else {
                state->editing = !state->editing;
                if (state->editing) {
                    SDL_StartTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
                }
                else {
                    SDL_StopTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT SAVED");
                }
            }
            break;
        default:
            break;
    }
}

static void handle_login_key(AppState *app, const SDL_KeyboardEvent *key)
{
    LoginState *state = &app->login;
    if (key->repeat) {
        return;
    }
    if ((key->keysym.sym == SDLK_RETURN || key->keysym.sym == SDLK_KP_ENTER) &&
        state->selected == FIELD_ACTION) {
        submit_login(app);
        return;
    }
    char before_server_id[sizeof(state->server_id)];
    char before_server[sizeof(state->server)];
    bool before_remember = state->remember_login;
    copy_text(before_server_id, sizeof(before_server_id), login_server_id_or_default(state));
    copy_text(before_server, sizeof(before_server), state->server);
    handle_key(state, key);
    if (strcasecmp(before_server_id, login_server_id_or_default(state)) != 0 ||
        strcmp(before_server, state->server) != 0 ||
        before_remember != state->remember_login) {
        if (save_login_form_config(app) != 0) {
            copy_text(state->status, sizeof(state->status), "LOGIN PREF SAVE FAILED");
        }
    }
    app->quit = state->quit;
}

static void move_password_change_selection(PasswordChangeState *state, int delta)
{
    int selected = (int)state->selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_PASSWORD_CHANGE_ROWS - 1;
    }
    if (selected >= INTEGRAL_PASSWORD_CHANGE_ROWS) {
        selected = 0;
    }
    state->selected = (PasswordChangeField)selected;
    state->editing = false;
}

static char *password_change_value(PasswordChangeState *state)
{
    switch (state->selected) {
        case PASSWORD_CHANGE_NEW:
            return state->new_password;
        case PASSWORD_CHANGE_CONFIRM:
            return state->confirm_password;
        default:
            return NULL;
    }
}

static void handle_password_change_key(AppState *app, const SDL_KeyboardEvent *key)
{
    PasswordChangeState *state = &app->password_change;
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_TAB:
        case SDLK_DOWN:
            move_password_change_selection(state, 1);
            SDL_StopTextInput();
            break;
        case SDLK_UP:
            move_password_change_selection(state, -1);
            SDL_StopTextInput();
            break;
        case SDLK_F2:
            if (state->selected != PASSWORD_CHANGE_SAVE) {
                state->editing = true;
                SDL_StartTextInput();
                copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
            }
            break;
        case SDLK_F3:
            state->password_visible = !state->password_visible;
            break;
        case SDLK_BACKSPACE:
            if (state->editing) {
                char *value = password_change_value(state);
                if (value) {
                    remove_last_char(value);
                }
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->selected == PASSWORD_CHANGE_SAVE) {
                submit_password_change(app);
            }
            else {
                state->editing = !state->editing;
                if (state->editing) {
                    SDL_StartTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
                }
                else {
                    SDL_StopTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT SAVED");
                }
            }
            break;
        default:
            break;
    }
}

static void handle_password_change_text_input(AppState *app, const SDL_TextInputEvent *text)
{
    PasswordChangeState *state = &app->password_change;
    if (!state->editing) {
        return;
    }
    char *value = password_change_value(state);
    if (!value) {
        return;
    }
    if (!append_alnum_text(value, sizeof(state->new_password), text->text)) {
        copy_text(state->status, sizeof(state->status), "ALNUM INPUT ONLY");
    }
}

static void move_main_selection(AppState *state, int delta)
{
    int selected = (int)state->main_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_MAIN_ROWS - 1;
    }
    if (selected >= INTEGRAL_MAIN_ROWS) {
        selected = 0;
    }
    state->main_selected = (unsigned)selected;
}

static void move_room_mode_selection(AppState *state, int delta)
{
    int selected = (int)state->room_mode_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_ROOM_MODE_ROWS - 1;
    }
    if (selected >= INTEGRAL_ROOM_MODE_ROWS) {
        selected = 0;
    }
    state->room_mode_selected = (unsigned)selected;
}

static void handle_room_mode_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
        case SDLK_RIGHT:
            move_room_mode_selection(state, 1);
            break;
        case SDLK_UP:
        case SDLK_LEFT:
            move_room_mode_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        {
            IntegralApiRoom room;
            char error[160];
            const char *mode = state->room_mode_selected == 0 ? "link_cable" : "n64";
            copy_text(state->login.status, sizeof(state->login.status), "CREATING ROOM");
            if (integral_api_create_room(state->login.server, state->login.token, mode,
                                         &room, error, sizeof(error)) != 0) {
                if (strstr(error, "no ROOM") || strstr(error, "allocation")) {
                    copy_text(state->login.status, sizeof(state->login.status),
                              state->room_mode_selected == 0
                                  ? "NO LINK CABLE ROOM AVAILABLE"
                                  : "NO N64 ROOM AVAILABLE");
                }
                else if (strstr(error, "already in a ROOM")) {
                    copy_text(state->login.status, sizeof(state->login.status), "ALREADY IN A ROOM");
                }
                else {
                    snprintf(state->login.status, sizeof(state->login.status),
                             "CREATE ROOM FAILED %s", error);
                }
                client_log(state, "room_create_failed", "mode=%s error=%s", mode, error);
                break;
            }
            client_log(state, "room_create_ok", "mode=%s room=%u", mode, room.room_number);
            activate_matched_room(state, &room);
            break;
        }
        default:
            break;
    }
}

static void handle_join_room_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) return;
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->room_code_editing = false;
            SDL_StopTextInput();
            state->screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_BACKSPACE:
            remove_last_char(state->room_code_input);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        {
            IntegralApiRoom room;
            char error[160];
            if (strlen(state->room_code_input) != 5) {
                copy_text(state->login.status, sizeof(state->login.status),
                          "ROOM CODE MUST BE 5 DIGITS");
                break;
            }
            if (integral_api_join_room_code(state->login.server, state->login.token,
                                            state->room_code_input, &room,
                                            error, sizeof(error)) != 0) {
                if (strstr(error, "not available")) {
                    copy_text(state->login.status, sizeof(state->login.status), "ROOM NOT AVAILABLE");
                }
                else if (strstr(error, "too many ROOM join")) {
                    copy_text(state->login.status, sizeof(state->login.status),
                              "TOO MANY ATTEMPTS  TRY LATER");
                }
                else if (strstr(error, "already in a ROOM")) {
                    copy_text(state->login.status, sizeof(state->login.status), "ALREADY IN A ROOM");
                }
                else {
                    snprintf(state->login.status, sizeof(state->login.status),
                             "JOIN ROOM FAILED %s", error);
                }
                client_log(state, "room_code_join_failed", "error=%s", error);
                break;
            }
            state->room_code_editing = false;
            SDL_StopTextInput();
            client_log(state, "room_code_join_ok", "room=%u mode=%s",
                       room.room_number, room.room_type);
            activate_matched_room(state, &room);
            break;
        }
        default:
            break;
    }
}

static void handle_join_room_text_input(AppState *state, const SDL_TextInputEvent *text)
{
    for (const char *p = text->text; *p && strlen(state->room_code_input) < 5; p++) {
        if (*p >= '0' && *p <= '9') {
            size_t length = strlen(state->room_code_input);
            state->room_code_input[length] = *p;
            state->room_code_input[length + 1] = '\0';
        }
    }
}

static void handle_main_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            leave_current_room(state, false);
            state->screen = SCREEN_LOGIN;
            state->login.selected = FIELD_ACTION;
            copy_text(state->login.status, sizeof(state->login.status), "LOGGED OUT LOCAL UI");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_main_selection(state, 1);
            break;
        case SDLK_UP:
            move_main_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->main_selected == 0) {
                state->screen = SCREEN_LOCAL_MODE;
                state->local_mode_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            }
            else if (state->main_selected == 1) {
                state->screen = SCREEN_ROOM_MODE;
                state->room_mode_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "SELECT ROOM MODE");
            }
            else if (state->main_selected == 2) {
                state->screen = SCREEN_JOIN_ROOM;
                state->room_code_input[0] = '\0';
                state->room_code_editing = true;
                SDL_StartTextInput();
                copy_text(state->login.status, sizeof(state->login.status), "ENTER ROOM CODE");
            }
            else if (state->main_selected == 3) {
                enter_rom_register(state);
            }
            else if (state->main_selected == 4) {
                state->screen = SCREEN_KEY_CONFIG;
                state->key_selected = 0;
                state->key_capture_target = KEY_CAPTURE_NONE;
                copy_text(state->login.status, sizeof(state->login.status), "KEY CONFIG");
            }
            else {
                copy_text(state->login.status, sizeof(state->login.status), "BUTTON PLACEHOLDER");
            }
            break;
        default:
            break;
    }
}

static void refresh_room_with_status(AppState *state, bool update_status)
{
    if (state->login.token[0] == '\0') {
        if (update_status) {
            copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        }
        return;
    }
    if (state->screen == SCREEN_N64_ROOM) state->room_poll_epoch++;
    char error[160];
    IntegralApiRoom current;
    int has_room = 0;
    if (integral_api_get_current_room(state->login.server,
                                state->login.token,
                                &current,
                                &has_room,
                                error,
                                sizeof(error)) != 0) {
        if (update_status) {
            snprintf(state->login.status, sizeof(state->login.status), "ROOM REFRESH FAILED %s", error);
        }
        return;
    }
    memset(&state->current_room, 0, sizeof(state->current_room));
    if (has_room && current.room_number >= 1 && current.room_number <= INTEGRAL_API_ROOMS) {
        state->current_room = current;
        state->room_number = current.room_number;
    }
    if (state->screen == SCREEN_ROOM || state->screen == SCREEN_N64_ROOM) {
        if (!has_room) {
            state->screen = SCREEN_MAIN_MENU;
            state->room_number = 0;
            copy_text(state->login.status, sizeof(state->login.status), "ROOM CLOSED");
            return;
        }
        load_room_chat_from_api(state);
        sync_room_ready_flags_from_api(state);
    }
}

static void refresh_room(AppState *state)
{
    refresh_room_with_status(state, true);
}

static void refresh_room_quiet(AppState *state)
{
    refresh_room_with_status(state, false);
}

static void poll_n64_room_async(AppState *state, Uint32 now)
{
    if (!state->n64_room_poll_worker) return;
    IntegralRoomPollResult result;
    int taken = integral_room_poll_worker_take(state->n64_room_poll_worker, &result);
    if (taken < 0) {
        client_log(state, "room_poll_async_error", "stage=take");
    }
    else if (taken > 0) {
        bool current = state->screen == SCREEN_N64_ROOM &&
                       state->room_number == result.room_number &&
                       state->room_poll_epoch == result.request_id &&
                       strcmp(state->login.server, result.server) == 0 &&
                       strcmp(state->login.token, result.token) == 0;
        if (current) {
            state->room_poll_last_ms = result.elapsed_ms;
            if (result.elapsed_ms > state->room_poll_max_ms) {
                state->room_poll_max_ms = result.elapsed_ms;
            }
            if (result.heartbeat_attempted && result.heartbeat_succeeded) {
                state->last_room_heartbeat_ticks = now;
            }
            if (result.heartbeat_attempted) {
                handle_room_heartbeat_result(state,
                                              &result.heartbeat_status,
                                              result.heartbeat_succeeded,
                                              result.error);
            }
            if (result.room_result == 0) {
                state->current_room = result.room;
                load_room_chat_from_api(state);
                sync_room_ready_flags_from_api(state);
            }
            else {
                client_log(state,
                           "room_poll_async_error",
                           "stage=room elapsed_ms=%u error=%s",
                           result.elapsed_ms,
                           result.error);
            }
            if (result.heartbeat_attempted && !result.heartbeat_succeeded) {
                client_log(state,
                           "room_poll_async_error",
                           "stage=heartbeat elapsed_ms=%u error=%s",
                           result.elapsed_ms,
                           result.error);
            }
        }
    }
    if (state->screen != SCREEN_N64_ROOM || state->login.token[0] == '\0' ||
        now - state->last_room_poll_ticks < 1000u ||
        integral_room_poll_worker_is_busy(state->n64_room_poll_worker)) return;
    bool heartbeat_due = integral_room_heartbeat_attempt_due(
        state->last_room_heartbeat_attempt_ticks,
        state->room_heartbeat_attempted,
        now,
        INTEGRAL_ROOM_HEARTBEAT_MS);
    int started = integral_room_poll_worker_start(state->n64_room_poll_worker,
                                              state->login.server,
                                              state->login.token,
                                              state->room_number,
                                              state->room_poll_epoch,
                                              heartbeat_due);
    if (started == 0) {
        state->last_room_poll_ticks = now;
        if (heartbeat_due) {
            state->last_room_heartbeat_attempt_ticks = now;
            state->room_heartbeat_attempted = true;
        }
    }
    else if (started < 0) {
        client_log(state, "room_poll_async_error", "stage=start error=%s", SDL_GetError());
    }
}

static void sync_room_state(AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS || state->login.token[0] == '\0') {
        return;
    }
    char slot_label[32];
    if (registered_rom_slot_at(state, state->room_slot_index)) {
        snprintf(slot_label, sizeof(slot_label), "ROM%d", state->room_slot_index + 1);
    }
    else {
        slot_label[0] = '\0';
    }
    char error[160];
    if (integral_api_update_room_state(state->login.server,
                                        state->login.token,
                                        state->room_number,
                                        slot_label,
                                        state->room_ready_self ? 1 : 0,
                                        current_room_is_user1(state)
                                            ? room_link_mode_api_name(state->room_link_mode)
                                            : NULL,
                                        error,
                                        sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "ROOM SYNC FAILED %s", error);
        client_log(state,
                   "room_sync_failed",
                   "room=%u slot=%s ready=%d mode=%s error=%s",
                   state->room_number,
                   slot_label[0] ? slot_label : "-",
                   state->room_ready_self ? 1 : 0,
                   room_link_mode_api_name(state->room_link_mode),
                   error);
        return;
    }
    client_log(state,
               "room_sync_ok",
               "room=%u slot=%s ready=%d mode=%s",
               state->room_number,
               slot_label[0] ? slot_label : "-",
               state->room_ready_self ? 1 : 0,
               room_link_mode_api_name(state->room_link_mode));
    if (state->screen != SCREEN_ROOM) {
        refresh_room_quiet(state);
    }
}

static bool current_room_is_ready_to_start(const AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS) {
        return false;
    }
    if (current_room_game_ended(state)) {
        return false;
    }
    const IntegralApiRoom *room = &state->current_room;
    return room->user1[0] != '\0' && room->user2[0] != '\0' && room->slot1[0] != '\0' && room->slot2[0] != '\0' &&
           room->ready1 && room->ready2;
}

static bool current_room_has_link_session(const AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS) {
        return false;
    }
    const IntegralApiRoom *room = &state->current_room;
    return room->link_session_id[0] != '\0';
}

static bool room_link_session_is_active_on_server(AppState *state, const char *session_id)
{
    if (!session_id || session_id[0] == '\0' || state->login.token[0] == '\0') {
        return false;
    }
    char status[32];
    char error[160];
    int session_room_number = 0;
    int rc = integral_api_get_link_session_info(state->login.server,
                                           state->login.token,
                                           session_id,
                                           status,
                                           sizeof(status),
                                           &session_room_number,
                                           error,
                                           sizeof(error));
    if (rc != 0) {
        client_log(state, "room_session_active_check_failed", "session=%s error=%s", session_id, error);
        return false;
    }
    bool active = link_session_status_is_active(status) && session_room_number == (int)state->room_number;
    client_log(state,
               "room_session_active_check",
               "session=%s status=%s session_room=%d current_room=%u active=%d",
               session_id,
               status,
               session_room_number,
               state->room_number,
               active ? 1 : 0);
    return active;
}

static bool handle_gb_runtime_fixed_host_trade_result(AppState *state);
static void discard_gb_runtime_fixed_host_result(AppState *state);

static void monitor_room_gb_runtime_client_exit(AppState *state)
{
    if (!state->room_client_started || state->room_client_pid <= 0) {
        return;
    }
    int status = 0;
    IntegralChildProcess result = waitpid(state->room_client_pid, &status, WNOHANG);
    if (result == 0) {
        return;
    }
    if (result < 0 && errno != ECHILD) {
        client_log(state, "gb_runtime_client_wait_failed", "pid=%ld errno=%d", (long)state->room_client_pid, errno);
        return;
    }
    bool remote = strcmp(state->room_gb_runtime_fixed_host_role, "remote") == 0;
    client_log(state, "gb_runtime_fixed_host_runtime_exited",
               "role=%s wait_status=%d session=%s",
               state->room_gb_runtime_fixed_host_role, status,
               state->room_link_session_id);
    state->room_client_started = false;
    state->room_client_pid = 0;
    if (state->room_gb_runtime_fixed_host_active &&
        strcmp(state->room_gb_runtime_fixed_host_save_policy, "commit_pair") == 0 &&
        child_process_exit_code(status) == 0 &&
        handle_gb_runtime_fixed_host_trade_result(state)) {
        state->room_gb_runtime_fixed_host_active = false;
        state->room_game_ended = true;
        return;
    }
    discard_gb_runtime_fixed_host_result(state);
    if (remote && state->room_gb_runtime_fixed_host_active && !state->room_game_ended) {
        copy_text(state->login.status, sizeof(state->login.status),
                  "REMOTE RECONNECTING - 20 SEC");
        return;
    }
    state->room_gb_runtime_fixed_host_active = false;
    state->room_game_ended = true;
    (void)stop_current_game_session(state);
    copy_text(state->login.status, sizeof(state->login.status),
              "SESSION ABORTED - SAVE NOT UPDATED");
}

#ifndef _WIN32
static ptrdiff_t gb_runtime_fixed_host_fd_write(void *context, const uint8_t *source, size_t size)
{
    int descriptor = *(const int *)context;
    ssize_t amount;
    do { amount = write(descriptor, source, size); } while (amount < 0 && errno == EINTR);
    return (ptrdiff_t)amount;
}
static ptrdiff_t gb_runtime_fixed_host_fd_read(void *context, uint8_t *destination, size_t size)
{
    int descriptor = *(const int *)context;
    ssize_t amount;
    do { amount = read(descriptor, destination, size); } while (amount < 0 && errno == EINTR);
    return (ptrdiff_t)amount;
}
#else
static ptrdiff_t gb_runtime_fixed_host_handle_write(void *context, const uint8_t *source, size_t size)
{
    HANDLE handle = *(HANDLE *)context;
    DWORD amount = 0u;
    DWORD requested = size > UINT32_MAX ? UINT32_MAX : (DWORD)size;
    if (!WriteFile(handle, source, requested, &amount, NULL)) return -1;
    return (ptrdiff_t)amount;
}
static ptrdiff_t gb_runtime_fixed_host_handle_read(void *context, uint8_t *destination, size_t size)
{
    HANDLE handle = *(HANDLE *)context;
    DWORD amount = 0u;
    DWORD requested = size > UINT32_MAX ? UINT32_MAX : (DWORD)size;
    if (!ReadFile(handle, destination, requested, &amount, NULL)) return -1;
    return (ptrdiff_t)amount;
}
#endif

static int gb_runtime_fixed_host_result_reader_thread(void *opaque)
{
    AppState *state = opaque;
    bool received = false;
    if (!state || state->room_gb_runtime_fixed_host_result_read < 0) return 1;
#ifdef _WIN32
    HANDLE handle = (HANDLE)state->room_gb_runtime_fixed_host_result_read;
    received = integral_gb_runtime_fixed_host_result_ipc_receive(
        gb_runtime_fixed_host_handle_read, &handle, &state->room_gb_runtime_fixed_host_result);
    CloseHandle(handle);
#else
    int descriptor = (int)state->room_gb_runtime_fixed_host_result_read;
    received = integral_gb_runtime_fixed_host_result_ipc_receive(
        gb_runtime_fixed_host_fd_read, &descriptor, &state->room_gb_runtime_fixed_host_result);
    close(descriptor);
#endif
    state->room_gb_runtime_fixed_host_result_read = -1;
    return received ? 0 : 1;
}

static bool take_gb_runtime_fixed_host_result(AppState *state, IntegralGBRuntimeFixedHostResult *result)
{
    int thread_status = 1;
    if (!state || !result || !state->room_gb_runtime_fixed_host_result_thread) return false;
    SDL_WaitThread(state->room_gb_runtime_fixed_host_result_thread, &thread_status);
    state->room_gb_runtime_fixed_host_result_thread = NULL;
    if (thread_status != 0) {
        integral_gb_runtime_fixed_host_result_release(&state->room_gb_runtime_fixed_host_result);
        return false;
    }
    *result = state->room_gb_runtime_fixed_host_result;
    memset(&state->room_gb_runtime_fixed_host_result, 0, sizeof(state->room_gb_runtime_fixed_host_result));
    return true;
}

static void discard_gb_runtime_fixed_host_result(AppState *state)
{
    IntegralGBRuntimeFixedHostResult result = {0};
    if (!state) return;
    if (state->room_gb_runtime_fixed_host_result_thread) {
        (void)take_gb_runtime_fixed_host_result(state, &result);
        integral_gb_runtime_fixed_host_result_release(&result);
    }
    else if (state->room_gb_runtime_fixed_host_result_read >= 0) {
#ifdef _WIN32
        CloseHandle((HANDLE)state->room_gb_runtime_fixed_host_result_read);
#else
        close((int)state->room_gb_runtime_fixed_host_result_read);
#endif
        state->room_gb_runtime_fixed_host_result_read = -1;
    }
    integral_gb_runtime_fixed_host_result_release(&state->room_gb_runtime_fixed_host_result);
}

static bool handle_gb_runtime_fixed_host_trade_result(AppState *state)
{
    IntegralGBRuntimeFixedHostResult result = {0};
    char control_state[24] = {0}, error[192] = {0};
    if (!take_gb_runtime_fixed_host_result(state, &result) ||
        result.host != (strcmp(state->room_gb_runtime_fixed_host_role, "host") == 0)) {
        integral_gb_runtime_fixed_host_result_release(&result);
        return false;
    }
    int submitted = result.host
        ? integral_api_gb_runtime_fixed_host_submit_host_finish(
              state->login.server, state->login.token,
              state->room_link_session_id, state->room_game_session_id,
              state->room_fencing_token, result.final_frame,
              result.terminal_digest, result.candidates.host_data,
              result.candidates.host_size, result.candidates.remote_data,
              result.candidates.remote_size, control_state,
              sizeof(control_state), error, sizeof(error))
        : integral_api_gb_runtime_fixed_host_submit_terminal_receipt(
              state->login.server, state->login.token,
              state->room_link_session_id, result.final_frame,
              result.terminal_digest, control_state,
              sizeof(control_state), error, sizeof(error));
    integral_gb_runtime_fixed_host_result_release(&result);
    if (submitted != 0) {
        client_log(state, "gb_runtime_fixed_host_trade_finish_failed", "error=%s", error);
        return false;
    }
    for (unsigned attempt = 0u;
         strcmp(control_state, "FINISHED") != 0 && attempt < 50u; attempt++) {
        char role[16], digest[80], host_title[65], remote_title[65], build[128];
        char host_platform[4], remote_platform[4];
        char host_header[21], remote_header[21];
        unsigned pause = 0u;
        SDL_Delay(100u);
        if (integral_api_gb_runtime_fixed_host_get_manifest(
                state->login.server, state->login.token,
                state->room_link_session_id, role, sizeof(role), digest,
                sizeof(digest), host_title, sizeof(host_title), remote_title,
                sizeof(remote_title), host_platform, sizeof(host_platform),
                remote_platform, sizeof(remote_platform),
                host_header, sizeof(host_header), remote_header, sizeof(remote_header),
                build, sizeof(build), control_state,
                sizeof(control_state), &pause, error, sizeof(error)) != 0) break;
    }
    if (strcmp(control_state, "FINISHED") != 0) {
        client_log(state, "gb_runtime_fixed_host_trade_commit_pending", "state=%s", control_state);
        copy_text(state->login.status, sizeof(state->login.status),
                  "TRADE FINALIZING - CHECK SERVER");
        return true;
    }
    client_log(state, "gb_runtime_fixed_host_trade_committed", "session=%s",
               state->room_link_session_id);
    copy_text(state->login.status, sizeof(state->login.status), "TRADE SAVED - BOTH PLAYERS");
    return true;
}

static const char *gb_runtime_fixed_host_key_spec_for_role(
    const char *role, const IntegralConfigKeys *keys)
{
    if (!role || !keys) return "";
    return strcmp(role, "host") == 0 ? keys->slot1 : keys->slot2;
}

static void gb_runtime_fixed_host_set_child_environment(
    const char *role, const char *relay_host, unsigned relay_port,
    const char *session_id, const char *ticket,
    const IntegralGBRuntimeFixedHostRomResolution *resolution,
    const IntegralConfigKeys *keys,
    intptr_t result_handle, const char *save_policy)
{
    char port[16];
    char result_handle_text[32];
    const char *game_keys = gb_runtime_fixed_host_key_spec_for_role(role, keys);
    snprintf(port, sizeof(port), "%u", relay_port);
    snprintf(result_handle_text, sizeof(result_handle_text), "%lld",
             (long long)result_handle);
#ifdef _WIN32
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE", role);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST", relay_host);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT", port);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION", session_id);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET", ticket);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA", integral_gb_runtime_fixed_host_ca_file());
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1",
                            resolution ? resolution->path_a : NULL);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2",
                            resolution ? resolution->path_b : NULL);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS", game_keys);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE", result_handle_text);
    SetEnvironmentVariableA("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY", save_policy);
#else
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE", role, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST", relay_host, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT", port, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION", session_id, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET", ticket, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA", integral_gb_runtime_fixed_host_ca_file(), 1);
    if (resolution) {
        setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1", resolution->path_a, 1);
        setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2", resolution->path_b, 1);
    }
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS", game_keys, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE", result_handle_text, 1);
    setenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY", save_policy, 1);
#endif
}

#ifdef _WIN32
static void gb_runtime_fixed_host_clear_parent_environment(void)
{
    static const char *names[] = {
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE",
        "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY",
    };
    for (size_t index = 0u; index < sizeof(names) / sizeof(names[0]); index++)
        SetEnvironmentVariableA(names[index], NULL);
}
#endif

static bool start_room_gb_runtime_fixed_host_runtime(
    AppState *state, const char *role, const IntegralGBRuntimeFixedHostRomResolution *resolution)
{
    char relay_host[128] = {0}, ticket_role[16] = {0}, scope[48] = {0};
    char ticket[192] = {0}, error[192] = {0};
    char save_policy[32] = {0};
    unsigned relay_port = 0u;
    IntegralGBRuntimeFixedHostSnapshotPair snapshots = {0};
    bool host = strcmp(role, "host") == 0;
    if (state->room_client_started) return true;
    if (access(integral_gb_runtime_fixed_host_runtime_path(), X_OK) != 0) {
        copy_text(state->login.status, sizeof(state->login.status),
                  "FIXED HOST RUNTIME NOT FOUND");
        return false;
    }
    if (host) {
        snapshots.host_data = malloc(INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX);
        snapshots.remote_data = malloc(INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX);
        if (!snapshots.host_data || !snapshots.remote_data ||
            integral_api_gb_runtime_fixed_host_download_snapshots(
                state->login.server, state->login.token,
                state->room_link_session_id,
                snapshots.host_data, INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX,
                &snapshots.host_size,
                snapshots.remote_data, INTEGRAL_GB_RUNTIME_FIXED_HOST_SNAPSHOT_MAX,
                &snapshots.remote_size, error, sizeof(error)) != 0) {
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            snprintf(state->login.status, sizeof(state->login.status),
                     "FIXED HOST SNAPSHOT FAILED %.96s", error);
            return false;
        }
    }
    if (integral_api_gb_runtime_fixed_host_issue_relay_ticket(
            state->login.server, state->login.token,
            state->room_link_session_id, relay_host, sizeof(relay_host),
            &relay_port, ticket_role, sizeof(ticket_role), scope, sizeof(scope),
            ticket, sizeof(ticket),
            save_policy, sizeof(save_policy),
            error, sizeof(error)) != 0 ||
        strcmp(ticket_role, role) != 0 ||
        strcmp(scope, "gb-runtime-fixed-host-media-v1") != 0) {
        integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
        snprintf(state->login.status, sizeof(state->login.status),
                 "FIXED HOST TICKET FAILED %.96s", error);
        memset(ticket, 0, sizeof(ticket));
        return false;
    }
#ifdef _WIN32
    {
        SECURITY_ATTRIBUTES security = {sizeof(security), NULL, TRUE};
        HANDLE child_input = NULL, parent_write = NULL;
        HANDLE parent_result_read = NULL, child_result_write = NULL;
        STARTUPINFOA startup;
        PROCESS_INFORMATION process;
        char command[INTEGRAL_CONFIG_PATH_MAX + 64];
        bool created;
        memset(&startup, 0, sizeof(startup));
        memset(&process, 0, sizeof(process));
        startup.cb = sizeof(startup);
        if (!CreatePipe(&parent_result_read, &child_result_write, &security, 0u) ||
            !SetHandleInformation(parent_result_read, HANDLE_FLAG_INHERIT, 0u) ||
            (host && (!CreatePipe(&child_input, &parent_write, &security, 0u) ||
                     !SetHandleInformation(parent_write, HANDLE_FLAG_INHERIT, 0u)))) {
            if (parent_result_read) CloseHandle(parent_result_read);
            if (child_result_write) CloseHandle(child_result_write);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        if (host) {
            startup.dwFlags = STARTF_USESTDHANDLES;
            startup.hStdInput = child_input;
            startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
            startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        }
        gb_runtime_fixed_host_set_child_environment(role, relay_host, relay_port,
                                         state->room_link_session_id, ticket,
                                         resolution, &state->keys,
                                         (intptr_t)child_result_write, save_policy);
        snprintf(command, sizeof(command), "\"%s\"%s",
                 integral_gb_runtime_fixed_host_runtime_path(),
                 host ? " --snapshot-stdin" : "");
        created = CreateProcessA(integral_gb_runtime_fixed_host_runtime_path(), command,
                                 NULL, NULL, TRUE, 0u, NULL, NULL,
                                 &startup, &process) != 0;
        gb_runtime_fixed_host_clear_parent_environment();
        if (child_input) CloseHandle(child_input);
        if (child_result_write) CloseHandle(child_result_write);
        if (!created || (host && !integral_gb_runtime_fixed_host_snapshot_ipc_send(
                gb_runtime_fixed_host_handle_write, &parent_write, &snapshots))) {
            if (created) { TerminateProcess(process.hProcess, 1u); CloseHandle(process.hProcess); }
            if (process.hThread) CloseHandle(process.hThread);
            if (parent_write) CloseHandle(parent_write);
            if (parent_result_read) CloseHandle(parent_result_read);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        if (parent_write) CloseHandle(parent_write);
        CloseHandle(process.hThread);
        state->room_client_pid = (IntegralChildProcess)(intptr_t)process.hProcess;
        state->room_gb_runtime_fixed_host_result_read = (intptr_t)parent_result_read;
    }
#else
    {
        int descriptors[2] = {-1, -1};
        int result_descriptors[2] = {-1, -1};
        pid_t child;
        if (pipe(result_descriptors) != 0 || (host && pipe(descriptors) != 0)) {
            if (result_descriptors[0] >= 0) close(result_descriptors[0]);
            if (result_descriptors[1] >= 0) close(result_descriptors[1]);
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        child = fork();
        if (child == 0) {
            close(result_descriptors[0]);
            if (host) {
                close(descriptors[1]);
                dup2(descriptors[0], STDIN_FILENO);
                close(descriptors[0]);
            }
            gb_runtime_fixed_host_set_child_environment(role, relay_host, relay_port,
                                             state->room_link_session_id, ticket,
                                             resolution, &state->keys,
                                             (intptr_t)result_descriptors[1], save_policy);
            redirect_child_output_to_client_log();
            if (host)
                execl(integral_gb_runtime_fixed_host_runtime_path(),
                      integral_gb_runtime_fixed_host_runtime_path(),
                      "--snapshot-stdin", (char *)NULL);
            else
                execl(integral_gb_runtime_fixed_host_runtime_path(),
                      integral_gb_runtime_fixed_host_runtime_path(), (char *)NULL);
            _exit(127);
        }
        if (child < 0) {
            close(result_descriptors[0]); close(result_descriptors[1]);
            if (host) { close(descriptors[0]); close(descriptors[1]); }
            integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
            memset(ticket, 0, sizeof(ticket));
            return false;
        }
        close(result_descriptors[1]);
        if (host) {
            close(descriptors[0]);
            if (!integral_gb_runtime_fixed_host_snapshot_ipc_send(
                    gb_runtime_fixed_host_fd_write, &descriptors[1], &snapshots)) {
                close(descriptors[1]);
                kill(child, SIGTERM);
                (void)waitpid(child, NULL, 0);
                memset(ticket, 0, sizeof(ticket));
                return false;
            }
            close(descriptors[1]);
        }
        state->room_client_pid = child;
        state->room_gb_runtime_fixed_host_result_read = result_descriptors[0];
    }
#endif
    memset(ticket, 0, sizeof(ticket));
    memset(&state->room_gb_runtime_fixed_host_result, 0, sizeof(state->room_gb_runtime_fixed_host_result));
    state->room_gb_runtime_fixed_host_result_thread = SDL_CreateThread(
        gb_runtime_fixed_host_result_reader_thread, "gb-runtime-fixed-host-result", state);
    if (!state->room_gb_runtime_fixed_host_result_thread) {
#ifdef _WIN32
        TerminateProcess((HANDLE)state->room_client_pid, 1u);
        WaitForSingleObject((HANDLE)state->room_client_pid, 5000u);
        CloseHandle((HANDLE)state->room_client_pid);
#else
        kill(state->room_client_pid, SIGTERM);
        (void)waitpid(state->room_client_pid, NULL, 0);
#endif
        discard_gb_runtime_fixed_host_result(state);
        state->room_client_pid = 0;
        copy_text(state->login.status, sizeof(state->login.status),
                  "FIXED HOST RESULT READER FAILED");
        return false;
    }
    state->room_client_started = true;
    state->room_gb_runtime_fixed_host_active = true;
    copy_text(state->room_gb_runtime_fixed_host_role, sizeof(state->room_gb_runtime_fixed_host_role), role);
    copy_text(state->room_gb_runtime_fixed_host_save_policy,
              sizeof(state->room_gb_runtime_fixed_host_save_policy), save_policy);
    copy_text(state->login.status, sizeof(state->login.status),
              host ? "FIXED HOST BATTLE RUNNING" : "FIXED HOST REMOTE RUNNING");
    client_log(state, "gb_runtime_fixed_host_runtime_started", "role=%s session=%s",
               role, state->room_link_session_id);
    return true;
}

static void maybe_advance_room_gb_runtime_fixed_host_preflight(AppState *state)
{
    char role[16] = {0};
    char digest[80] = {0};
    char host_game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX] = {0};
    char remote_game_type[INTEGRAL_API_ROOM_GAME_TYPE_MAX] = {0};
    char host_platform[INTEGRAL_API_ROM_PLATFORM_MAX] = {0};
    char remote_platform[INTEGRAL_API_ROM_PLATFORM_MAX] = {0};
    char host_header_title[INTEGRAL_API_ROM_HEADER_TITLE_MAX] = {0};
    char remote_header_title[INTEGRAL_API_ROM_HEADER_TITLE_MAX] = {0};
    char runtime_build_id[128] = {0};
    char control_state[24] = {0};
    unsigned pause_remaining_seconds = 0u;
    char error[160] = {0};
    IntegralGBRuntimeFixedHostRomResolution resolution;
    IntegralGBRuntimeFixedHostRomResolveStatus rom_status;
    if (integral_api_gb_runtime_fixed_host_get_manifest(
            state->login.server, state->login.token,
            state->room_link_session_id, role, sizeof(role), digest,
            sizeof(digest), host_game_type, sizeof(host_game_type), remote_game_type,
            sizeof(remote_game_type), host_platform, sizeof(host_platform),
            remote_platform, sizeof(remote_platform),
            host_header_title, sizeof(host_header_title),
            remote_header_title, sizeof(remote_header_title),
            runtime_build_id, sizeof(runtime_build_id),
            control_state, sizeof(control_state),
            &pause_remaining_seconds,
            error, sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status),
                 "FIXED HOST MANIFEST FAILED %.96s", error);
        return;
    }
    if (strcmp(role, "host") == 0) {
        rom_status = integral_gb_runtime_fixed_host_rom_resolve_local(
            state->rom_slots, state->server_rom_slots, INTEGRAL_ROM_SLOTS,
            host_game_type, host_platform, host_header_title,
            remote_game_type, remote_platform, remote_header_title,
            &resolution);
    }
    else if (strcmp(role, "remote") == 0) {
        memset(&resolution, 0, sizeof(resolution));
        rom_status = INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK;
    }
    else {
        copy_text(state->login.status, sizeof(state->login.status),
                  "FIXED HOST ROLE INVALID");
        return;
    }
    if (rom_status != INTEGRAL_GB_RUNTIME_FIXED_HOST_ROM_RESOLVE_OK) {
        snprintf(state->login.status, sizeof(state->login.status),
                 "FIXED HOST ROM CHECK %.96s",
                 integral_gb_runtime_fixed_host_rom_resolve_status_text(rom_status));
        return;
    }
    if (strcmp(runtime_build_id, INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID) != 0) {
        copy_text(state->login.status, sizeof(state->login.status),
                  "FIXED HOST CLIENT UPDATE REQUIRED");
        client_log(state, "gb_runtime_fixed_host_runtime_build_mismatch",
                   "server=%s client=%s", runtime_build_id,
                   INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID);
        return;
    }
    if (strcmp(control_state, "PREFLIGHT") == 0 ||
        strcmp(control_state, "READY") == 0) {
        if (integral_api_gb_runtime_fixed_host_submit_preflight(
                state->login.server, state->login.token,
                state->room_link_session_id, digest,
                INTEGRAL_GB_RUNTIME_FIXED_HOST_RUNTIME_BUILD_ID,
                strcmp(role, "host") == 0 ? host_game_type : "",
                strcmp(role, "host") == 0 ? host_platform : "",
                strcmp(role, "host") == 0 ? host_header_title : "",
                strcmp(role, "host") == 0 ? remote_game_type : "",
                strcmp(role, "host") == 0 ? remote_platform : "",
                strcmp(role, "host") == 0 ? remote_header_title : "",
                control_state, sizeof(control_state),
                error, sizeof(error)) != 0) {
            snprintf(state->login.status, sizeof(state->login.status),
                     "FIXED HOST PREFLIGHT FAILED %.96s", error);
            return;
        }
    }
    if (strcmp(control_state, "READY") == 0 ||
        strcmp(control_state, "WAITING_PEER") == 0 ||
        (strcmp(control_state, "PAUSED_REMOTE") == 0 &&
         strcmp(role, "remote") == 0)) {
        if (integral_api_get_link_game_fence(
                state->login.server, state->login.token,
                state->room_link_session_id,
                state->room_game_session_id,
                sizeof(state->room_game_session_id),
                &state->room_fencing_token,
                error, sizeof(error)) != 0) {
            snprintf(state->login.status, sizeof(state->login.status),
                     "FIXED HOST FENCE FAILED %.96s", error);
            client_log(state, "gb_runtime_fixed_host_fence_failed",
                       "session=%s error=%s", state->room_link_session_id,
                       error);
            return;
        }
        (void)start_room_gb_runtime_fixed_host_runtime(
            state, role, strcmp(role, "host") == 0 ? &resolution : NULL);
        return;
    }
    if (strcmp(control_state, "PAUSED_REMOTE") == 0) {
        snprintf(state->login.status, sizeof(state->login.status),
                 "REMOTE RECONNECTING - %u SEC", pause_remaining_seconds);
        return;
    }
    if (strcmp(control_state, "RUNNING") == 0 && state->room_client_started) {
        return;
    }
    copy_text(state->login.status, sizeof(state->login.status),
              "FIXED HOST WAITING PEER");
}

static void maybe_launch_room_host(AppState *state)
{
    if (state->room_link_session_id[0] == '\0' || state->login.token[0] == '\0') {
        client_log(state,
                   "room_launch_skip",
                   "reason=missing_session_or_token session=%s token=%s",
                   state->room_link_session_id[0] ? state->room_link_session_id : "-",
                   state->login.token[0] ? "present" : "missing");
        return;
    }
    {
        char protocol_id[32];
        char protocol_error[160];
        if (integral_api_get_link_session_protocol(
                state->login.server, state->login.token,
                state->room_link_session_id, protocol_id,
                sizeof(protocol_id), protocol_error,
                sizeof(protocol_error)) != 0) {
            snprintf(state->login.status, sizeof(state->login.status),
                     "PROTOCOL CHECK FAILED %s", protocol_error);
            client_log(state, "room_protocol_check_failed",
                       "session=%s error=%s",
                       state->room_link_session_id, protocol_error);
            return;
        }
        if (strcmp(protocol_id, "gb_runtime_fixed_host_v1") != 0) {
            copy_text(state->login.status, sizeof(state->login.status),
                      "CLIENT CAPABILITY MISMATCH");
            client_log(state, "room_protocol_rejected",
                       "session=%s protocol=%s",
                       state->room_link_session_id, protocol_id);
            return;
        }
        maybe_advance_room_gb_runtime_fixed_host_preflight(state);
        return;
    }
}

static void maybe_start_room_session(AppState *state)
{
    if (state->room_number < 1 || state->room_number > INTEGRAL_API_ROOMS || state->login.token[0] == '\0') {
        return;
    }
    if (state->room_game_ended) {
        return;
    }
    if (state->room_link_session_id[0] != '\0') {
        if (!room_link_session_is_active_on_server(state, state->room_link_session_id)) {
            client_log(state, "room_local_session_stale_before_retry", "session=%s", state->room_link_session_id);
            clear_room_link_session_id(state);
        }
        else {
            client_log(state,
                       "room_local_session_retry",
                       "session=%s ready_self=%d ready_peer=%d status=%s",
                       state->room_link_session_id,
                       state->room_ready_self ? 1 : 0,
                       state->room_ready_peer ? 1 : 0,
                       state->login.status);
            maybe_launch_room_host(state);
            return;
        }
    }
    const IntegralApiRoom *room = &state->current_room;
    if (room->link_session_id[0] != '\0') {
        if (!room_link_session_is_active_on_server(state, room->link_session_id)) {
            client_log(state, "room_server_session_stale_before_reuse", "session=%s", room->link_session_id);
        }
        else {
            client_log(state,
                       "room_existing_session_seen",
                       "session=%s ready_self=%d ready_peer=%d",
                       room->link_session_id,
                       state->room_ready_self ? 1 : 0,
                       state->room_ready_peer ? 1 : 0);
            set_room_link_session_id(state, room->link_session_id);
            maybe_launch_room_host(state);
            if (state->login.status[0] == '\0') {
                copy_text(state->login.status, sizeof(state->login.status), "LINK SESSION READY");
            }
            return;
        }
    }
    if (!current_room_is_ready_to_start(state) || state->room_start_requested) {
        client_log(state,
                   "room_start_skip",
                   "ready_to_start=%d start_requested=%d ready_self=%d ready_peer=%d",
                   current_room_is_ready_to_start(state) ? 1 : 0,
                   state->room_start_requested ? 1 : 0,
                   state->room_ready_self ? 1 : 0,
                   state->room_ready_peer ? 1 : 0);
        return;
    }

    state->room_start_requested = true;
    client_log(state, "room_start_request", "room=%u", state->room_number);
    char session_id[INTEGRAL_API_ROOM_SESSION_MAX];
    char error[160];
    if (integral_api_start_room(state->login.server,
                                 state->login.token,
                                 state->room_number,
                                 room_link_mode_api_name(state->room_link_mode),
                                 session_id,
                                 sizeof(session_id),
                                 error,
                                 sizeof(error)) != 0) {
        state->room_start_requested = false;
        snprintf(state->login.status, sizeof(state->login.status), "ROOM START FAILED %s", error);
        client_log(state, "room_start_failed", "room=%u error=%s", state->room_number, error);
        return;
    }
    client_log(state, "room_start_ok", "room=%u session=%s", state->room_number, session_id);
    set_room_link_session_id(state, session_id);
    maybe_launch_room_host(state);
    refresh_room_quiet(state);
    if (state->login.status[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LINK SESSION READY");
    }
}

static void activate_link_room(AppState *state, const IntegralApiRoom *matched_room)
{
    IntegralApiRoom room_copy = *matched_room;
    unsigned room_number = room_copy.room_number;
    IntegralRoomLinkMode server_mode;
    if (!room_link_mode_from_api(room_copy.link_mode, &server_mode)) {
        copy_text(state->login.status, sizeof(state->login.status), "ROOM MODE INVALID");
        client_log(state, "room_mode_invalid", "value=%s", room_copy.link_mode);
        return;
    }
    memset(&state->current_room, 0, sizeof(state->current_room));
    state->current_room = room_copy;
    state->screen = SCREEN_ROOM;
    state->room_number = room_number;
    state->room_selected = 0;
    state->room_chat_editing = false;
    state->room_chat_scroll = 0;
    state->room_chat_input[0] = '\0';
    state->room_chat_composition[0] = '\0';
    memset(state->room_chat_log, 0, sizeof(state->room_chat_log));
    state->room_link_mode = server_mode;
    state->room_link_mode_local_override = false;
    state->room_slot_index = registered_rom_slot_at(state, state->local_slot_indices[0]) ? state->local_slot_indices[0] : -1;
    state->room_ready_self = false;
    state->room_ready_peer = false;
    state->room_heartbeat_failures = 0;
    state->room_heartbeat_attempted = false;
    state->room_lifecycle_status[0] = '\0';
    state->room_termination_reason[0] = '\0';
    state->room_remaining_seconds = -1;
    state->room_link_session_id[0] = '\0';
    state->room_game_session_id[0] = '\0';
    state->room_fencing_token = 0;
    state->room_start_requested = false;
    state->room_client_started = false;
    state->room_game_ended = false;
    state->room_link_mode_local_override = false;
    state->room_client_pid = 0;
    state->room_session_missing_since_ticks = 0;
    sync_room_state(state);
    mark_room_game_ended_if_used(state);
    if (!current_room_game_ended(state) && strncmp(state->login.status, "ROOM SYNC FAILED", 16) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "ENTERED ROOM");
    }
    client_log(state,
               "room_activate",
               "room=%u slot_index=%d status=%s",
               room_number,
               state->room_slot_index,
               state->login.status);
}

static void activate_n64_room(AppState *state, const IntegralApiRoom *matched_room)
{
    IntegralApiRoom room_copy = *matched_room;
    state->room_number = room_copy.room_number;
    memset(&state->current_room, 0, sizeof(state->current_room));
    state->current_room = room_copy;
    state->room_selected = 0;
    state->n64_room_ready = false;
    state->room_heartbeat_failures = 0;
    state->room_heartbeat_attempted = false;
    state->room_chat_editing = false;
    state->room_chat_scroll = 0;
    state->room_chat_input[0] = '\0';
    state->room_chat_composition[0] = '\0';
    memset(state->room_chat_log, 0, sizeof(state->room_chat_log));
    reset_n64_runtime_media_connection(state);
    init_n64_room_selection(state);
    state->screen = SCREEN_N64_ROOM;
    refresh_room_quiet(state);
    if (sync_n64_room_state(state, false)) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 ROOM SELECTION SYNCED");
    }
    client_log(state, "room_activate", "room=%u mode=n64", state->room_number);
}

static void activate_matched_room(AppState *state, const IntegralApiRoom *matched_room)
{
    if (!matched_room || matched_room->room_number < 1 ||
        matched_room->room_number > INTEGRAL_API_ROOMS) return;
    if (strcmp(matched_room->room_type, "n64") == 0) activate_n64_room(state, matched_room);
    else activate_link_room(state, matched_room);
}

static void update_room_ready_status(AppState *state)
{
    if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
        return;
    }
    if (current_room_has_link_session(state)) {
        maybe_start_room_session(state);
        return;
    }
    if (current_room_is_ready_to_start(state)) {
        maybe_start_room_session(state);
        if (state->room_link_session_id[0] == '\0' && state->room_start_requested) {
            copy_text(state->login.status, sizeof(state->login.status), "BOTH READY  SERVER START PENDING");
        }
        return;
    }
    if (state->room_ready_self && state->room_ready_peer) {
        copy_text(state->login.status, sizeof(state->login.status), "BOTH READY  EMULATOR START");
    }
    else if (state->room_ready_self) {
        copy_text(state->login.status, sizeof(state->login.status), "READY  WAITING USER2");
    }
    else {
        copy_text(state->login.status, sizeof(state->login.status), "ROOM");
    }
}

static bool stop_room_client(AppState *state)
{
    if (!state->room_client_started || state->room_client_pid <= 0) {
        return true;
    }

    client_log(state, "room_client_stop_request", "pid=%ld session=%s", (long)state->room_client_pid, state->room_link_session_id);
#ifdef _WIN32
    HANDLE process = (HANDLE)(intptr_t)state->room_client_pid;
    DWORD wait_result = WaitForSingleObject(process, 5000);
    if (wait_result == WAIT_TIMEOUT) {
        TerminateProcess(process, 1);
        wait_result = WaitForSingleObject(process, 5000);
    }
    if (wait_result != WAIT_OBJECT_0) {
        client_log(state, "room_client_stop_failed", "pid=%ld", (long)state->room_client_pid);
        copy_text(state->login.status, sizeof(state->login.status), "GAME STOP FAILED");
        return false;
    }
    CloseHandle(process);
#else
    kill(state->room_client_pid, SIGTERM);
    bool exited = false;
    for (unsigned attempt = 0; attempt < 50; attempt++) {
        int status = 0;
        IntegralChildProcess result = waitpid(state->room_client_pid, &status, WNOHANG);
        if (result == state->room_client_pid || (result < 0 && errno == ECHILD)) {
            exited = true;
            break;
        }
        SDL_Delay(100);
    }
    if (!exited) {
        client_log(state, "room_client_stop_timeout", "pid=%ld", (long)state->room_client_pid);
        copy_text(state->login.status, sizeof(state->login.status), "GAME STOP TIMEOUT");
        return false;
    }
#endif
    state->room_client_started = false;
    state->room_client_pid = 0;
    state->room_game_ended = true;
    if (state->room_gb_runtime_fixed_host_active) {
        state->room_gb_runtime_fixed_host_active = false;
        discard_gb_runtime_fixed_host_result(state);
        (void)stop_current_game_session(state);
        copy_text(state->login.status, sizeof(state->login.status),
                  "SESSION ABORTED - SAVE NOT UPDATED");
        return true;
    }
    client_log(state, "room_client_stop_unexpected_runtime",
               "session=%s save=not_updated", state->room_link_session_id);
    (void)stop_current_game_session(state);
    copy_text(state->login.status, sizeof(state->login.status),
              "SESSION ABORTED - SAVE NOT UPDATED");
    return true;
}

static bool stop_current_game_session(AppState *state)
{
    if (state->login.token[0] == '\0' || state->room_link_session_id[0] == '\0') {
        return true;
    }
    char error[160];
    if (integral_api_stop_game(state->login.server,
                          state->login.token,
                          state->room_game_session_id,
                          state->room_fencing_token,
                          error,
                          sizeof(error)) != 0) {
        if (strstr(error, "active game session not found") != NULL ||
            strstr(error, "link session is already ended") != NULL) {
            client_log(state, "game_stop_already_done", "session=%s message=%s", state->room_link_session_id, error);
            return true;
        }
        client_log(state, "game_stop_failed", "session=%s error=%s", state->room_link_session_id, error);
        snprintf(state->login.status, sizeof(state->login.status), "GAME STOP API FAILED %s", error);
        return false;
    }
    client_log(state, "game_stop_ok", "session=%s", state->room_link_session_id);
    return true;
}

static void leave_current_room(AppState *state, bool update_status)
{
    reset_n64_runtime_media_connection(state);
    if (state->login.token[0] == '\0') {
        return;
    }
    if (state->room_client_started && state->room_link_session_id[0] != '\0') {
        if (!stop_room_client(state)) {
            client_log(state, "room_leave_blocked", "reason=client_stop_failed session=%s", state->room_link_session_id);
            return;
        }
        if (!stop_current_game_session(state)) {
            client_log(state, "room_leave_blocked", "reason=game_stop_failed session=%s", state->room_link_session_id);
            return;
        }
        client_log(state, "room_leave_local_after_client_stop", "session=%s", state->room_link_session_id);
        if (update_status) {
            copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED");
        }
        return;
    }
    char error[160];
    client_log(state, "room_leave_request", "room=%u session=%s", state->room_number, state->room_link_session_id);
    if (integral_api_leave_room(state->login.server, state->login.token, error, sizeof(error)) != 0) {
        if (update_status) {
            snprintf(state->login.status, sizeof(state->login.status), "ROOM LEAVE FAILED %s", error);
        }
        client_log(state, "room_leave_failed", "room=%u error=%s", state->room_number, error);
        return;
    }
    client_log(state, "room_leave_ok", "room=%u", state->room_number);
    state->room_number = 0;
    state->room_ready_self = false;
    state->room_ready_peer = false;
    state->room_link_session_id[0] = '\0';
    state->room_game_session_id[0] = '\0';
    state->room_fencing_token = 0;
    state->room_start_requested = false;
    state->room_client_started = false;
    state->room_game_ended = false;
    state->room_client_pid = 0;
    state->room_session_missing_since_ticks = 0;
    state->room_chat_editing = false;
    state->room_chat_scroll = 0;
    state->room_chat_input[0] = '\0';
    state->room_chat_composition[0] = '\0';
    memset(state->room_chat_log, 0, sizeof(state->room_chat_log));
    if (update_status) {
        copy_text(state->login.status, sizeof(state->login.status), "LEFT ROOM");
    }
}

static bool lifecycle_status_stops_emulator(const char *status)
{
    return status && (
        strcmp(status, "FINALIZING") == 0 ||
        strcmp(status, "RECOVERING") == 0 ||
        strcmp(status, "COMPLETED") == 0 ||
        strcmp(status, "FAILED") == 0 ||
        strcmp(status, "CANCELLED") == 0 ||
        strcmp(status, "EXPIRED") == 0
    );
}

static void handle_room_heartbeat_result(AppState *state,
                                          const IntegralApiHeartbeatStatus *heartbeat,
                                          bool succeeded,
                                          const char *error)
{
    if (!succeeded) {
        bool authoritative = error &&
            (strstr(error, "401") || strstr(error, "403") ||
             strstr(error, "fence") || strstr(error, "session mismatch"));
        state->room_heartbeat_failures = authoritative
                                                ? 3u
                                                : state->room_heartbeat_failures + 1u;
        client_log(state,
                   "room_heartbeat_failed",
                   "failures=%u error=%s",
                   state->room_heartbeat_failures,
                   error && error[0] ? error : "unknown");
        if (state->room_heartbeat_failures < 3) {
            return;
        }
        if (state->screen == SCREEN_N64_ROOM) {
            reset_n64_runtime_media_connection(state);
        }
        else if (state->screen == SCREEN_ROOM && state->room_client_started) {
            (void)stop_room_client(state);
        }
        state->room_game_ended = true;
        copy_text(state->login.status,
                  sizeof(state->login.status),
                  "SESSION LOST  EMULATOR STOPPED");
        return;
    }

    state->room_heartbeat_failures = 0;
    if (!heartbeat) {
        return;
    }
    if (strcmp(heartbeat->lifecycle_kind, "link") == 0 &&
        (heartbeat->lifecycle_session_id[0] == '\0' ||
         state->room_link_session_id[0] == '\0' ||
         strcmp(heartbeat->lifecycle_session_id,
                state->room_link_session_id) != 0)) {
        client_log(state,
                   "room_stale_lifecycle_ignored",
                   "notice_session=%s current_session=%s status=%s",
                   heartbeat->lifecycle_session_id[0]
                       ? heartbeat->lifecycle_session_id : "-",
                   state->room_link_session_id[0]
                       ? state->room_link_session_id : "-",
                   heartbeat->lifecycle_status[0]
                       ? heartbeat->lifecycle_status : "-");
        return;
    }
    if (strcmp(heartbeat->lifecycle_kind, "media") == 0 &&
        (heartbeat->lifecycle_session_id[0] == '\0' ||
         state->n64_runtime_media_session_id[0] == '\0' ||
         strcmp(heartbeat->lifecycle_session_id,
                state->n64_runtime_media_session_id) != 0)) {
        client_log(state,
                   "room_stale_lifecycle_ignored",
                   "notice_session=%s current_session=%s status=%s",
                   heartbeat->lifecycle_session_id[0]
                       ? heartbeat->lifecycle_session_id : "-",
                   state->n64_runtime_media_session_id[0]
                       ? state->n64_runtime_media_session_id : "-",
                   heartbeat->lifecycle_status[0]
                       ? heartbeat->lifecycle_status : "-");
        return;
    }
    copy_text(state->room_lifecycle_status,
              sizeof(state->room_lifecycle_status),
              heartbeat->lifecycle_status);
    copy_text(state->room_termination_reason,
              sizeof(state->room_termination_reason),
              heartbeat->termination_reason);
    long long expires_unix = 0;
    state->room_remaining_seconds =
        heartbeat->server_unix_time > 0 &&
        parse_iso8601_unix(heartbeat->expires_at, &expires_unix)
            ? (expires_unix > heartbeat->server_unix_time
                   ? expires_unix - heartbeat->server_unix_time
                   : 0)
            : -1;
    if (!lifecycle_status_stops_emulator(heartbeat->lifecycle_status)) {
        return;
    }
    if (state->screen != SCREEN_ROOM && state->screen != SCREEN_N64_ROOM) {
        return;
    }

    if (strcmp(heartbeat->lifecycle_kind, "room") == 0) {
        reset_n64_runtime_media_connection(state);
        if (state->room_client_started) {
            (void)stop_room_client(state);
        }
        state->room_number = 0;
        state->room_ready_self = false;
        state->room_ready_peer = false;
        state->room_game_ended = true;
        state->screen = SCREEN_MAIN_MENU;
        snprintf(state->login.status,
                 sizeof(state->login.status),
                 "ROOM EXPIRED  %s",
                 heartbeat->termination_reason[0]
                     ? heartbeat->termination_reason
                     : "IDLE TIMEOUT");
        return;
    }

    client_log(state,
               "room_session_terminal",
               "status=%s reason=%s",
               heartbeat->lifecycle_status,
               heartbeat->termination_reason[0] ? heartbeat->termination_reason : "-");
    if (state->screen == SCREEN_N64_ROOM) {
        reset_n64_runtime_media_connection(state);
    }
    else if (state->screen == SCREEN_ROOM && state->room_client_started) {
        (void)stop_room_client(state);
    }
    state->room_game_ended = true;
    snprintf(state->login.status,
             sizeof(state->login.status),
             "SESSION %s  %s",
             heartbeat->lifecycle_status,
             heartbeat->termination_reason[0] ? heartbeat->termination_reason : "SERVER END");
}

static void send_room_heartbeat(AppState *state, Uint32 now)
{
    if (state->login.token[0] == '\0') {
        return;
    }
    if (state->screen != SCREEN_ROOM && state->screen != SCREEN_N64_ROOM) {
        return;
    }
    Uint32 heartbeat_interval = (state->screen == SCREEN_ROOM || state->screen == SCREEN_N64_ROOM)
                                    ? INTEGRAL_ROOM_HEARTBEAT_MS
                                    : INTEGRAL_LINK_ROOM_HEARTBEAT_MS;
    if (!integral_room_heartbeat_attempt_due(state->last_room_heartbeat_attempt_ticks,
                                              state->room_heartbeat_attempted,
                                              now,
                                              heartbeat_interval)) {
        return;
    }
    state->last_room_heartbeat_attempt_ticks = now;
    state->room_heartbeat_attempted = true;
    char error[160];
    IntegralApiHeartbeatStatus heartbeat;
    bool succeeded = integral_api_room_heartbeat_status(
                         state->login.server,
                         state->login.token,
                         &heartbeat,
                         error,
                         sizeof(error)) == 0;
    if (succeeded) {
        state->last_room_heartbeat_ticks = now;
    }
    handle_room_heartbeat_result(state, &heartbeat, succeeded, error);
}

static void move_room_selection(AppState *state, int delta)
{
    int selected = (int)state->room_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_ROOM_ROWS - 1;
    }
    if (selected >= INTEGRAL_ROOM_ROWS) {
        selected = 0;
    }
    state->room_selected = (unsigned)selected;
    if (state->room_selected != 4 && state->room_chat_editing) {
        state->room_chat_editing = false;
        state->room_chat_composition[0] = '\0';
        SDL_StopTextInput();
    }
    if (state->room_selected == 4) {
        copy_text(state->login.status, sizeof(state->login.status), "ENTER CHAT INPUT");
    }
}

static void handle_room_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            if (state->room_chat_editing) {
                state->room_chat_editing = false;
                state->room_chat_composition[0] = '\0';
                SDL_StopTextInput();
                copy_text(state->login.status, sizeof(state->login.status), "CHAT INPUT SAVED");
            }
            else {
                leave_current_room(state, true);
                state->screen = SCREEN_MAIN_MENU;
                copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            }
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_room_selection(state, 1);
            break;
        case SDLK_UP:
            move_room_selection(state, -1);
            break;
        case SDLK_RIGHT:
            if (state->room_selected == 0) {
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    client_log(state, "room_slot_blocked", "reason=game_ended direction=right");
                    break;
                }
                cycle_room_rom_slot(state, 1);
                client_log(state, "room_slot_changed", "direction=right slot_index=%d", state->room_slot_index);
                sync_room_state(state);
            }
            else if (state->room_selected == 1 || state->room_selected == 2) {
                if (!current_room_is_user1(state)) {
                    copy_text(state->login.status, sizeof(state->login.status), "MODE SELECTED BY USER1");
                    client_log(state, "room_mode_blocked", "reason=user2 direction=right");
                    break;
                }
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    break;
                }
                if (state->room_link_session_id[0] != '\0') {
                    copy_text(state->login.status, sizeof(state->login.status), "MODE LOCKED AFTER START");
                    client_log(state, "room_mode_blocked", "reason=session_active session=%s", state->room_link_session_id);
                    break;
                }
                cycle_room_link_mode(state, 1);
                mark_room_link_mode_local_override(state);
                client_log(state, "room_mode_changed", "direction=right mode=%s", room_link_mode_api_name(state->room_link_mode));
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->room_selected == 3 && state->room_chat_scroll > 0) {
                state->room_chat_scroll--;
                copy_text(state->login.status, sizeof(state->login.status), "CHAT LOG NEWER");
            }
            break;
        case SDLK_LEFT:
            if (state->room_selected == 0) {
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    client_log(state, "room_slot_blocked", "reason=game_ended direction=left");
                    break;
                }
                cycle_room_rom_slot(state, -1);
                client_log(state, "room_slot_changed", "direction=left slot_index=%d", state->room_slot_index);
                sync_room_state(state);
            }
            else if (state->room_selected == 1 || state->room_selected == 2) {
                if (!current_room_is_user1(state)) {
                    copy_text(state->login.status, sizeof(state->login.status), "MODE SELECTED BY USER1");
                    client_log(state, "room_mode_blocked", "reason=user2 direction=left");
                    break;
                }
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    break;
                }
                if (state->room_link_session_id[0] != '\0') {
                    copy_text(state->login.status, sizeof(state->login.status), "MODE LOCKED AFTER START");
                    client_log(state, "room_mode_blocked", "reason=session_active session=%s", state->room_link_session_id);
                    break;
                }
                cycle_room_link_mode(state, -1);
                mark_room_link_mode_local_override(state);
                client_log(state, "room_mode_changed", "direction=left mode=%s", room_link_mode_api_name(state->room_link_mode));
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->room_selected == 3) {
                unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room_chat_log);
                unsigned max_scroll = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
                if (state->room_chat_scroll < max_scroll) {
                    state->room_chat_scroll++;
                    copy_text(state->login.status, sizeof(state->login.status), "CHAT LOG OLDER");
                }
            }
            break;
        case SDLK_BACKSPACE:
            if (state->room_selected == 4 && state->room_chat_editing) {
                remove_last_utf8_char(state->room_chat_input);
            }
            break;
        case SDLK_F5:
            if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                client_log(state, "room_ready_debug_blocked", "reason=game_ended");
                break;
            }
            state->room_ready_peer = !state->room_ready_peer;
            update_room_ready_status(state);
            break;
        case SDLK_m:
            if (!current_room_is_user1(state)) {
                copy_text(state->login.status, sizeof(state->login.status), "MODE SELECTED BY USER1");
                client_log(state, "room_mode_blocked", "reason=user2 key=m");
                break;
            }
            if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                break;
            }
            if (state->room_link_session_id[0] != '\0') {
                copy_text(state->login.status, sizeof(state->login.status), "MODE LOCKED AFTER START");
                client_log(state, "room_mode_blocked", "reason=session_active session=%s", state->room_link_session_id);
                break;
            }
            cycle_room_link_mode(state, 1);
            mark_room_link_mode_local_override(state);
            client_log(state, "room_mode_changed", "direction=m mode=%s", room_link_mode_api_name(state->room_link_mode));
            sync_room_state(state);
            update_room_ready_status(state);
            break;
        case SDLK_t:
        case SDLK_b:
            if (!current_room_is_user1(state)) {
                copy_text(state->login.status, sizeof(state->login.status), "MODE SELECTED BY USER1");
                client_log(state, "room_mode_blocked", "reason=user2 key=%c", (char)key->keysym.sym);
                break;
            }
            if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                break;
            }
            if (state->room_link_session_id[0] != '\0') {
                copy_text(state->login.status, sizeof(state->login.status), "MODE LOCKED AFTER START");
                client_log(state, "room_mode_blocked", "reason=session_active session=%s", state->room_link_session_id);
                break;
            }
            if (key->keysym.sym == SDLK_t) {
                set_room_link_mode(state, INTEGRAL_ROOM_MODE_TRADE);
            }
            else if (key->keysym.sym == SDLK_b) {
                set_room_link_mode(state, INTEGRAL_ROOM_MODE_BATTLE);
            }
            mark_room_link_mode_local_override(state);
            client_log(state, "room_mode_set", "key=%c mode=%s", (char)key->keysym.sym, room_link_mode_api_name(state->room_link_mode));
            sync_room_state(state);
            update_room_ready_status(state);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->room_selected == 0) {
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    client_log(state, "room_slot_blocked", "reason=game_ended direction=enter");
                    break;
                }
                cycle_room_rom_slot(state, 1);
                client_log(state, "room_slot_changed", "direction=enter slot_index=%d", state->room_slot_index);
            }
            else if (state->room_selected == 1) {
                if (!current_room_is_user1(state)) {
                    copy_text(state->login.status, sizeof(state->login.status), "MODE SELECTED BY USER1");
                    client_log(state, "room_mode_blocked", "reason=user2 direction=enter");
                    break;
                }
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    break;
                }
                if (state->room_link_session_id[0] != '\0') {
                    copy_text(state->login.status, sizeof(state->login.status), "MODE LOCKED AFTER START");
                    client_log(state, "room_mode_blocked", "reason=session_active session=%s", state->room_link_session_id);
                    break;
                }
                cycle_room_link_mode(state, 1);
                mark_room_link_mode_local_override(state);
                client_log(state, "room_mode_changed", "direction=enter mode=%s", room_link_mode_api_name(state->room_link_mode));
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->room_selected == 2) {
                if (current_room_game_ended(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ENDED  ESC MAIN MENU");
                    client_log(state, "room_ready_blocked", "reason=game_instance_used");
                    break;
                }
                if (!registered_rom_slot_at(state, state->room_slot_index)) {
                    copy_text(state->login.status, sizeof(state->login.status), "ROOM SLOT REQUIRED");
                    client_log(state, "room_ready_blocked", "reason=slot_required slot_index=%d", state->room_slot_index);
                    break;
                }
                const IntegralConfigRomSlot *room_slot = registered_rom_slot_at(state, state->room_slot_index);
                if (!state->room_ready_self &&
                    (!room_slot || !resolve_save_upload_outbox(state, room_slot->save_id))) {
                    client_log(state,
                               "room_ready_blocked",
                               "reason=outbox_recovery slot_index=%d",
                               state->room_slot_index);
                    break;
                }
                state->room_ready_self = !state->room_ready_self;
                client_log(state,
                           "room_ready_toggle",
                           "ready_self=%d slot_index=%d",
                           state->room_ready_self ? 1 : 0,
                           state->room_slot_index);
                sync_room_state(state);
                update_room_ready_status(state);
            }
            else if (state->room_selected == 4) {
                if (state->room_chat_editing) {
                    if (state->room_chat_input[0] != '\0') {
                        char error[160];
                        if (integral_api_send_room_chat(state->login.server,
                                                         state->login.token,
                                                         state->room_number,
                                                         state->room_chat_input,
                                                         error,
                                                         sizeof(error)) != 0) {
                            snprintf(state->login.status, sizeof(state->login.status), "CHAT SEND FAILED %s", error);
                            break;
                        }
                        state->room_chat_input[0] = '\0';
                        state->room_chat_composition[0] = '\0';
                        refresh_room(state);
                        copy_text(state->login.status, sizeof(state->login.status), "CHAT SENT LOCAL");
                    }
                    else {
                        state->room_chat_editing = false;
                        state->room_chat_composition[0] = '\0';
                        SDL_StopTextInput();
                        copy_text(state->login.status, sizeof(state->login.status), "CHAT INPUT SAVED");
                    }
                }
                else {
                    state->room_chat_editing = true;
                    state->room_chat_composition[0] = '\0';
                    SDL_Rect input_rect = {.x = 154, .y = 386, .w = INTEGRAL_WINDOW_WIDTH - 202, .h = 30};
                    SDL_SetTextInputRect(&input_rect);
                    SDL_StartTextInput();
                    copy_text(state->login.status, sizeof(state->login.status), "CHAT INPUT ACTIVE");
                }
            }
            break;
        default:
            break;
    }
}

static bool n64_room_runtime_active(const AppState *state)
{
    return state->screen == SCREEN_N64_ROOM &&
           (state->n64_runtime_media_paired || state->n64_runtime_media_host_pid != 0);
}

static void request_runtime_exit_confirmation(AppState *state, bool quit_client)
{
    state->runtime_exit_confirming = true;
    state->runtime_exit_confirm_yes = false;
    state->runtime_exit_quit_client = quit_client;
    state->n64_runtime_media_last_sent_buttons = UINT64_MAX;
    integral_n64_runtime_media_stream_set_exit_confirmation(
        state->n64_runtime_media_stream, true, false);
}

static void cancel_runtime_exit_confirmation(AppState *state)
{
    state->runtime_exit_confirming = false;
    state->runtime_exit_confirm_yes = false;
    state->runtime_exit_quit_client = false;
    integral_n64_runtime_media_stream_set_exit_confirmation(
        state->n64_runtime_media_stream, false, false);
}

static void confirm_runtime_exit(AppState *state)
{
    bool quit_client = state->runtime_exit_quit_client;
    cancel_runtime_exit_confirmation(state);
    leave_current_room(state, false);
    if (quit_client) {
        state->quit = true;
    }
    else {
        state->screen = SCREEN_MAIN_MENU;
        copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
    }
}

static bool n64_confirmation_binding_pressed(const AppState *state,
                                             const SDL_Event *event,
                                             unsigned index)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    bool pressed = false;
    key_spec_to_names_count(state->keys.n64_p1, names,
                            INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    if (index >= INTEGRAL_N64_RUNTIME_KEY_BUTTONS || !names[index][0]) return false;
    SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(names[index]);
    return binding != SDLK_UNKNOWN &&
           integral_gb_runtime_key_config_binding_matches_event(binding, event, &pressed) &&
           pressed;
}

static bool handle_runtime_exit_confirmation_event(AppState *state,
                                                   const SDL_Event *event)
{
    if (!state->runtime_exit_confirming) return false;
    if (event->type == SDL_QUIT ||
        (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE)) {
        return true;
    }
    bool binding_right = n64_confirmation_binding_pressed(state, event, 0);
    bool binding_left = n64_confirmation_binding_pressed(state, event, 1);
    bool binding_a = n64_confirmation_binding_pressed(state, event, 7);
    if (binding_right || binding_left) {
        state->runtime_exit_confirm_yes = !state->runtime_exit_confirm_yes;
        integral_n64_runtime_media_stream_set_exit_confirmation(
            state->n64_runtime_media_stream, true, state->runtime_exit_confirm_yes);
        return true;
    }
    if (binding_a) {
        if (state->runtime_exit_confirm_yes) confirm_runtime_exit(state);
        else cancel_runtime_exit_confirmation(state);
        return true;
    }
    if (event->type != SDL_KEYDOWN || event->key.repeat)
        return game_controller_input_event(event->type);
    switch (event->key.keysym.sym) {
        case SDLK_ESCAPE:
            cancel_runtime_exit_confirmation(state);
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
            state->runtime_exit_confirm_yes = !state->runtime_exit_confirm_yes;
            integral_n64_runtime_media_stream_set_exit_confirmation(
                state->n64_runtime_media_stream, true, state->runtime_exit_confirm_yes);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->runtime_exit_confirm_yes) confirm_runtime_exit(state);
            else cancel_runtime_exit_confirmation(state);
            break;
        default:
            break;
    }
    return true;
}

static void handle_n64_room_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    if (state->n64_runtime_media_paired && strcmp(state->n64_runtime_media_role, "remote") == 0 &&
        key->keysym.sym != SDLK_ESCAPE && n64_remote_controller_scancode(state, key->keysym.scancode)) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            if (n64_room_runtime_active(state)) {
                request_runtime_exit_confirmation(state, false);
                break;
            }
            leave_current_room(state, false);
            state->screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            state->room_selected = (state->room_selected + 1) % INTEGRAL_N64_RUNTIME_ROOM_ROWS;
            break;
        case SDLK_UP:
            state->room_selected = state->room_selected == 0 ? INTEGRAL_N64_RUNTIME_ROOM_ROWS - 1 : state->room_selected - 1;
            break;
        case SDLK_LEFT:
            if (state->room_selected <= 2) {
                int local_user = n64_room_local_user_index(state);
                if ((local_user == 0 && state->room_selected <= 1) ||
                    (local_user == 1 && state->room_selected == 2)) {
                    cycle_n64_room_slot(state, state->room_selected, -1);
                    (void)sync_n64_room_state(state, false);
                }
            }
            else if (state->room_selected == 4) {
                unsigned message_count = chat_message_count((char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room_chat_log);
                unsigned max_scroll = message_count > INTEGRAL_CHAT_VISIBLE_LINES ? message_count - INTEGRAL_CHAT_VISIBLE_LINES : 0;
                if (state->room_chat_scroll < max_scroll) {
                    state->room_chat_scroll++;
                }
            }
            break;
        case SDLK_RIGHT:
            if (state->room_selected <= 2) {
                int local_user = n64_room_local_user_index(state);
                if ((local_user == 0 && state->room_selected <= 1) ||
                    (local_user == 1 && state->room_selected == 2)) {
                    cycle_n64_room_slot(state, state->room_selected, 1);
                    (void)sync_n64_room_state(state, false);
                }
            }
            else if (state->room_selected == 4 && state->room_chat_scroll > 0) {
                state->room_chat_scroll--;
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->room_selected <= 2) {
                int local_user = n64_room_local_user_index(state);
                if ((local_user == 0 && state->room_selected <= 1) ||
                    (local_user == 1 && state->room_selected == 2)) {
                    cycle_n64_room_slot(state, state->room_selected, 1);
                    (void)sync_n64_room_state(state, false);
                }
            }
            else if (state->room_selected == 3) {
                bool next_ready = !state->n64_room_ready;
                if (next_ready && !resolve_n64_room_local_outbox(state)) {
                    state->n64_room_ready = false;
                    reset_n64_runtime_media_connection(state);
                    break;
                }
                if (!sync_n64_room_state(state, next_ready)) {
                    state->n64_room_ready = false;
                    reset_n64_runtime_media_connection(state);
                    break;
                }
                state->n64_room_ready = next_ready;
                if (!next_ready) {
                    reset_n64_runtime_media_connection(state);
                }
                copy_text(state->login.status,
                          sizeof(state->login.status),
                          state->n64_room_ready ? "N64 ROOM READY  NO SAV OVERWRITE"
                                                : "N64 ROOM READY CANCELLED");
                if (state->n64_room_ready && state->room_ready_self && state->room_ready_peer) {
                    (void)request_n64_runtime_media_session(state);
                }
            }
            break;
        default:
            break;
    }
}

static void move_local_selection(AppState *state, int delta)
{
    unsigned row_count = state->screen == SCREEN_GB_MOBILE ? INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS
                                                           : INTEGRAL_GB_RUNTIME_MODE_ROWS;
    int selected = (int)state->local_selected + delta;
    if (selected < 0) {
        selected = (int)row_count - 1;
    }
    if (selected >= (int)row_count) {
        selected = 0;
    }
    state->local_selected = (unsigned)selected;
}

static void move_local_mode_selection(AppState *state, int delta)
{
    int selected = (int)state->local_mode_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_LOCAL_MODE_ROWS - 1;
    }
    if (selected >= INTEGRAL_LOCAL_MODE_ROWS) {
        selected = 0;
    }
    state->local_mode_selected = (unsigned)selected;
}

static void handle_local_mode_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_local_mode_selection(state, 1);
            break;
        case SDLK_UP:
            move_local_mode_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->local_mode_selected == 0) {
                state->screen = SCREEN_LOCAL;
                state->local_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "GB MODE");
            }
            else if (state->local_mode_selected == 1) {
                state->screen = SCREEN_GB_MOBILE;
                state->local_selected = 0;
                refresh_mobile_scenarios(state);
            }
            else {
                init_n64_runtime_selection(state);
                state->screen = SCREEN_N64_RUNTIME;
                copy_text(state->login.status, sizeof(state->login.status), "N64 MODE");
            }
            break;
        default:
            break;
    }
}

static void clear_local_slot(AppState *state)
{
    if (state->local_selected < 1 || state->local_selected > 2) {
        return;
    }
    unsigned slot_index = state->local_selected - 1;
    state->local_slot_indices[slot_index] = -1;
    IntegralConfigLocal local = {
        .slot1_index = state->local_slot_indices[0],
        .slot2_index = state->local_slot_indices[1],
    };
    if (integral_config_save_local(state->config_path, &local) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "SLOT CLEAR SAVE FAILED");
        return;
    }
    snprintf(state->login.status, sizeof(state->login.status), "SLOT %u CLEARED", slot_index + 1);
}

static void handle_local_key(AppState *state, const SDL_KeyboardEvent *key)
{
    bool mobile_mode = state->screen == SCREEN_GB_MOBILE;
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->screen = SCREEN_LOCAL_MODE;
            copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_local_selection(state, 1);
            break;
        case SDLK_UP:
            move_local_selection(state, -1);
            break;
        case SDLK_RIGHT:
            if (state->local_selected == 1 || (!mobile_mode && state->local_selected == 2)) {
                cycle_local_rom_slot(state, state->local_selected - 1, 1);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            else if (mobile_mode && state->local_selected == 2) cycle_mobile_scenario(state, 1);
            break;
        case SDLK_LEFT:
            if (state->local_selected == 1 || (!mobile_mode && state->local_selected == 2)) {
                cycle_local_rom_slot(state, state->local_selected - 1, -1);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            else if (mobile_mode && state->local_selected == 2) cycle_mobile_scenario(state, -1);
            break;
        case SDLK_BACKSPACE:
            if (!mobile_mode || state->local_selected == 1) {
                clear_local_slot(state);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->local_selected == 0) {
                if (mobile_mode) {
                    start_local_gb_mobile(state);
                }
                else {
                    start_local_gb_runtime(state);
                }
            }
            else if (state->local_selected == 1 || (!mobile_mode && state->local_selected == 2)) {
                cycle_local_rom_slot(state, state->local_selected - 1, 1);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            else if (mobile_mode && state->local_selected == 2) cycle_mobile_scenario(state, 1);
            break;
        default:
            break;
    }
}

static void handle_n64_runtime_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->screen = SCREEN_LOCAL_MODE;
            copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            state->integral_n64_runtime_selected = (state->integral_n64_runtime_selected + 1) % 6;
            break;
        case SDLK_UP:
            state->integral_n64_runtime_selected = state->integral_n64_runtime_selected == 0 ? 5 : state->integral_n64_runtime_selected - 1;
            break;
        case SDLK_RIGHT:
            if (state->integral_n64_runtime_selected == 1) {
                cycle_n64_runtime_n64_slot(state, 1);
            }
            else if (state->integral_n64_runtime_selected >= 2 && state->integral_n64_runtime_selected <= 5) {
                cycle_n64_runtime_transfer_slot(state, state->integral_n64_runtime_selected - 2, 1);
            }
            break;
        case SDLK_LEFT:
            if (state->integral_n64_runtime_selected == 1) {
                cycle_n64_runtime_n64_slot(state, -1);
            }
            else if (state->integral_n64_runtime_selected >= 2 && state->integral_n64_runtime_selected <= 5) {
                cycle_n64_runtime_transfer_slot(state, state->integral_n64_runtime_selected - 2, -1);
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->integral_n64_runtime_selected == 0) {
                start_local_n64_runtime(state);
            }
            else if (state->integral_n64_runtime_selected == 1) {
                cycle_n64_runtime_n64_slot(state, 1);
            }
            else if (state->integral_n64_runtime_selected >= 2 && state->integral_n64_runtime_selected <= 5) {
                cycle_n64_runtime_transfer_slot(state, state->integral_n64_runtime_selected - 2, 1);
            }
            break;
        default:
            break;
    }
}

static void move_key_selection(AppState *state, int delta)
{
    int selected = (int)state->key_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_KEY_ROWS - 1;
    }
    if (selected >= INTEGRAL_KEY_ROWS) {
        selected = 0;
    }
    state->key_selected = (unsigned)selected;
}

static void save_key_config(AppState *state)
{
    if (integral_config_save_keys(state->config_path, &state->keys) == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "KEY CONFIG SAVED");
    }
    else {
        copy_text(state->login.status, sizeof(state->login.status), "KEY CONFIG SAVE FAILED");
    }
}

static void begin_key_capture(AppState *state, KeyCaptureTarget target)
{
    state->key_capture_target = target;
    state->key_capture_step = 0;
    state->key_capture_wait_release = false;
    state->key_capture_release_binding = SDLK_UNKNOWN;
    copy_text(state->login.status, sizeof(state->login.status), "PRESS KEY OR JOY-CON INPUT");
}

static void finish_key_capture(AppState *state)
{
    state->key_capture_target = KEY_CAPTURE_NONE;
    state->key_capture_step = 0;
    state->key_capture_wait_release = false;
    state->key_capture_release_binding = SDLK_UNKNOWN;
    save_key_config(state);
}

static void apply_captured_binding(AppState *state,
                                   SDL_Keycode key,
                                   SDL_Scancode scancode,
                                   const char *controller_name)
{
    char name[INTEGRAL_CONFIG_KEY_NAME_MAX];
    if (controller_name && controller_name[0]) {
        copy_text(name, sizeof(name), controller_name);
    }
    else if (state->key_capture_target == KEY_CAPTURE_N64) {
        scancode_name_from_sdl(scancode, name, sizeof(name));
    }
    else {
        key_name_from_sdl(key, name, sizeof(name));
    }
    if (strcmp(name, "UNKNOWN") == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "UNKNOWN KEY");
        return;
    }

    if (state->key_capture_target == KEY_CAPTURE_UTILS) {
        if (state->key_capture_step == 0) {
            copy_text(state->keys.fast, sizeof(state->keys.fast), name);
        }
        else if (state->key_capture_step == 1) {
            copy_text(state->keys.screenshot, sizeof(state->keys.screenshot), name);
        }
        else if (state->key_capture_step == 2) {
            copy_text(state->keys.escape, sizeof(state->keys.escape), name);
        }
        else if (state->key_capture_step == 3) {
            copy_text(state->keys.turbo_hold, sizeof(state->keys.turbo_hold), name);
        }
        else {
            copy_text(state->keys.reset, sizeof(state->keys.reset), name);
        }
        state->key_capture_step++;
        if (state->key_capture_step >= INTEGRAL_UTIL_KEYS) {
            finish_key_capture(state);
        }
        else {
            snprintf(state->login.status,
                     sizeof(state->login.status),
                     "NEXT %s",
                     key_config_step_label(state->key_capture_target, state->key_capture_step));
        }
        return;
    }
    if (state->key_capture_target == KEY_CAPTURE_N64) {
        char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
        key_spec_to_names_count(state->keys.n64_p1, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
        unsigned index = n64_key_spec_index_for_capture_step(state->key_capture_step);
        copy_text(names[index], sizeof(names[index]), name);
        key_names_to_spec_count(names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS, state->keys.n64_p1, sizeof(state->keys.n64_p1));
        state->key_capture_step++;
        if (state->key_capture_step >= INTEGRAL_N64_RUNTIME_KEY_BUTTONS) {
            finish_key_capture(state);
        }
        else {
            snprintf(state->login.status,
                     sizeof(state->login.status),
                     "NEXT %s",
                     key_config_step_label(state->key_capture_target, state->key_capture_step));
        }
        return;
    }

    char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    char *target_spec = state->key_capture_target == KEY_CAPTURE_SLOT1 ? state->keys.slot1 : state->keys.slot2;
    key_spec_to_names(target_spec, names);
    unsigned index = key_spec_index_for_capture_step(state->key_capture_step);
    copy_text(names[index], sizeof(names[index]), name);
    key_names_to_spec(names, target_spec, INTEGRAL_CONFIG_KEY_SPEC_MAX);
    state->key_capture_step++;
    if (state->key_capture_step >= INTEGRAL_KEY_BUTTONS) {
        finish_key_capture(state);
    }
    else {
        snprintf(state->login.status,
                 sizeof(state->login.status),
                 "NEXT %s",
                 key_config_step_label(state->key_capture_target, state->key_capture_step));
    }
}

static void reset_key_defaults(AppState *state)
{
    integral_keys_defaults(&state->keys);
    save_key_config(state);
}

static void handle_key_config_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    if (state->key_capture_target != KEY_CAPTURE_NONE) {
        if (key->keysym.sym == SDLK_ESCAPE) {
            state->key_capture_target = KEY_CAPTURE_NONE;
            state->key_capture_step = 0;
            state->key_capture_wait_release = false;
            state->key_capture_release_binding = SDLK_UNKNOWN;
            copy_text(state->login.status, sizeof(state->login.status), "KEY CONFIG CANCELED");
            return;
        }
        apply_captured_binding(state, key->keysym.sym, key->keysym.scancode, NULL);
        return;
    }

    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_key_selection(state, 1);
            break;
        case SDLK_UP:
            move_key_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->key_selected == 0) {
                begin_key_capture(state, KEY_CAPTURE_SLOT1);
            }
            else if (state->key_selected == 1) {
                begin_key_capture(state, KEY_CAPTURE_SLOT2);
            }
            else if (state->key_selected == 2) {
                begin_key_capture(state, KEY_CAPTURE_N64);
            }
            else if (state->key_selected == 3) {
                begin_key_capture(state, KEY_CAPTURE_UTILS);
            }
            else {
                reset_key_defaults(state);
            }
            break;
        default:
            break;
    }
}

static bool game_controller_input_event(Uint32 type)
{
    return type == SDL_CONTROLLERBUTTONDOWN || type == SDL_CONTROLLERBUTTONUP ||
           type == SDL_CONTROLLERAXISMOTION || type == SDL_JOYBUTTONDOWN ||
           type == SDL_JOYBUTTONUP || type == SDL_JOYAXISMOTION ||
           type == SDL_JOYHATMOTION;
}

static void handle_key_config_controller_event(AppState *state, const SDL_Event *event)
{
    if (state->key_capture_target == KEY_CAPTURE_NONE ||
        state->key_capture_target == KEY_CAPTURE_UTILS) return;
    if (state->key_capture_wait_release) {
        bool pressed = true;
        if (integral_gb_runtime_key_config_binding_matches_event(
                state->key_capture_release_binding, event, &pressed) && !pressed) {
            state->key_capture_wait_release = false;
            state->key_capture_release_binding = SDLK_UNKNOWN;
        }
        return;
    }
    SDL_Keycode binding = integral_gb_runtime_key_config_code_from_event(event);
    if (binding == SDLK_UNKNOWN) return;
    const char *name = integral_gb_runtime_key_config_key_name(binding);
    if (!name || strcmp(name, "UNKNOWN") == 0) return;
    apply_captured_binding(state, binding, SDL_SCANCODE_UNKNOWN, name);
    if (state->key_capture_target != KEY_CAPTURE_NONE) {
        state->key_capture_wait_release = true;
        state->key_capture_release_binding = binding;
    }
}

static bool set_game_input_active(AppState *state, bool active)
{
    if (state->game_input_active == active) return true;
    if (active) {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
            client_log(state, "controller_init_failed", "error=%s", SDL_GetError());
            copy_text(state->login.status, sizeof(state->login.status), "CONTROLLER INIT FAILED");
            return false;
        }
        SDL_GameControllerEventState(SDL_ENABLE);
        SDL_JoystickEventState(SDL_ENABLE);
        int opened = integral_gb_runtime_key_config_open_game_controllers();
        state->game_input_active = true;
        client_log(state, "controller_input_enabled", "opened=%d scope=%s", opened,
                   state->screen == SCREEN_KEY_CONFIG ? "key_config" : "game");
        return true;
    }
    integral_gb_runtime_key_config_close_game_controllers();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    state->game_input_active = false;
    client_log(state, "controller_input_disabled", "");
    return true;
}

static bool client_game_input_required(const AppState *state)
{
    return state->screen == SCREEN_KEY_CONFIG ||
           (state->screen == SCREEN_N64_ROOM && state->n64_runtime_media_authenticated);
}

static void move_rom_selection(AppState *state, int delta)
{
    int selected = (int)state->rom_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_ROM_ROWS - 1;
    }
    if (selected >= INTEGRAL_ROM_ROWS) {
        selected = 0;
    }
    state->rom_selected = (unsigned)selected;
}

static char *rom_edit_value(AppState *state)
{
    if (state->rom_selected >= INTEGRAL_ROM_SLOTS) {
        return NULL;
    }
    if (state->rom_edit_target == ROM_EDIT_ROM) {
        return state->rom_slots[state->rom_selected].rom_path;
    }
    if (state->rom_edit_target == ROM_EDIT_INITIAL_SAVE) {
        return state->rom_initial_save_import_path;
    }
    return NULL;
}

static size_t rom_edit_capacity(AppState *state)
{
    if (state->rom_edit_target == ROM_EDIT_ROM) {
        return sizeof(state->rom_slots[state->rom_selected].rom_path);
    }
    if (state->rom_edit_target == ROM_EDIT_INITIAL_SAVE) {
        return sizeof(state->rom_initial_save_import_path);
    }
    return 0;
}

static void begin_rom_edit(AppState *state, RomEditTarget target)
{
    if (state->rom_selected >= INTEGRAL_ROM_SLOTS) {
        state->screen = SCREEN_MAIN_MENU;
        state->rom_edit_target = ROM_EDIT_NONE;
        SDL_StopTextInput();
        return;
    }
    if (target == ROM_EDIT_ROM &&
        state->rom_initial_save_import_slot == (int)state->rom_selected) {
        state->rom_initial_save_import_slot = -1;
        state->rom_initial_save_import_path[0] = '\0';
    }
    state->rom_edit_target = target;
    SDL_StartTextInput();
    copy_text(state->login.status,
              sizeof(state->login.status),
              target == ROM_EDIT_INITIAL_SAVE ? "EDITING INITIAL SAV PATH" : "EDITING ROM PATH");
}

static void finish_rom_edit(AppState *state)
{
    if (state->rom_edit_target == ROM_EDIT_INITIAL_SAVE) {
        if (state->rom_initial_save_import_path[0] == '\0' ||
            !local_regular_file(state->rom_initial_save_import_path)) {
            state->rom_initial_save_import_slot = -1;
            state->rom_initial_save_import_path[0] = '\0';
            copy_text(state->login.status, sizeof(state->login.status), "INITIAL SAV FILE REQUIRED");
        }
        else {
            state->rom_initial_save_import_slot = (int)state->rom_selected;
            copy_text(state->login.status, sizeof(state->login.status), "INITIAL SAV SELECTED  REGISTER TO CONFIRM");
        }
    }
    else if (state->rom_selected < INTEGRAL_ROM_SLOTS && slot_has_server_registration(&state->rom_slots[state->rom_selected])) {
        if (integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS) != 0) {
            copy_text(state->login.status, sizeof(state->login.status), "LOCAL ROM PATH SAVE FAILED");
        }
        else {
            copy_text(state->login.status, sizeof(state->login.status), "LOCAL ROM PATH SAVED");
        }
    }
    else {
        copy_text(state->login.status, sizeof(state->login.status), "LOCAL READY");
    }
    state->rom_edit_target = ROM_EDIT_NONE;
    SDL_StopTextInput();
}

static const char *title_for_rom_path(const char *path)
{
    return path_file_name(path);
}

#if !defined(_WIN32) && !defined(__APPLE__) && defined(INTEGRAL_USE_OPENSSL)
static int digest_file_hex_openssl(const char *path,
                                   const EVP_MD *md,
                                   char *out,
                                   size_t out_size)
{
    if (!md) {
        return -1;
    }
    int digest_size = EVP_MD_get_size(md);
    if (digest_size <= 0 || out_size < (size_t)digest_size * 2u + 1u) {
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int actual_size = 0;
    int rc = -1;
    if (!ctx || EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        goto done;
    }
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, n) != 1) {
            goto done;
        }
    }
    if (ferror(file) != 0 || EVP_DigestFinal_ex(ctx, digest, &actual_size) != 1 ||
        actual_size != (unsigned int)digest_size) {
        goto done;
    }
    for (unsigned int i = 0; i < actual_size; i++) {
        snprintf(out + i * 2u, out_size - i * 2u, "%02x", digest[i]);
    }
    rc = 0;
done:
    EVP_MD_CTX_free(ctx);
    fclose(file);
    return rc;
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(INTEGRAL_USE_OPENSSL)
static bool shell_quote_path(const char *path, char *out, size_t out_size)
{
    size_t used = 0;
    if (out_size < 3) {
        return false;
    }
    out[used++] = '\'';
    for (const char *p = path; *p; p++) {
        const char *chunk = *p == '\'' ? "'\\''" : NULL;
        if (chunk) {
            size_t len = strlen(chunk);
            if (used + len + 2 > out_size) {
                return false;
            }
            memcpy(out + used, chunk, len);
            used += len;
        }
        else {
            if (used + 2 > out_size) {
                return false;
            }
            out[used++] = *p;
        }
    }
    out[used++] = '\'';
    out[used] = '\0';
    return true;
}

static int digest_file_hex_command(const char *path,
                                   const char *tool,
                                   size_t digest_chars,
                                   char *out,
                                   size_t out_size)
{
    if (out_size < digest_chars + 1u) {
        return -1;
    }
    char quoted[640];
    if (!shell_quote_path(path, quoted, sizeof(quoted))) {
        return -1;
    }
    char command[768];
    int n = snprintf(command, sizeof(command), "%s -- %s", tool, quoted);
    if (n < 0 || (size_t)n >= sizeof(command)) {
        return -1;
    }
    FILE *pipe = popen(command, "r");
    if (!pipe) {
        return -1;
    }
    char line[256];
    bool ok = fgets(line, sizeof(line), pipe) != NULL;
    int status = pclose(pipe);
    if (!ok || status != 0 || strlen(line) < digest_chars) {
        return -1;
    }
    for (size_t i = 0; i < digest_chars; i++) {
        if (!isxdigit((unsigned char)line[i])) {
            return -1;
        }
        out[i] = (char)tolower((unsigned char)line[i]);
    }
    out[digest_chars] = '\0';
    return 0;
}
#endif

static int sha256_file_hex(const char *path, char *out, size_t out_size)
{
#ifdef _WIN32
    const size_t digest_size = 32;
    if (out_size < digest_size * 2 + 1) {
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[32];
    int rc = -1;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0))) {
        goto done;
    }
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer, (ULONG)n, 0))) {
            goto done;
        }
    }
    if (ferror(file) != 0 ||
        !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, (ULONG)sizeof(digest), 0))) {
        goto done;
    }
    for (size_t i = 0; i < digest_size; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    rc = 0;
done:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    fclose(file);
    return rc;
#elif !defined(__APPLE__) && defined(INTEGRAL_USE_OPENSSL)
    return digest_file_hex_openssl(path, EVP_sha256(), out, out_size);
#elif defined(__APPLE__)
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        CC_SHA256_Update(&ctx, buffer, (CC_LONG)n);
    }
    bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok || out_size < CC_SHA256_DIGEST_LENGTH * 2 + 1) {
        return -1;
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    for (size_t i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    return 0;
#else
    return digest_file_hex_command(path, "sha256sum", 64, out, out_size);
#endif
}

static int sha1_file_hex(const char *path, char *out, size_t out_size)
{
#ifdef _WIN32
    const size_t digest_size = 20;
    if (out_size < digest_size * 2 + 1) {
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[20];
    int rc = -1;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0))) {
        goto done;
    }
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer, (ULONG)n, 0))) {
            goto done;
        }
    }
    if (ferror(file) != 0 ||
        !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, (ULONG)sizeof(digest), 0))) {
        goto done;
    }
    for (size_t i = 0; i < digest_size; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    rc = 0;
done:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    fclose(file);
    return rc;
#elif !defined(__APPLE__) && defined(INTEGRAL_USE_OPENSSL)
    return digest_file_hex_openssl(path, EVP_sha1(), out, out_size);
#elif defined(__APPLE__)
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    CC_SHA1_CTX ctx;
    CC_SHA1_Init(&ctx);
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        CC_SHA1_Update(&ctx, buffer, (CC_LONG)n);
    }
    bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok || out_size < CC_SHA1_DIGEST_LENGTH * 2 + 1) {
        return -1;
    }
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1_Final(digest, &ctx);
    for (size_t i = 0; i < CC_SHA1_DIGEST_LENGTH; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    return 0;
#else
    return digest_file_hex_command(path, "sha1sum", 40, out, out_size);
#endif
}

static int write_binary_file(const char *path, const unsigned char *data, size_t data_size)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        return -1;
    }
    int rc = 0;
    if (data_size > 0 && fwrite(data, 1, data_size, file) != data_size) {
        rc = -1;
    }
    if (fflush(file) != 0) {
        rc = -1;
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        rc = -1;
    }
    if (fclose(file) != 0) {
        rc = -1;
    }
    return rc;
}

static int copy_binary_file_limited(const char *source_path, const char *dest_path, size_t max_size)
{
    unsigned char *data = NULL;
    size_t data_size = 0;
    if (read_binary_file_alloc(source_path, &data, &data_size, max_size) != 0) {
        return -1;
    }
    int rc = write_private_runtime_file(dest_path, data, data_size);
    free(data);
    return rc;
}

static int atomic_replace_binary_file(const char *path, const unsigned char *data, size_t data_size)
{
    char temp_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", path, (long)getpid());
    if (write_private_runtime_file(temp_path, data, data_size) != 0) {
        return -1;
    }
#ifdef _WIN32
    bool replaced = false;
    DWORD replace_error = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt < 20u; attempt++) {
        if (MoveFileExA(temp_path, path,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            replaced = true;
            break;
        }
        replace_error = GetLastError();
        if (replace_error != ERROR_SHARING_VIOLATION &&
            replace_error != ERROR_ACCESS_DENIED) {
            break;
        }
        Sleep(1u);
    }
    if (!replaced) {
#else
    if (rename(temp_path, path) != 0) {
#endif
#ifdef _WIN32
        if (replace_error == ERROR_SUCCESS) replace_error = GetLastError();
        remove(temp_path);
        SetLastError(replace_error);
#else
        int replace_error = errno;
        remove(temp_path);
        errno = replace_error;
#endif
        return -1;
    }
    return 0;
}

static int write_private_runtime_file(const char *path,
                                      const unsigned char *data,
                                      size_t data_size)
{
#ifdef _WIN32
    return write_binary_file(path, data, data_size);
#else
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    int rc = 0;
    if (data_size > 0 && fwrite(data, 1, data_size, file) != data_size) rc = -1;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) rc = -1;
    if (fclose(file) != 0) rc = -1;
    return rc;
#endif
}

static void make_safe_outbox_token(const char *source, char *out, size_t out_size)
{
    size_t used = 0;
    if (out_size == 0) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)source; p && *p && used + 1 < out_size; p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            out[used++] = (char)*p;
        }
    }
    if (used == 0) {
        copy_text(out, out_size, "save");
    }
    else {
        out[used] = '\0';
    }
}

static int ensure_directory(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST) {
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
#endif
        return 0;
    }
    return -1;
}

static int ensure_private_runtime_directory(const char *path)
{
#ifdef _WIN32
    return ensure_directory(path);
#else
    if (mkdir(path, 0700) != 0 && errno != EEXIST) return -1;
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode)) return -1;
    return chmod(path, 0700);
#endif
}

static int make_private_runtime_file(const char *path)
{
#ifdef _WIN32
    (void)path;
    return 0;
#else
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode)) return -1;
    return chmod(path, 0600);
#endif
}

static int append_flushed_text_line(const char *path, const char *line)
{
#ifdef _WIN32
    FILE *file = fopen(path, "ab");
#else
    int flags = O_WRONLY | O_CREAT | O_APPEND;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    FILE *file = fdopen(fd, "ab");
#endif
    if (!file) {
#ifndef _WIN32
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
#endif
        return -1;
    }
    int rc = 0;
    if (fwrite(line, 1, strlen(line), file) != strlen(line) ||
        fwrite("\n", 1, 1, file) != 1) {
        rc = -1;
    }
    if (fflush(file) != 0) {
        rc = -1;
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        rc = -1;
    }
    if (fclose(file) != 0) {
        rc = -1;
    }
    return rc;
}

static bool extract_tsv_field(const char *line, const char *key, char *out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char *p = line;
    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *value = p + key_len + 1;
            const char *end = strchr(value, '\t');
            if (!end) {
                end = value + strcspn(value, "\r\n");
            }
            size_t len = (size_t)(end - value);
            if (len >= out_size) {
                len = out_size > 0 ? out_size - 1 : 0;
            }
            if (out_size > 0) {
                memcpy(out, value, len);
                out[len] = '\0';
            }
            return true;
        }
        p = strchr(p, '\t');
        if (p) {
            p++;
        }
    }
    return false;
}

static int read_binary_file(const char *path, unsigned char *out, size_t out_capacity, size_t *out_size)
{
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    size_t n = fread(out, 1, out_capacity, file);
    bool ok = ferror(file) == 0;
    int extra = fgetc(file);
    fclose(file);
    if (!ok || extra != EOF) {
        return -1;
    }
    *out_size = n;
    return 0;
}

static int read_binary_file_alloc(const char *path, unsigned char **out, size_t *out_size, size_t max_size)
{
    *out = NULL;
    *out_size = 0;
    FILE *file = fopen(path, "rb");
    if (!file) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size <= 0 || (size_t)size > max_size || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    unsigned char *data = malloc((size_t)size);
    if (!data) {
        fclose(file);
        return -1;
    }
    size_t n = fread(data, 1, (size_t)size, file);
    bool ok = ferror(file) == 0 && n == (size_t)size;
    fclose(file);
    if (!ok) {
        free(data);
        return -1;
    }
    *out = data;
    *out_size = n;
    return 0;
}

static int sync_slot_from_server(AppState *state,
                                 const IntegralConfigRomSlot *slot,
                                 const char *session_save_path,
                                 LocalSyncSlot *sync_slot)
{
    if (slot->save_id[0] == '\0' || !session_save_path || session_save_path[0] == '\0') {
        return -1;
    }
    if (!resolve_save_upload_outbox(state, slot->save_id)) {
        return -1;
    }
    unsigned char save_data[INTEGRAL_MAX_SAVE_BYTES];
    size_t save_size = 0;
    int revision = 0;
    char error[160];
    if (integral_api_download_save(state->login.server,
                              state->login.token,
                              slot->save_id,
                              save_data,
                              sizeof(save_data),
                              &save_size,
                              &revision,
                              error,
                              sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "SAV DOWNLOAD FAILED %s", error);
        return -1;
    }
    if (atomic_replace_binary_file(session_save_path, save_data, save_size) != 0 ||
        sha256_file_hex(session_save_path, sync_slot->last_hash, sizeof(sync_slot->last_hash)) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "SESSION SAV WRITE FAILED");
        return -1;
    }
    copy_text(sync_slot->save_id, sizeof(sync_slot->save_id), slot->save_id);
    copy_text(sync_slot->save_path, sizeof(sync_slot->save_path), session_save_path);
    sync_slot->revision = revision;
    return 0;
}

static void export_save_filename(const IntegralConfigRomSlot *slot, unsigned slot_index, char *out, size_t out_size)
{
    const char *name = path_file_name(slot->rom_path);
    char base[96];
    if (!name || name[0] == '\0') {
        snprintf(base, sizeof(base), "rom%u", slot_index + 1);
    }
    else {
        copy_text(base, sizeof(base), name);
        char *dot = strrchr(base, '.');
        if (dot) {
            *dot = '\0';
        }
    }

    char safe[96];
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)base; *p && used + 1 < sizeof(safe); p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            safe[used++] = (char)*p;
        }
        else if (*p == ' ' || *p == '.') {
            safe[used++] = '_';
        }
    }
    if (used == 0) {
        snprintf(safe, sizeof(safe), "rom%u", slot_index + 1);
    }
    else {
        safe[used] = '\0';
    }
    snprintf(out, out_size, "%s.sav", safe);
}

static void export_registered_saves(AppState *state)
{
    if (state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        return;
    }
    if (ensure_directory(INTEGRAL_EXPORT_FOLDER) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "EXPORT FOLDER CREATE FAILED");
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_now);

    char export_dir[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(export_dir, sizeof(export_dir), "%s/%s", INTEGRAL_EXPORT_FOLDER, timestamp);
    if (ensure_directory(export_dir) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "EXPORT FOLDER CREATE FAILED");
        return;
    }

    unsigned exported = 0;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        IntegralConfigRomSlot *slot = &state->rom_slots[i];
        if (slot->save_id[0] == '\0') {
            continue;
        }

        unsigned char save_data[INTEGRAL_MAX_SAVE_BYTES];
        size_t save_size = 0;
        int revision = 0;
        char error[160];
        if (integral_api_download_save(state->login.server,
                                  state->login.token,
                                  slot->save_id,
                                  save_data,
                                  sizeof(save_data),
                                  &save_size,
                                  &revision,
                                  error,
                                  sizeof(error)) != 0) {
            snprintf(state->login.status, sizeof(state->login.status), "EXPORT FAILED ROM%u %s", i + 1, error);
            client_log(state, "sav_export_failed", "slot=%u save_id=%s error=%s", i + 1, slot->save_id, error);
            return;
        }

        char filename[128];
        export_save_filename(slot, i, filename, sizeof(filename));
        char path[INTEGRAL_CONFIG_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", export_dir, filename);
        if (write_binary_file(path, save_data, save_size) != 0) {
            snprintf(state->login.status, sizeof(state->login.status), "EXPORT WRITE FAILED ROM%u", i + 1);
            client_log(state, "sav_export_write_failed", "slot=%u path=%s", i + 1, path);
            return;
        }
        exported++;
        client_log(state, "sav_exported", "slot=%u revision=%d path=%s", i + 1, revision, path);
    }

    if (exported == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "NO REGISTERED SAVS");
        return;
    }
    snprintf(state->login.status, sizeof(state->login.status), "EXPORTED %u SAVS %s", exported, export_dir);
}

static bool write_save_upload_outbox(const char *server,
                                     const LocalSyncSlot *sync_slot,
                                     const unsigned char *save_data,
                                     size_t save_size,
                                     const char *current_hash,
                                     const char *error,
                                     const char *game_session_id,
                                     long long fencing_token,
                                     const char *request_id)
{
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
        client_log(NULL, "save_upload_outbox_failed", "save_id=%s reason=mkdir", sync_slot->save_id);
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_now);

    char safe_id[96];
    make_safe_outbox_token(sync_slot->save_id, safe_id, sizeof(safe_id));

    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(save_path,
             sizeof(save_path),
             "%s/%s_%ld_%s_rev%d.sav",
             INTEGRAL_SAVE_OUTBOX_DIR,
             timestamp,
             (long)getpid(),
             safe_id,
             sync_slot->revision);
    if (atomic_replace_binary_file(save_path, save_data, save_size) != 0) {
        client_log(NULL, "save_upload_outbox_failed", "save_id=%s reason=write path=%s", sync_slot->save_id, save_path);
        return false;
    }

    char line[768];
    snprintf(line,
             sizeof(line),
             "%s\tserver=%s\tsave_id=%s\texpected_revision=%d\tbytes=%zu\tsha256=%s\tpath=%s\tgame_session_id=%s\tfencing_token=%lld\trequest_id=%s\terror=%s",
             timestamp,
             server,
             sync_slot->save_id,
             sync_slot->revision,
             save_size,
             current_hash,
             save_path,
             game_session_id ? game_session_id : "",
             fencing_token,
             request_id ? request_id : "",
             error ? error : "");
    char record_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(record_path, sizeof(record_path), "%s.pending", save_path);
    if (atomic_replace_binary_file(record_path, (const unsigned char *)line, strlen(line)) != 0) {
        client_log(NULL,
                   "save_upload_outbox_record_failed",
                   "save_id=%s path=%s record=%s",
                   sync_slot->save_id,
                   save_path,
                   record_path);
        return false;
    }
    client_log(NULL,
               "save_upload_outbox_written",
               "save_id=%s path=%s record=%s",
               sync_slot->save_id,
               save_path,
               record_path);
    return true;
}

static bool write_save_recovery_pointer(const char *server,
                                        const LocalSyncSlot *sync_slot,
                                        const char *source_path,
                                        const char *error,
                                        const char *game_session_id,
                                        long long fencing_token)
{
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
        return false;
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_now);
    char safe_id[96];
    make_safe_outbox_token(sync_slot->save_id, safe_id, sizeof(safe_id));
    char record_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(record_path,
             sizeof(record_path),
             "%s/%s_%ld_%s_rev%d.recovery.pending",
             INTEGRAL_SAVE_OUTBOX_DIR,
             timestamp,
             (long)getpid(),
             safe_id,
             sync_slot->revision);
    char line[768];
    snprintf(line,
             sizeof(line),
             "%s\tserver=%s\tsave_id=%s\texpected_revision=%d\tbytes=unknown\tsha256=unknown\tpath=%s\tgame_session_id=%s\tfencing_token=%lld\trequest_id=%s-%ld-%s-%d\terror=%s",
             timestamp,
             server,
             sync_slot->save_id,
             sync_slot->revision,
             source_path,
             game_session_id ? game_session_id : "",
             fencing_token,
             timestamp,
             (long)getpid(),
             safe_id,
             sync_slot->revision,
             error ? error : "recovery_required");
    if (atomic_replace_binary_file(record_path, (const unsigned char *)line, strlen(line)) != 0) {
        client_log(NULL, "save_recovery_record_failed", "save_id=%s source=%s", sync_slot->save_id, source_path);
        return false;
    }
    unsigned char *verified = NULL;
    size_t verified_size = 0;
    bool verified_ok = local_regular_file(source_path) &&
                       read_binary_file_alloc(record_path, &verified, &verified_size, sizeof(line)) == 0 &&
                       verified_size == strlen(line) &&
                       memcmp(verified, line, verified_size) == 0;
    free(verified);
    if (!verified_ok) {
        (void)remove(record_path);
        client_log(NULL,
                   "save_recovery_record_failed",
                   "save_id=%s source=%s reason=readback",
                   sync_slot->save_id,
                   source_path);
        return false;
    }
    client_log(NULL,
               "save_recovery_record_written",
               "save_id=%s source=%s record=%s",
               sync_slot->save_id,
               source_path,
               record_path);
    return true;
}

static bool complete_replayed_outbox_entry(const char *save_path,
                                            const char *record_path,
                                            const char *completion_path,
                                            const char *completion,
                                            size_t completion_size)
{
    if (!save_path || !completion_path) return false;
    if (!local_file_exists(completion_path) &&
        (!completion || completion_size == 0 ||
         atomic_replace_binary_file(completion_path,
                                    (const unsigned char *)completion,
                                    completion_size) != 0)) {
        return false;
    }
    if (remove(save_path) != 0 && errno != ENOENT) return false;
    if (record_path && record_path[0] != '\0' &&
        remove(record_path) != 0 && errno != ENOENT) {
        return false;
    }
    return true;
}

static void replay_save_upload_outbox_line(const char *line,
                                           const char *record_path,
                                           const char *server,
                                           const char *token,
                                           const char *only_save_id,
                                           unsigned *replayed,
                                           unsigned *pending)
{
    char entry_server[160];
    char save_id[96];
    char revision_text[32];
    char request_id[192];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!extract_tsv_field(line, "server", entry_server, sizeof(entry_server)) ||
        !extract_tsv_field(line, "save_id", save_id, sizeof(save_id)) ||
        !extract_tsv_field(line, "expected_revision", revision_text, sizeof(revision_text)) ||
        !extract_tsv_field(line, "path", save_path, sizeof(save_path)) ||
        strcmp(entry_server, server) != 0 ||
        strcmp(save_id, only_save_id) != 0) {
        return;
    }
    if (!extract_tsv_field(line, "request_id", request_id, sizeof(request_id))) {
        request_id[0] = '\0';
    }
    if (!managed_runtime_file_path(save_path) ||
        (make_private_runtime_file(save_path) != 0 && errno != ENOENT)) {
        (*pending)++;
        return;
    }

    char completion_path[INTEGRAL_CONFIG_PATH_MAX];
    int completion_length = snprintf(completion_path,
                                     sizeof(completion_path),
                                     "%s.complete",
                                     save_path);
    if (completion_length < 0 || (size_t)completion_length >= sizeof(completion_path)) {
        (*pending)++;
        return;
    }
    bool upload_completed = local_file_exists(completion_path);
    if (upload_completed && make_private_runtime_file(completion_path) != 0) {
        (*pending)++;
        return;
    }
    if (upload_completed) {
        if (!complete_replayed_outbox_entry(save_path, record_path,
                                            completion_path, NULL, 0)) {
            (*pending)++;
        }
        return;
    }
    unsigned char *save_data = NULL;
    size_t save_size = 0;
    if (read_binary_file_alloc(save_path, &save_data, &save_size, INTEGRAL_MAX_SAVE_BYTES) != 0) {
        (*pending)++;
        return;
    }

    char error[160];
    int next_revision = 0;
    int expected_revision = atoi(revision_text);
    if (integral_api_upload_save_with_request_id(server,
                                            token,
                                            save_id,
                                            expected_revision,
                                            save_data,
                                            save_size,
                                            request_id,
                                            &next_revision,
                                            error,
                                            sizeof(error)) != 0) {
        client_log(NULL, "save_upload_outbox_replay_failed", "save_id=%s error=%s", save_id, error);
        (*pending)++;
        free(save_data);
        return;
    }
    free(save_data);

    char completion[384];
    int completion_size = snprintf(completion,
                                   sizeof(completion),
                                   "save_id=%s\texpected_revision=%d\tnext_revision=%d\trequest_id=%s",
                                   save_id,
                                   expected_revision,
                                   next_revision,
                                   request_id);
    if (completion_size < 0 || (size_t)completion_size >= sizeof(completion) ||
        !complete_replayed_outbox_entry(save_path,
                                        record_path,
                                        completion_path,
                                        completion,
                                        (size_t)completion_size)) {
        client_log(NULL, "save_upload_outbox_completion_failed", "save_id=%s path=%s", save_id, completion_path);
        (*pending)++;
        return;
    }
    char replayed_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(replayed_path, sizeof(replayed_path), "%s/replayed.tsv", INTEGRAL_SAVE_OUTBOX_DIR);
    char replay_line[768];
    snprintf(replay_line,
             sizeof(replay_line),
             "save_id=%s\texpected_revision=%d\tnext_revision=%d\trequest_id=%s",
             save_id,
             expected_revision,
             next_revision,
             request_id);
    (void)append_flushed_text_line(replayed_path, replay_line);
    client_log(NULL, "save_upload_outbox_replayed", "save_id=%s revision=%d", save_id, next_revision);
    (*replayed)++;
}

static unsigned replay_save_upload_outbox(const char *server,
                                          const char *token,
                                          const char *only_save_id,
                                          unsigned *pending_out)
{
    if (pending_out) {
        *pending_out = 0;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
        if (pending_out) *pending_out = 1;
        return 0;
    }
    char manifest_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/outbox.tsv", INTEGRAL_SAVE_OUTBOX_DIR);
    if (make_private_runtime_file(manifest_path) != 0 && errno != ENOENT) {
        if (pending_out) *pending_out = 1;
        return 0;
    }
    FILE *manifest = fopen(manifest_path, "rb");

    unsigned replayed = 0;
    unsigned pending = 0;
    char line[1024];
    if (manifest) {
        while (fgets(line, sizeof(line), manifest)) {
            replay_save_upload_outbox_line(line, NULL, server, token, only_save_id, &replayed, &pending);
        }
        fclose(manifest);
    }

    DIR *dir = opendir(INTEGRAL_SAVE_OUTBOX_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len <= 8 || strcmp(entry->d_name + name_len - 8, ".pending") != 0) {
                continue;
            }
            char record_path[INTEGRAL_CONFIG_PATH_MAX];
            snprintf(record_path, sizeof(record_path), "%s/%s", INTEGRAL_SAVE_OUTBOX_DIR, entry->d_name);
            if (make_private_runtime_file(record_path) != 0) {
                pending++;
                continue;
            }
            unsigned char *record_data = NULL;
            size_t record_size = 0;
            if (read_binary_file_alloc(record_path, &record_data, &record_size, sizeof(line) - 1) != 0) {
                pending++;
                continue;
            }
            memcpy(line, record_data, record_size);
            line[record_size] = '\0';
            free(record_data);
            replay_save_upload_outbox_line(line,
                                           record_path,
                                           server,
                                           token,
                                           only_save_id,
                                           &replayed,
                                           &pending);
        }
        closedir(dir);
    }
    if (pending_out) {
        *pending_out = pending;
    }
    return replayed;
}

static bool resolve_save_upload_outbox(AppState *state, const char *save_id)
{
    unsigned pending = 0;
    unsigned replayed = replay_save_upload_outbox(state->login.server,
                                                   state->login.token,
                                                   save_id,
                                                   &pending);
    if (pending > 0) {
        snprintf(state->login.status,
                 sizeof(state->login.status),
                 "OUTBOX RECOVERY REQUIRED %u",
                 pending);
        client_log(state,
                   "save_upload_outbox_blocked",
                   "save_id=%s pending=%u replayed=%u",
                   save_id,
                   pending,
                   replayed);
        return false;
    }
    if (replayed > 0) {
        snprintf(state->login.status, sizeof(state->login.status), "OUTBOX REPLAYED %u", replayed);
    }
    return true;
}

static bool upload_changed_save(const char *server,
                                const char *token,
                                const char *game_session_id,
                                long long fencing_token,
                                LocalSyncSlot *sync_slot,
                                bool write_outbox_on_failure)
{
    char current_hash[65];
    if (sha256_file_hex(sync_slot->save_path, current_hash, sizeof(current_hash)) != 0) {
        client_log(NULL, "save_upload_hash_failed", "save_id=%s path=%s", sync_slot->save_id, sync_slot->save_path);
        if (!write_outbox_on_failure) {
            return true;
        }
        sync_slot->preserve_save_path =
            write_save_recovery_pointer(server,
                                        sync_slot,
                                        sync_slot->save_path,
                                        "hash_failed",
                                        game_session_id,
                                        fencing_token);
        return sync_slot->preserve_save_path;
    }
    if (strcmp(current_hash, sync_slot->last_hash) == 0) {
        return true;
    }

    unsigned char save_data[INTEGRAL_MAX_SAVE_BYTES];
    size_t save_size = 0;
    if (read_binary_file(sync_slot->save_path, save_data, sizeof(save_data), &save_size) != 0 || save_size == 0) {
        client_log(NULL, "save_upload_read_failed", "save_id=%s path=%s", sync_slot->save_id, sync_slot->save_path);
        if (!write_outbox_on_failure) {
            return true;
        }
        sync_slot->preserve_save_path =
            write_save_recovery_pointer(server,
                                        sync_slot,
                                        sync_slot->save_path,
                                        "read_failed",
                                        game_session_id,
                                        fencing_token);
        return sync_slot->preserve_save_path;
    }

    char error[160];
    int next_revision = 0;
    char request_id[192];
    char safe_id[96];
    make_safe_outbox_token(sync_slot->save_id, safe_id, sizeof(safe_id));
    snprintf(request_id,
             sizeof(request_id),
             "save-%s-r%d-%.24s",
             safe_id,
             sync_slot->revision,
             current_hash);
    if (integral_api_upload_save_fenced(server,
                                   token,
                                   sync_slot->save_id,
                                   sync_slot->revision,
                                   save_data,
                                   save_size,
                                   game_session_id,
                                   fencing_token,
                                   request_id,
                                   &next_revision,
                                   error,
                                   sizeof(error)) == 0) {
        sync_slot->revision = next_revision;
        copy_text(sync_slot->last_hash, sizeof(sync_slot->last_hash), current_hash);
        return true;
    }
    else if (write_outbox_on_failure) {
        return write_save_upload_outbox(server,
                                        sync_slot,
                                        save_data,
                                        save_size,
                                        current_hash,
                                        error,
                                        game_session_id,
                                        fencing_token,
                                        request_id);
    }
    return false;
}

static void make_safe_n64_runtime_save_name(const char *source, char *out, size_t out_size)
{
    size_t used = 0;
    if (out_size == 0) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)source; p && *p && used + 1 < out_size; p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            out[used++] = (char)*p;
        }
        else if (*p == ' ' || *p == '.') {
            out[used++] = '_';
        }
    }
    if (used == 0) {
        copy_text(out, out_size, "n64_save");
    }
    else {
        out[used] = '\0';
    }
}

static bool heartbeat_failure_is_terminal(const char *error)
{
    return error &&
           (strstr(error, "expired") != NULL ||
            strstr(error, "stale") != NULL ||
            strstr(error, "different active game session") != NULL);
}

static bool preserve_heartbeat_lost_recovery_set(const char *server,
                                                 const char *game_session_id,
                                                 long long fencing_token,
                                                 LocalSyncSlot *sync_slots,
                                                 unsigned sync_count)
{
    bool preserved = true;
    for (unsigned i = 0; i < sync_count; i++) {
        sync_slots[i].preserve_save_path =
            write_save_recovery_pointer(server,
                                        &sync_slots[i],
                                        sync_slots[i].save_path,
                                        "heartbeat_lost",
                                        game_session_id,
                                        fencing_token);
        if (!sync_slots[i].preserve_save_path) {
            preserved = false;
        }
    }
    return preserved;
}

static void remove_reflected_session_saves(LocalSyncSlot *sync_slots, unsigned sync_count)
{
    static const char prefix[] = "runtime/";
    for (unsigned i = 0; i < sync_count; i++) {
        if (sync_slots[i].preserve_save_path ||
            strncmp(sync_slots[i].save_path, prefix, sizeof(prefix) - 1u) != 0) {
            continue;
        }
        if (remove(sync_slots[i].save_path) != 0 && errno != ENOENT) {
            client_log(NULL,
                       "session_save_cleanup_failed",
                       "save_id=%s path=%s errno=%d",
                       sync_slots[i].save_id,
                       sync_slots[i].save_path,
                       errno);
            continue;
        }
        char rtc_path[INTEGRAL_CONFIG_PATH_MAX];
        if (snprintf(rtc_path, sizeof(rtc_path), "%s.rtc", sync_slots[i].save_path) > 0) {
            (void)remove(rtc_path);
        }
        client_log(NULL,
                   "session_save_removed",
                   "save_id=%s path=%s",
                   sync_slots[i].save_id,
                   sync_slots[i].save_path);
    }
}

#ifndef _WIN32
static bool child_process_alive(IntegralChildProcess pid)
{
    int status = 0;
    pid_t waited = waitpid(pid, &status, WNOHANG);
    if (waited == pid) {
        return false;
    }
    if (waited == 0) {
        return true;
    }
    if (errno == ECHILD) {
        return !(kill(pid, 0) != 0 && errno == ESRCH);
    }
    return true;
}

static void monitor_save_sync_process(IntegralChildProcess integral_gb_runtime_pid,
                                      const char *server,
                                      const char *token,
                                      const char *game_session_id,
                                      long long fencing_token,
                                      LocalSyncSlot *sync_slots,
                                      unsigned sync_count,
                                      bool game_session_active)
{
    unsigned heartbeat_seconds = 0;
    unsigned heartbeat_failures = 0;
    bool heartbeat_lost = false;
    while (true) {
        bool alive = child_process_alive(integral_gb_runtime_pid);
        if (!heartbeat_lost) {
            for (unsigned i = 0; i < sync_count; i++) {
                upload_changed_save(server, token, game_session_id, fencing_token, &sync_slots[i], false);
            }
        }
        if (game_session_active && alive && !heartbeat_lost) {
            heartbeat_seconds++;
            if (heartbeat_seconds >= INTEGRAL_LOCAL_GAME_HEARTBEAT_SECONDS) {
                char error[160];
                int heartbeat_result =
                    sync_count == 1 && sync_slots[0].mobile_guard
                        ? integral_api_heartbeat_mobile_session(server,
                                                                token,
                                                                sync_slots[0].mobile_session_id,
                                                                game_session_id,
                                                                fencing_token,
                                                                error,
                                                                sizeof(error))
                        : integral_api_heartbeat_game(server,
                                                      token,
                                                      game_session_id,
                                                      fencing_token,
                                                      error,
                                                      sizeof(error));
                if (heartbeat_result != 0) {
                    heartbeat_failures++;
                    client_log(NULL,
                               "local_game_heartbeat_failed",
                               "game_session_id=%s failures=%u error=%s",
                               game_session_id,
                               heartbeat_failures,
                               error);
                    if (heartbeat_failure_is_terminal(error) ||
                        heartbeat_failures >= INTEGRAL_LOCAL_GAME_HEARTBEAT_FAILURE_LIMIT) {
                        heartbeat_lost = true;
                        (void)kill(integral_gb_runtime_pid, SIGTERM);
                        client_log(NULL,
                                   "local_emulator_stop_on_heartbeat_loss",
                                   "pid=%ld game_session_id=%s",
                                   (long)integral_gb_runtime_pid,
                                   game_session_id);
                    }
                }
                else {
                    heartbeat_failures = 0;
                }
                heartbeat_seconds = 0;
            }
        }
        if (!alive) {
            client_log(NULL,
                       "local_emulator_exited_monitor",
                       "pid=%ld game_session_id=%s",
                       (long)integral_gb_runtime_pid,
                       game_session_id);
            if (heartbeat_lost) {
                (void)preserve_heartbeat_lost_recovery_set(server,
                                                           game_session_id,
                                                           fencing_token,
                                                           sync_slots,
                                                           sync_count);
                break;
            }
            bool safe_to_release = true;
            bool mobile_session = sync_count == 1 && sync_slots[0].mobile_guard;
            for (unsigned i = 0; i < sync_count; i++) {
                if (!upload_changed_save(server, token, game_session_id, fencing_token, &sync_slots[i], true)) {
                    safe_to_release = false;
                }
            }
            if (game_session_active && safe_to_release && mobile_session) {
                safe_to_release = complete_mobile_lifecycle(server, token,
                                                            game_session_id,
                                                            fencing_token,
                                                            &sync_slots[0]);
                if (safe_to_release && !sync_slots[0].preserve_save_path) {
                    (void)cleanup_mobile_runtime_directory(sync_slots[0].mobile_runtime_dir);
                }
            }
            else if (game_session_active && safe_to_release) {
                char error[160];
                if (integral_api_stop_local_game(server, token, game_session_id, fencing_token, error, sizeof(error)) != 0) {
                    client_log(NULL,
                               "local_game_stop_failed",
                               "game_session_id=%s error=%s",
                               game_session_id,
                               error);
                }
                else {
                    client_log(NULL,
                               "local_game_stop_ok",
                               "game_session_id=%s",
                               game_session_id);
                    remove_reflected_session_saves(sync_slots, sync_count);
                }
            }
            else if (game_session_active && !safe_to_release) {
                client_log(NULL,
                           "game_stop_withheld_for_recovery",
                           "game_session_id=%s",
                           game_session_id);
            }
            break;
        }
        sleep(1);
    }
}
#endif

#ifdef _WIN32
static DWORD WINAPI monitor_save_sync_thread(LPVOID param)
{
    SaveSyncMonitorArgs *args = (SaveSyncMonitorArgs *)param;
    HANDLE process = (HANDLE)args->process_handle;
    unsigned heartbeat_ticks = 0;
    unsigned heartbeat_failures = 0;
    bool heartbeat_lost = false;
    while (true) {
        DWORD wait_result = WaitForSingleObject(process, 1000);
        if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED) {
            client_log(NULL,
                       "local_emulator_exited_monitor",
                       "game_session_id=%s wait_result=%lu",
                       args->game_session_id,
                       (unsigned long)wait_result);
            if (heartbeat_lost) {
                (void)preserve_heartbeat_lost_recovery_set(args->server,
                                                           args->game_session_id,
                                                           args->fencing_token,
                                                           args->sync_slots,
                                                           args->sync_count);
                break;
            }
            bool safe_to_release = true;
            bool mobile_session = args->sync_count == 1 && args->sync_slots[0].mobile_guard;
            for (unsigned i = 0; i < args->sync_count; i++) {
                if (!upload_changed_save(args->server, args->token, args->game_session_id, args->fencing_token, &args->sync_slots[i], true)) {
                    safe_to_release = false;
                }
            }
            if (args->game_session_active && safe_to_release && mobile_session) {
                safe_to_release = complete_mobile_lifecycle(args->server,
                                                            args->token,
                                                            args->game_session_id,
                                                            args->fencing_token,
                                                            &args->sync_slots[0]);
                if (safe_to_release && !args->sync_slots[0].preserve_save_path) {
                    (void)cleanup_mobile_runtime_directory(args->sync_slots[0].mobile_runtime_dir);
                }
            }
            else if (args->game_session_active && safe_to_release) {
                char error[160];
                if (integral_api_stop_local_game(args->server, args->token, args->game_session_id, args->fencing_token, error, sizeof(error)) != 0) {
                    client_log(NULL,
                               "local_game_stop_failed",
                               "game_session_id=%s error=%s",
                               args->game_session_id,
                               error);
                }
                else {
                    client_log(NULL,
                               "local_game_stop_ok",
                               "game_session_id=%s",
                               args->game_session_id);
                    remove_reflected_session_saves(args->sync_slots, args->sync_count);
                }
            }
            else if (args->game_session_active && !safe_to_release) {
                client_log(NULL,
                           "game_stop_withheld_for_recovery",
                           "game_session_id=%s",
                           args->game_session_id);
            }
            break;
        }
        if (!heartbeat_lost) {
            for (unsigned i = 0; i < args->sync_count; i++) {
                upload_changed_save(args->server, args->token, args->game_session_id, args->fencing_token, &args->sync_slots[i], false);
            }
        }
        if (args->game_session_active && !heartbeat_lost) {
            heartbeat_ticks++;
            if (heartbeat_ticks >= INTEGRAL_LOCAL_GAME_HEARTBEAT_SECONDS) {
                char error[160];
                int heartbeat_result =
                    args->sync_count == 1 && args->sync_slots[0].mobile_guard
                        ? integral_api_heartbeat_mobile_session(args->server,
                                                                args->token,
                                                                args->sync_slots[0].mobile_session_id,
                                                                args->game_session_id,
                                                                args->fencing_token,
                                                                error,
                                                                sizeof(error))
                        : integral_api_heartbeat_game(args->server,
                                                      args->token,
                                                      args->game_session_id,
                                                      args->fencing_token,
                                                      error,
                                                      sizeof(error));
                if (heartbeat_result != 0) {
                    heartbeat_failures++;
                    client_log(NULL,
                               "local_game_heartbeat_failed",
                               "game_session_id=%s failures=%u error=%s",
                               args->game_session_id,
                               heartbeat_failures,
                               error);
                    if (heartbeat_failure_is_terminal(error) ||
                        heartbeat_failures >= INTEGRAL_LOCAL_GAME_HEARTBEAT_FAILURE_LIMIT) {
                        heartbeat_lost = true;
                        (void)TerminateProcess(process, 1);
                        client_log(NULL,
                                   "local_emulator_stop_on_heartbeat_loss",
                                   "game_session_id=%s",
                                   args->game_session_id);
                    }
                }
                else {
                    heartbeat_failures = 0;
                }
                heartbeat_ticks = 0;
            }
        }
    }
    CloseHandle(process);
    free(args);
    return 0;
}

static void start_save_sync_thread(intptr_t process_handle,
                                   const char *server,
                                   const char *token,
                                   const char *game_session_id,
                                   long long fencing_token,
                                   const LocalSyncSlot *sync_slots,
                                   unsigned sync_count,
                                   bool game_session_active)
{
    SaveSyncMonitorArgs *args = calloc(1, sizeof(*args));
    if (!args) {
        CloseHandle((HANDLE)process_handle);
        return;
    }
    args->process_handle = process_handle;
    copy_text(args->server, sizeof(args->server), server);
    copy_text(args->token, sizeof(args->token), token);
    copy_text(args->game_session_id, sizeof(args->game_session_id), game_session_id);
    args->fencing_token = fencing_token;
    args->sync_count = sync_count > INTEGRAL_N64_RUNTIME_SYNC_SLOTS ? INTEGRAL_N64_RUNTIME_SYNC_SLOTS : sync_count;
    args->game_session_active = game_session_active;
    for (unsigned i = 0; i < args->sync_count; i++) {
        args->sync_slots[i] = sync_slots[i];
    }
    HANDLE thread = CreateThread(NULL, 0, monitor_save_sync_thread, args, 0, NULL);
    if (!thread) {
        CloseHandle((HANDLE)process_handle);
        free(args);
        return;
    }
    CloseHandle(thread);
}
#endif

static bool server_rtc_offset_text(AppState *state, char *out, size_t out_size)
{
    long long server_time = 0;
    char error[160];
    if (integral_api_get_server_time(state->login.server, &server_time, error, sizeof(error)) != 0) {
        copy_text(out, out_size, "0");
        snprintf(state->login.status, sizeof(state->login.status), "SERVER RTC LOCAL FALLBACK: %.80s", error);
        client_log(state, "server_rtc_failed", "error=%s", error);
        return true;
    }
    long long local_time = (long long)time(NULL);
    long long offset_seconds = server_time - local_time;
    snprintf(out, out_size, "%lld", offset_seconds);
    client_log(state,
               "server_rtc_offset",
               "server=%lld local=%lld offset_seconds=%lld",
               server_time,
               local_time,
               offset_seconds);
    return true;
}

static bool integral_n64_runtime_paths(char *frontend,
                                    size_t frontend_size,
                                    char *core,
                                    size_t core_size,
                                    char *video,
                                    size_t video_size,
                                    char *audio,
                                    size_t audio_size,
                                    char *input,
                                    size_t input_size,
                                    char *rsp,
                                    size_t rsp_size,
                                    char *data,
                                    size_t data_size)
{
#ifdef _WIN32
    integral_n64_runtime_path(frontend, frontend_size, "build/integral_n64_runtime_frontend.exe");
    integral_n64_runtime_path(core, core_size, "build/prefix/lib/mupen64plus.dll");
    integral_n64_runtime_path(video, video_size, "build/prefix/lib/mupen64plus/mupen64plus-video-GLideN64.dll");
    integral_n64_runtime_path(audio, audio_size, "build/prefix/lib/mupen64plus/mupen64plus-audio-sdl.dll");
    integral_n64_runtime_path(input, input_size, "build/prefix/lib/mupen64plus/mupen64plus-input-sdl.dll");
    integral_n64_runtime_path(rsp, rsp_size, "build/prefix/lib/mupen64plus/mupen64plus-rsp-hle.dll");
#elif defined(__APPLE__)
    integral_n64_runtime_path(frontend, frontend_size, "build/integral_n64_runtime_frontend");
    integral_n64_runtime_path(core, core_size, "build/prefix/lib/libmupen64plus.dylib");
    integral_n64_runtime_path(video, video_size, "build/prefix/lib/mupen64plus/mupen64plus-video-GLideN64.dylib");
    integral_n64_runtime_path(audio, audio_size, "build/prefix/lib/mupen64plus/mupen64plus-audio-sdl.dylib");
    integral_n64_runtime_path(input, input_size, "build/prefix/lib/mupen64plus/mupen64plus-input-sdl.dylib");
    integral_n64_runtime_path(rsp, rsp_size, "build/prefix/lib/mupen64plus/mupen64plus-rsp-hle.dylib");
#else
    integral_n64_runtime_path(frontend, frontend_size, "build/integral_n64_runtime_frontend");
    integral_n64_runtime_path(core, core_size, "build/prefix/lib/libmupen64plus.so.2.0.0");
    integral_n64_runtime_path(video, video_size, "build/prefix/lib/mupen64plus/mupen64plus-video-GLideN64.so");
    integral_n64_runtime_path(audio, audio_size, "build/prefix/lib/mupen64plus/mupen64plus-audio-sdl.so");
    integral_n64_runtime_path(input, input_size, "build/prefix/lib/mupen64plus/mupen64plus-input-sdl.so");
    integral_n64_runtime_path(rsp, rsp_size, "build/prefix/lib/mupen64plus/mupen64plus-rsp-hle.so");
#endif
    integral_n64_runtime_path(data, data_size, "build/prefix/share/mupen64plus");
    return frontend[0] && core[0] && video[0] && audio[0] && input[0] && rsp[0] && data[0];
}

static int n64_key_name_to_scancode(const char *name)
{
    if (!name || name[0] == '\0') {
        return SDL_SCANCODE_UNKNOWN;
    }
    if (strncmp(name, "SCANCODE ", 9) == 0) {
        char *end = NULL;
        long value = strtol(name + 9, &end, 10);
        if (end != name + 9 && *end == '\0' && value > SDL_SCANCODE_UNKNOWN && value < SDL_NUM_SCANCODES) {
            return (int)value;
        }
    }
    struct Alias {
        const char *name;
        SDL_Scancode scancode;
    };
    static const struct Alias aliases[] = {
        {"RETURN", SDL_SCANCODE_RETURN},
        {"ENTER", SDL_SCANCODE_RETURN},
        {"LCTRL", SDL_SCANCODE_LCTRL},
        {"LEFT CTRL", SDL_SCANCODE_LCTRL},
        {"LSHIFT", SDL_SCANCODE_LSHIFT},
        {"LEFT SHIFT", SDL_SCANCODE_LSHIFT},
        {"RSHIFT", SDL_SCANCODE_RSHIFT},
        {"RIGHT SHIFT", SDL_SCANCODE_RSHIFT},
        {"RIGHT", SDL_SCANCODE_RIGHT},
        {"LEFT", SDL_SCANCODE_LEFT},
        {"UP", SDL_SCANCODE_UP},
        {"DOWN", SDL_SCANCODE_DOWN},
    };
    char upper[64];
    copy_text(upper, sizeof(upper), name);
    for (char *p = upper; *p; p++) {
        *p = (char)toupper((unsigned char)*p);
    }
    for (unsigned i = 0; i < sizeof(aliases) / sizeof(aliases[0]); i++) {
        if (strcmp(upper, aliases[i].name) == 0) {
            return aliases[i].scancode;
        }
    }
    SDL_Scancode scancode = SDL_GetScancodeFromName(name);
    if (scancode != SDL_SCANCODE_UNKNOWN) {
        return (int)scancode;
    }
    if (strlen(name) == 1 && isalpha((unsigned char)name[0])) {
        char single[2] = {(char)toupper((unsigned char)name[0]), '\0'};
        scancode = SDL_GetScancodeFromName(single);
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            return (int)scancode;
        }
    }
    return SDL_SCANCODE_UNKNOWN;
}

static bool configured_controller_binding(const char *name)
{
    return strncmp(name, "PAD@", 4) == 0 || strncmp(name, "JOY@", 4) == 0 ||
           strncmp(name, "PAD_", 4) == 0 || strncmp(name, "JOY_", 4) == 0;
}

static uint64_t n64_remote_keyboard_buttons(const AppState *state)
{
    static const unsigned protocol_bits[INTEGRAL_N64_RUNTIME_KEY_BUTTONS] = {
        0, 1, 2, 3, 6, 7, 5, 4, 10, 11, 12, 13, 9, 8, 14, 15, 16, 17,
    };
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    const Uint8 *pressed = SDL_GetKeyboardState(NULL);
    uint64_t buttons = 0;
    key_spec_to_names_count(state->keys.n64_p1, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    for (unsigned index = 0; index < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; index++) {
        if (configured_controller_binding(names[index])) {
            SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(names[index]);
            if (binding != SDLK_UNKNOWN && integral_gb_runtime_key_config_binding_pressed(binding)) {
                buttons |= 1ULL << protocol_bits[index];
            }
            continue;
        }
        int scancode = n64_key_name_to_scancode(names[index]);
        if (scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES && pressed[scancode]) {
            buttons |= 1ULL << protocol_bits[index];
        }
    }
    return buttons;
}

static bool n64_remote_controller_scancode(const AppState *state, SDL_Scancode scancode)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    if (scancode <= SDL_SCANCODE_UNKNOWN || scancode >= SDL_NUM_SCANCODES) return false;
    key_spec_to_names_count(state->keys.n64_p1, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    for (unsigned index = 0; index < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; index++) {
        if (n64_key_name_to_scancode(names[index]) == (int)scancode) return true;
    }
    return false;
}

static bool make_n64_runtime_keymap_spec(const char *spec, char *out, size_t out_size)
{
    Uint32 controller_flags = SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
    bool temporary_controller_scope =
        (SDL_WasInit(controller_flags) & controller_flags) != controller_flags;
    bool result = false;
    if (temporary_controller_scope && SDL_InitSubSystem(controller_flags) != 0) return false;
    (void)integral_gb_runtime_key_config_open_game_controllers();
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    size_t used = 0;
    key_spec_to_names_count(spec, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    int selected_device = -1;
    struct N64Binding {
        int kind;
        int index;
        int direction;
    } bindings[INTEGRAL_N64_RUNTIME_KEY_BUTTONS];
    for (unsigned i = 0; i < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; i++) {
        if (configured_controller_binding(names[i])) {
            SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(names[i]);
            int device = -1;
            if (binding == SDLK_UNKNOWN ||
                !integral_gb_runtime_key_config_binding_to_n64(binding,
                                                               &device,
                                                               &bindings[i].kind,
                                                               &bindings[i].index,
                                                               &bindings[i].direction)) {
                goto done;
            }
            if (selected_device >= 0 && selected_device != device) goto done;
            selected_device = device;
        }
        else {
            int scancode = n64_key_name_to_scancode(names[i]);
            if (scancode <= SDL_SCANCODE_UNKNOWN) goto done;
            bindings[i].kind = 1;
            bindings[i].index = scancode;
            bindings[i].direction = 0;
        }
    }
    int n = snprintf(out, out_size, "%d|", selected_device);
    if (n < 0 || (size_t)n >= out_size) {
        goto done;
    }
    used = (size_t)n;
    for (unsigned i = 0; i < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; i++) {
        n = snprintf(out + used,
                     out_size - used,
                     "%s%d:%d:%d",
                     i == 0 ? "" : ",",
                     bindings[i].kind,
                     bindings[i].index,
                     bindings[i].direction);
        if (n < 0 || (size_t)n >= out_size - used) {
            goto done;
        }
        used += (size_t)n;
    }
    result = true;
done:
    if (temporary_controller_scope) {
        integral_gb_runtime_key_config_close_game_controllers();
        SDL_QuitSubSystem(controller_flags);
    }
    return result;
}

static int prepare_n64_runtime_transfer_slot(AppState *state,
                                           unsigned transfer_slot,
                                           const char *transfer_dir,
                                           IntegralConfigRomSlot *slot,
                                           LocalSyncSlot *sync_slot)
{
    char rom_path[INTEGRAL_CONFIG_PATH_MAX];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(rom_path, sizeof(rom_path), "%s/slot%u.gbc", transfer_dir, transfer_slot + 1);
    snprintf(save_path, sizeof(save_path), "%s/slot%u.sav", transfer_dir, transfer_slot + 1);
    if (sync_slot_from_server(state, slot, save_path, sync_slot) != 0) {
        return -1;
    }
    if (copy_binary_file_limited(slot->rom_path, rom_path, INTEGRAL_MAX_ROM_BYTES) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "TRANSFER SLOT%u PREP FAILED", transfer_slot + 1);
        return -1;
    }
    return 0;
}

static int prepare_n64_runtime_n64_save(AppState *state,
                                       const IntegralConfigRomSlot *selected,
                                       const char *n64_save_dir,
                                       LocalSyncSlot *sync_slot)
{
    if (!selected || selected->save_id[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER N64 ROM FIRST");
        return -1;
    }

    char save_name[96];
    make_safe_n64_runtime_save_name(selected->save_id, save_name, sizeof(save_name));

    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(save_path, sizeof(save_path), "%s/%s.sav", n64_save_dir, save_name);
    if (sync_slot_from_server(state, selected, save_path, sync_slot) != 0) {
        return -1;
    }
    return 0;
}

static void poll_n64_room_host_process(AppState *state)
{
    if (!state || state->n64_runtime_media_host_pid == 0) return;
    int status = 0;
    IntegralChildProcess result = waitpid(state->n64_runtime_media_host_pid, &status, WNOHANG);
    if (result == state->n64_runtime_media_host_pid || result < 0) {
        client_log(state,
                   "n64_room_host_exit",
                   "session=%s status=%d wait_result=%ld no_save=1",
                   state->n64_runtime_media_launched_session_id,
                   status,
                   (long)result);
        if (state->n64_runtime_stop_request_path[0] != '\0') {
            (void)remove(state->n64_runtime_stop_request_path);
        }
        state->n64_runtime_media_host_pid = 0;
        state->n64_runtime_stop_request_path[0] = '\0';
    }
}

static bool download_n64_runtime_save_to_file(AppState *state,
                                              const char *kind,
                                              const char *path)
{
    unsigned char *save_data = malloc(INTEGRAL_MAX_SAVE_BYTES);
    size_t save_size = 0;
    int revision = 0;
    char error[160];
    if (!save_data) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 NO-SAVE OUT OF MEMORY");
        return false;
    }
    int rc = integral_api_download_n64_runtime_save(state->login.server,
                                               state->login.token,
                                               state->n64_runtime_media_session_id,
                                               kind,
                                               save_data,
                                               INTEGRAL_MAX_SAVE_BYTES,
                                               &save_size,
                                               &revision,
                                               error,
                                               sizeof(error));
    if (rc != 0 || save_size == 0 ||
        write_private_runtime_file(path, save_data, save_size) != 0) {
        free(save_data);
        snprintf(state->login.status,
                 sizeof(state->login.status),
                 "N64 NO-SAVE %s FAILED %.80s",
                 kind,
                 rc != 0 ? error : "LOCAL STAGE");
        return false;
    }
    free(save_data);
    client_log(state,
               "n64_runtime_save_staged",
               "session=%s kind=%s revision=%d bytes=%zu no_save=1",
               state->n64_runtime_media_session_id,
               kind,
               revision,
               save_size);
    return true;
}

static bool start_n64_room_host_n64_runtime(AppState *state)
{
    const IntegralConfigRomSlot *n64_slot = registered_rom_slot_at(state, state->n64_room_n64_slot_index);
    const IntegralConfigRomSlot *host_gb_slot = registered_rom_slot_at(state, state->n64_room_user1_gb_slot_index);
    int remote_rom_index = -1;
    const IntegralConfigRomSlot *remote_gb_rom_slot = find_n64_room_host_slot2_rom(state, &remote_rom_index);
    if (n64_room_local_user_index(state) != 0 || !n64_slot || !host_gb_slot ||
        !slot_is_supported_n64(n64_slot) || !slot_is_supported_gb(host_gb_slot) ||
        !state->n64_runtime_media_remote_input_path[0]) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 HOST SELECTION INVALID");
        return false;
    }
    if (!remote_gb_rom_slot) {
        copy_text(state->login.status, sizeof(state->login.status), "USER1 NEEDS USER2 GB VERSION IN ROM1-ROM8");
        return false;
    }

    char frontend[INTEGRAL_CONFIG_PATH_MAX];
    char core[INTEGRAL_CONFIG_PATH_MAX];
    char video[INTEGRAL_CONFIG_PATH_MAX];
    char audio[INTEGRAL_CONFIG_PATH_MAX];
    char input[INTEGRAL_CONFIG_PATH_MAX];
    char rsp[INTEGRAL_CONFIG_PATH_MAX];
    char data[INTEGRAL_CONFIG_PATH_MAX];
    bool runtime_paths_ok = integral_n64_runtime_paths(frontend,
                                 sizeof(frontend),
                                 core,
                                 sizeof(core),
                                 video,
                                 sizeof(video),
                                 audio,
                                 sizeof(audio),
                                 input,
                                 sizeof(input),
                                 rsp,
                                 sizeof(rsp),
                                 data,
                                 sizeof(data));
    int frontend_access = runtime_paths_ok ? integral_runtime_frontend_access(frontend) : -1;
    if (!runtime_paths_ok || frontend_access != 0) {
        client_log(state,
                   "n64_runtime_probe_failed",
                   "home=%s frontend=%s paths_ok=%d access=%d errno=%d",
                   integral_n64_runtime_home_path(),
                   frontend,
                   runtime_paths_ok ? 1 : 0,
                   frontend_access,
                   errno);
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME NOT FOUND");
        return false;
    }

    char session_dir[INTEGRAL_CONFIG_PATH_MAX];
    char transfer_dir[INTEGRAL_CONFIG_PATH_MAX];
    char n64_save_dir[INTEGRAL_CONFIG_PATH_MAX];
    char config_dir[INTEGRAL_CONFIG_PATH_MAX];
    char screenshot_dir[INTEGRAL_CONFIG_PATH_MAX];
    char slot1_rom[INTEGRAL_CONFIG_PATH_MAX];
    char slot2_rom[INTEGRAL_CONFIG_PATH_MAX];
    char slot1_save[INTEGRAL_CONFIG_PATH_MAX];
    char slot2_save[INTEGRAL_CONFIG_PATH_MAX];
    char n64_save[INTEGRAL_CONFIG_PATH_MAX];
    char remote_media_ipc[INTEGRAL_CONFIG_PATH_MAX];
    char stop_request_file[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(session_dir, sizeof(session_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, NULL) ||
        !format_runtime_session_path(transfer_dir, sizeof(transfer_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "transfer") ||
        !format_runtime_session_path(n64_save_dir, sizeof(n64_save_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "n64-save") ||
        !format_runtime_session_path(config_dir, sizeof(config_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "config") ||
        !format_runtime_session_path(screenshot_dir, sizeof(screenshot_dir), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "screenshots") ||
        !format_runtime_session_path(slot1_rom, sizeof(slot1_rom), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "transfer/slot1.gbc") ||
        !format_runtime_session_path(slot2_rom, sizeof(slot2_rom), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "transfer/slot2.gbc") ||
        !format_runtime_session_path(slot1_save, sizeof(slot1_save), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "transfer/slot1.sav") ||
        !format_runtime_session_path(slot2_save, sizeof(slot2_save), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "transfer/slot2.sav") ||
        !format_runtime_session_path(n64_save, sizeof(n64_save), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "n64-save/n64.sav") ||
        !format_runtime_session_path(remote_media_ipc, sizeof(remote_media_ipc), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "remote-media.ipc") ||
        !format_runtime_session_path(stop_request_file, sizeof(stop_request_file), INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                     state->n64_runtime_media_session_id, "stop.request")) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 SESSION PATH INVALID");
        return false;
    }
    if (ensure_private_runtime_directory(transfer_dir) != 0 ||
        ensure_private_runtime_directory(n64_save_dir) != 0 ||
        ensure_private_runtime_directory(config_dir) != 0 ||
        ensure_private_runtime_directory(screenshot_dir) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 NO-SAVE RUNTIME CREATE FAILED");
        return false;
    }
    if (remove(stop_request_file) != 0 && errno != ENOENT) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 STOP IPC PREP FAILED");
        return false;
    }
    if (copy_binary_file_limited(host_gb_slot->rom_path, slot1_rom, INTEGRAL_MAX_ROM_BYTES) != 0 ||
        copy_binary_file_limited(remote_gb_rom_slot->rom_path, slot2_rom, INTEGRAL_MAX_ROM_BYTES) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 HOST GB ROM STAGE FAILED");
        return false;
    }
    client_log(state,
               "n64_transfer_roms_staged",
               "session=%s slot1=ROM%d slot2=ROM%d remote_game_type=%s rom_transfer=disabled",
               state->n64_runtime_media_session_id,
               state->n64_room_user1_gb_slot_index + 1,
               remote_rom_index + 1,
               state->current_room.slot_game_type2);
    if (!download_n64_runtime_save_to_file(state, "n64", n64_save) ||
        !download_n64_runtime_save_to_file(state, "host-gb", slot1_save) ||
        !download_n64_runtime_save_to_file(state, "remote-gb", slot2_save)) {
        return false;
    }

    char controller_map[512];
    if (!make_n64_runtime_keymap_spec(state->keys.n64_p1,
                                    controller_map,
                                    sizeof(controller_map))) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 KEY CONFIG INVALID");
        return false;
    }

#ifdef _WIN32
    IntegralChildProcess spawned = _spawnl(_P_NOWAIT,
                                      frontend,
                                      frontend,
                                      "--rom",
                                      n64_slot->rom_path,
                                      "--core",
                                      core,
                                      "--config-dir",
                                      config_dir,
                                      "--data-dir",
                                      data,
                                      "--screenshot-dir",
                                      screenshot_dir,
                                      "--save-dir",
                                      n64_save_dir,
                                      "--save-name",
                                      "n64",
                                      "--video",
                                      video,
                                      "--audio",
                                      audio,
                                      "--input",
                                      input,
                                      "--rsp",
                                      rsp,
                                      "--transfer-storage",
                                      transfer_dir,
                                      "--controller1",
                                      "keyboard",
                                      "--controller-map1",
                                      controller_map,
                                      "--remote-input-file",
                                      state->n64_runtime_media_remote_input_path,
                                      "--remote-media-file",
                                      remote_media_ipc,
                                      "--stop-request-file",
                                      stop_request_file,
                                      "--interactive",
                                      NULL);
    if (spawned == -1) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 HOST START FAILED");
        return false;
    }
#else
    pid_t spawned = fork();
    if (spawned < 0) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 HOST START FAILED");
        return false;
    }
    if (spawned == 0) {
        redirect_child_output_to_client_log();
        execl(frontend,
              frontend,
              "--rom",
              n64_slot->rom_path,
              "--core",
              core,
              "--config-dir",
              config_dir,
              "--data-dir",
              data,
              "--screenshot-dir",
              screenshot_dir,
              "--save-dir",
              n64_save_dir,
              "--save-name",
              "n64",
              "--video",
              video,
              "--audio",
              audio,
              "--input",
              input,
              "--rsp",
              rsp,
              "--transfer-storage",
              transfer_dir,
              "--controller1",
              "keyboard",
              "--controller-map1",
              controller_map,
              "--remote-input-file",
              state->n64_runtime_media_remote_input_path,
              "--remote-media-file",
              remote_media_ipc,
              "--stop-request-file",
              stop_request_file,
              "--interactive",
              (char *)NULL);
        _exit(127);
    }
#endif
    state->n64_runtime_media_host_pid = spawned;
    copy_text(state->n64_runtime_media_ipc_path,
              sizeof(state->n64_runtime_media_ipc_path),
              remote_media_ipc);
    copy_text(state->n64_runtime_stop_request_path,
              sizeof(state->n64_runtime_stop_request_path),
              stop_request_file);
    copy_text(state->n64_runtime_media_launched_session_id,
              sizeof(state->n64_runtime_media_launched_session_id),
              state->n64_runtime_media_session_id);
    copy_text(state->login.status,
              sizeof(state->login.status),
              "N64 HOST STARTED  NO SAV OVERWRITE");
    client_log(state,
               "n64_room_host_started",
               "session=%s pid=%ld transfer_slots=2 no_save=1",
               state->n64_runtime_media_session_id,
               (long)spawned);
    return true;
}

static bool maybe_start_n64_room_host_n64_runtime(AppState *state, Uint32 now)
{
    if (!state || !state->n64_runtime_media_paired || strcmp(state->n64_runtime_media_role, "host") != 0) {
        return true;
    }
    if (strcmp(state->n64_runtime_media_launched_session_id, state->n64_runtime_media_session_id) == 0) {
        if (!state->n64_runtime_media_ipc_path[0] &&
            runtime_session_id_is_path_safe(state->n64_runtime_media_session_id)) {
            (void)format_runtime_session_path(state->n64_runtime_media_ipc_path,
                                              sizeof(state->n64_runtime_media_ipc_path),
                                              INTEGRAL_N64_RUNTIME_MEDIA_DIR,
                                              state->n64_runtime_media_session_id,
                                              "remote-media.ipc");
        }
        return true;
    }
    if (state->n64_runtime_media_host_pid != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 HOST PROCESS ALREADY RUNNING");
        return false;
    }
    if (state->n64_runtime_media_host_launch_retry_after_ticks != 0 &&
        (Sint32)(now - state->n64_runtime_media_host_launch_retry_after_ticks) < 0) {
        return false;
    }
    if (!start_n64_room_host_n64_runtime(state)) {
        client_log(state,
                   "n64_room_host_start_failed",
                   "session=%s status=%s",
                   state->n64_runtime_media_session_id,
                   state->login.status);
        state->n64_runtime_media_host_launch_retry_after_ticks = now + 5000u;
        return false;
    }
    state->n64_runtime_media_host_launch_retry_after_ticks = 0;
    return true;
}

static void start_local_n64_runtime(AppState *state)
{
    if (state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        return;
    }
    if (!refresh_rom_slots_from_server(state)) {
        return;
    }
    const IntegralConfigRomSlot *n64_slot = selected_n64_rom_slot(state);
    if (!n64_slot) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 ROM REQUIRED");
        return;
    }
    if (n64_slot->save_id[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER N64 ROM FIRST");
        return;
    }

    char frontend[INTEGRAL_CONFIG_PATH_MAX];
    char core[INTEGRAL_CONFIG_PATH_MAX];
    char video[INTEGRAL_CONFIG_PATH_MAX];
    char audio[INTEGRAL_CONFIG_PATH_MAX];
    char input[INTEGRAL_CONFIG_PATH_MAX];
    char rsp[INTEGRAL_CONFIG_PATH_MAX];
    char data[INTEGRAL_CONFIG_PATH_MAX];
    if (!integral_n64_runtime_paths(frontend,
                                 sizeof(frontend),
                                 core,
                                 sizeof(core),
                                 video,
                                 sizeof(video),
                                 audio,
                                 sizeof(audio),
                                 input,
                                 sizeof(input),
                                 rsp,
                                 sizeof(rsp),
                                 data,
                                 sizeof(data)) ||
        integral_runtime_frontend_access(frontend) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME NOT FOUND");
        return;
    }
    const char *save_ids[INTEGRAL_N64_RUNTIME_SYNC_SLOTS];
    unsigned save_id_count = 0;
    save_ids[save_id_count++] = n64_slot->save_id;
    for (unsigned i = 0; i < 4; i++) {
        const IntegralConfigRomSlot *selected = selected_transfer_rom_slot(state, i);
        if (selected) {
            save_ids[save_id_count++] = selected->save_id;
        }
    }
    for (unsigned i = 0; i < save_id_count; i++) {
        if (!resolve_save_upload_outbox(state, save_ids[i])) {
            client_log(state,
                       "n64_runtime_start_blocked",
                       "reason=outbox_recovery save_id=%s",
                       save_ids[i]);
            return;
        }
    }

    char error[160];
    char game_session_id[96];
    long long fencing_token = 0;
    if (integral_api_start_game_with_saves(state->login.server,
                                      state->login.token,
                                      INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT,
                                      save_ids,
                                      save_id_count,
                                      game_session_id,
                                      sizeof(game_session_id),
                                      &fencing_token,
                                      error,
                                      sizeof(error)) != 0) {
        client_log(state,
                   "n64_runtime_lock_failed",
                   "execution_mode=%s save_count=%u error=%s",
                   INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT,
                   save_id_count,
                   error);
        snprintf(state->login.status, sizeof(state->login.status), "N64_RUNTIME LOCK FAILED %s", error);
        return;
    }
    bool game_lock_acquired = true;
    if (!runtime_session_id_is_path_safe(game_session_id)) {
        (void)integral_api_stop_local_game(state->login.server, state->login.token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME SESSION ID INVALID");
        return;
    }

    char integral_n64_runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_transfer_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_n64_save_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_config_dir[INTEGRAL_CONFIG_PATH_MAX];
    char integral_n64_runtime_screenshot_dir[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(integral_n64_runtime_dir, sizeof(integral_n64_runtime_dir),
                                     "runtime/n64_runtime", game_session_id, NULL) ||
        !format_runtime_session_path(integral_n64_runtime_transfer_dir, sizeof(integral_n64_runtime_transfer_dir),
                                     "runtime/n64_runtime", game_session_id, "transfer") ||
        !format_runtime_session_path(integral_n64_runtime_n64_save_dir, sizeof(integral_n64_runtime_n64_save_dir),
                                     "runtime/n64_runtime", game_session_id, "n64-save") ||
        !format_runtime_session_path(integral_n64_runtime_config_dir, sizeof(integral_n64_runtime_config_dir),
                                     "runtime/n64_runtime", game_session_id, "config") ||
        !format_runtime_session_path(integral_n64_runtime_screenshot_dir, sizeof(integral_n64_runtime_screenshot_dir),
                                     "runtime/n64_runtime", game_session_id, "screenshots")) {
        (void)integral_api_stop_local_game(state->login.server, state->login.token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME SESSION PATH INVALID");
        return;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/n64_runtime") != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_transfer_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_n64_save_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_config_dir) != 0 ||
        ensure_private_runtime_directory(integral_n64_runtime_screenshot_dir) != 0) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME RUNTIME CREATE FAILED");
        return;
    }

    LocalSyncSlot sync_slots[INTEGRAL_N64_RUNTIME_SYNC_SLOTS];
    memset(sync_slots, 0, sizeof(sync_slots));
    unsigned sync_count = 0;
    if (prepare_n64_runtime_n64_save(state, n64_slot, integral_n64_runtime_n64_save_dir, &sync_slots[sync_count]) != 0) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        return;
    }
    char n64_save_name[96];
    make_safe_n64_runtime_save_name(n64_slot->save_id, n64_save_name, sizeof(n64_save_name));
    sync_count++;
    for (unsigned i = 0; i < 4; i++) {
        const IntegralConfigRomSlot *selected = selected_transfer_rom_slot(state, i);
        if (!selected) {
            continue;
        }
        IntegralConfigRomSlot slot = *selected;
        if (prepare_n64_runtime_transfer_slot(state, i, integral_n64_runtime_transfer_dir, &slot, &sync_slots[sync_count]) != 0) {
            integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
            return;
        }
        sync_count++;
    }

    char controller_map[512];
    if (!make_n64_runtime_keymap_spec(state->keys.n64_p1, controller_map, sizeof(controller_map))) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "N64 KEY CONFIG INVALID");
        return;
    }

#ifdef _WIN32
    intptr_t spawned = _spawnl(_P_NOWAIT,
                               frontend,
                               frontend,
                               "--rom",
                               n64_slot->rom_path,
                               "--core",
                               core,
                               "--config-dir",
                               integral_n64_runtime_config_dir,
                               "--data-dir",
                               data,
                               "--screenshot-dir",
                               integral_n64_runtime_screenshot_dir,
                               "--save-dir",
                               integral_n64_runtime_n64_save_dir,
                               "--save-name",
                               n64_save_name,
                               "--video",
                               video,
                               "--audio",
                               audio,
                               "--input",
                               input,
                               "--rsp",
                               rsp,
                               "--transfer-storage",
                               integral_n64_runtime_transfer_dir,
                               "--controller1",
                               "keyboard",
                               "--controller-map1",
                               controller_map,
                               "--interactive",
                               NULL);
    if (spawned == -1) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME START FAILED");
        client_log(state, "n64_runtime_spawn_failed", "errno=%d", errno);
        return;
    }
    start_save_sync_thread(spawned, state->login.server, state->login.token, game_session_id, fencing_token, sync_slots, sync_count, game_lock_acquired);
#else
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "N64_RUNTIME START FAILED");
        return;
    }
    if (monitor_pid == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
            _exit(127);
        }
        if (pid == 0) {
            redirect_child_output_to_client_log();
            execl(frontend,
                  frontend,
                  "--rom",
                  n64_slot->rom_path,
                  "--core",
                  core,
                  "--config-dir",
                  integral_n64_runtime_config_dir,
                  "--data-dir",
                  data,
                  "--screenshot-dir",
                  integral_n64_runtime_screenshot_dir,
                  "--save-dir",
                  integral_n64_runtime_n64_save_dir,
                  "--save-name",
                  n64_save_name,
                  "--video",
                  video,
                  "--audio",
                  audio,
                  "--input",
                  input,
                  "--rsp",
                  rsp,
                  "--transfer-storage",
                  integral_n64_runtime_transfer_dir,
                  "--controller1",
                  "keyboard",
                  "--controller-map1",
                  controller_map,
                  "--interactive",
                  (char *)NULL);
            _exit(127);
        }
        monitor_save_sync_process(pid, state->login.server, state->login.token, game_session_id, fencing_token, sync_slots, sync_count, game_lock_acquired);
        _exit(0);
    }
#endif
    snprintf(state->login.status, sizeof(state->login.status), "N64_RUNTIME STARTED TP:%u", sync_count - 1u);
    client_log(state, "n64_runtime_started", "rom=%s transfer_slots=%u n64_save_id=%s",
               n64_slot->rom_path,
               sync_count - 1u,
               n64_slot->save_id);
}

static void start_local_gb_mobile(AppState *state)
{
    const IntegralConfigRomSlot *selected = local_selected_rom_slot(state, 0);
    if (!selected) {
        copy_text(state->login.status, sizeof(state->login.status), "SLOT1 ROM REQUIRED");
        return;
    }
    if (selected->save_id[0] == '\0' || state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER SLOT1 AND LOGIN FIRST");
        return;
    }
    if (state->mobile_scenario_count == 0u ||
        state->mobile_scenario_selected >= state->mobile_scenario_count ||
        strcmp(state->mobile_scenario_rom_id, selected->rom_id) != 0 ||
        strcmp(state->mobile_scenario_save_id, selected->save_id) != 0) {
        refresh_mobile_scenarios(state);
        if (state->mobile_scenario_count == 0u) {
            return;
        }
    }
    const IntegralApiMobileScenario *scenario =
        &state->mobile_scenarios[state->mobile_scenario_selected];
    if (access(integral_gb_runtime_mobile_runtime_path(), X_OK) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "GB MOBILE RUNTIME NOT FOUND");
        return;
    }
    if (!resolve_save_upload_outbox(state, selected->save_id)) {
        return;
    }

    char error[160];
    char mobile_session_id[96];
    char game_session_id[96];
    char create_request_id[160];
    char create_request_path[INTEGRAL_CONFIG_PATH_MAX];
    char safe_save_id[96];
    char safe_rom_id[96];
    char safe_scenario_id[96];
    make_safe_outbox_token(selected->save_id, safe_save_id, sizeof(safe_save_id));
    make_safe_outbox_token(selected->rom_id, safe_rom_id, sizeof(safe_rom_id));
    make_safe_outbox_token(scenario->scenario_id, safe_scenario_id, sizeof(safe_scenario_id));
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/gb-mobile-create") != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE CREATE OUTBOX FAILED");
        return;
    }
    snprintf(create_request_path, sizeof(create_request_path),
             "runtime/gb-mobile-create/%s-%s-%s.txt",
             safe_save_id, safe_rom_id, safe_scenario_id);
    unsigned char *request_bytes = NULL;
    size_t request_size = 0;
    if (read_binary_file_alloc(create_request_path, &request_bytes, &request_size,
                               sizeof(create_request_id) - 1u) == 0 && request_size > 0u) {
        memcpy(create_request_id, request_bytes, request_size);
        create_request_id[request_size] = '\0';
        free(request_bytes);
    }
    else {
        free(request_bytes);
        snprintf(create_request_id, sizeof(create_request_id),
                 "mobile-create:%lld-%ld-%s",
                 (long long)time(NULL), (long)getpid(), safe_scenario_id);
        if (atomic_replace_binary_file(
                create_request_path,
                (const unsigned char *)create_request_id,
                strlen(create_request_id)) != 0) {
            copy_text(state->login.status, sizeof(state->login.status), "MOBILE CREATE OUTBOX FAILED");
            return;
        }
    }
    IntegralMobileRuntimeContract runtime_contract;
    long long fencing_token = 0;
    int create_result = integral_api_start_mobile_session_contract(state->login.server,
                                          state->login.token,
                                          selected->save_id,
                                          selected->rom_id,
                                          create_request_id,
                                          scenario->scenario_id,
                                          mobile_session_id,
                                          sizeof(mobile_session_id),
                                          game_session_id,
                                          sizeof(game_session_id),
                                          &fencing_token,
                                          &runtime_contract,
                                          error,
                                          sizeof(error));
    if (create_result != 0) {
        if (create_result == INTEGRAL_API_MOBILE_CREATE_ABORTED) {
            remove(create_request_path);
        }
        if (create_result == INTEGRAL_API_MOBILE_CREATE_AUTH_SESSION_CONFLICT) {
            copy_text(state->login.status, sizeof(state->login.status),
                      "MOBILE SESSION BELONGS TO ANOTHER LOGIN - WAIT FOR EXPIRY");
        }
        else if (create_result == INTEGRAL_API_MOBILE_CREATE_ABORTED) {
            copy_text(state->login.status, sizeof(state->login.status),
                      "MOBILE CREATE ABORTED - RETRY START");
        }
        else {
            snprintf(state->login.status, sizeof(state->login.status), "MOBILE LOCK FAILED %s", error);
        }
        return;
    }
    if (!runtime_session_id_is_path_safe(mobile_session_id) ||
        !runtime_session_id_is_path_safe(game_session_id)) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        (void)integral_api_cancel_mobile_session(state->login.server,
                                                 state->login.token,
                                                 mobile_session_id,
                                                 game_session_id,
                                                 fencing_token,
                                                 "invalid session identifier",
                                                 error,
                                                 sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE SESSION ID INVALID");
        return;
    }
    if (remove(create_request_path) != 0 && errno != ENOENT) {
        client_log(state, "mobile_create_outbox_cleanup_failed", "path=%s", create_request_path);
    }

    char runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    char config_path[INTEGRAL_CONFIG_PATH_MAX];
    char result_path[INTEGRAL_CONFIG_PATH_MAX];
    char manifest_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(runtime_dir, sizeof(runtime_dir),
                                     "runtime/gb-mobile", mobile_session_id, NULL) ||
        !format_runtime_session_path(save_path, sizeof(save_path),
                                     "runtime/gb-mobile", mobile_session_id, "working.sav") ||
        !format_runtime_session_path(config_path, sizeof(config_path),
                                     "runtime/gb-mobile", mobile_session_id, "adapter.bin") ||
        !format_runtime_session_path(result_path, sizeof(result_path),
                                     "runtime/gb-mobile", mobile_session_id, "runtime-result.txt") ||
        !format_runtime_session_path(manifest_path, sizeof(manifest_path),
                                     "runtime/gb-mobile", mobile_session_id, "session.manifest")) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        (void)integral_api_cancel_mobile_session(state->login.server, state->login.token,
                                                 mobile_session_id, game_session_id, fencing_token,
                                                 "runtime path invalid", error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE SESSION PATH INVALID");
        return;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/gb-mobile") != 0 ||
        ensure_private_runtime_directory(runtime_dir) != 0) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        integral_api_cancel_mobile_session(state->login.server, state->login.token, mobile_session_id, game_session_id, fencing_token, "runtime directory create failed", error, sizeof(error));
        (void)cleanup_mobile_runtime_directory(runtime_dir);
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE RUNTIME CREATE FAILED");
        return;
    }

    IntegralConfigRomSlot slot = *selected;
    LocalSyncSlot sync_slot;
    memset(&sync_slot, 0, sizeof(sync_slot));
    if (sync_slot_from_server(state, &slot, save_path, &sync_slot) != 0 ||
        integral_mobile_runtime_contract_write_manifest(
            &runtime_contract, runtime_dir, manifest_path, error, sizeof(error)) != 0 ||
        strcmp(sync_slot.save_path, save_path) != 0) {
        integral_mobile_runtime_contract_free(&runtime_contract);
        integral_api_cancel_mobile_session(state->login.server, state->login.token, mobile_session_id, game_session_id, fencing_token, "working SAV write failed", error, sizeof(error));
        (void)cleanup_mobile_runtime_directory(runtime_dir);
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE SAV WRITE FAILED");
        return;
    }
    copy_text(sync_slot.mobile_session_id, sizeof(sync_slot.mobile_session_id), mobile_session_id);
    unsigned char *authoritative = NULL;
    size_t authoritative_size = 0;
    if (read_binary_file_alloc(save_path, &authoritative, &authoritative_size, INTEGRAL_MAX_SAVE_BYTES) != 0 ||
        authoritative_size == 0u) {
        free(authoritative);
        integral_mobile_runtime_contract_free(&runtime_contract);
        integral_api_cancel_mobile_session(state->login.server, state->login.token, mobile_session_id, game_session_id, fencing_token, "authoritative SAV invalid", error, sizeof(error));
        (void)cleanup_mobile_runtime_directory(runtime_dir);
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE SAV SIZE INVALID");
        return;
    }
    sync_slot.mobile_guard = true;
    sync_slot.authoritative_size = authoritative_size;
    copy_text(sync_slot.mobile_runtime_dir, sizeof(sync_slot.mobile_runtime_dir), runtime_dir);
    free(authoritative);
    integral_mobile_runtime_contract_free(&runtime_contract);
    (void)remove(result_path);

    const char *runtime = integral_gb_runtime_mobile_runtime_path();
#ifdef _WIN32
    intptr_t spawned = _spawnl(_P_NOWAIT,
                               runtime, runtime,
                               "--rom", slot.rom_path,
                               "--save", save_path,
                               "--config", config_path,
                               "--session-manifest", manifest_path,
                               "--runtime-result", result_path,
                               NULL);
    if (spawned == -1) {
        integral_api_cancel_mobile_session(state->login.server, state->login.token, mobile_session_id, game_session_id, fencing_token, "runtime start failed", error, sizeof(error));
        (void)cleanup_mobile_runtime_directory(runtime_dir);
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE START FAILED");
        return;
    }
    start_save_sync_thread(spawned, state->login.server, state->login.token, game_session_id, fencing_token, &sync_slot, 1, true);
#else
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        integral_api_cancel_mobile_session(state->login.server, state->login.token, mobile_session_id, game_session_id, fencing_token, "monitor start failed", error, sizeof(error));
        (void)cleanup_mobile_runtime_directory(runtime_dir);
        copy_text(state->login.status, sizeof(state->login.status), "MOBILE START FAILED");
        return;
    }
    if (monitor_pid == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            integral_api_cancel_mobile_session(state->login.server, state->login.token, mobile_session_id, game_session_id, fencing_token, "runtime fork failed", error, sizeof(error));
            _exit(127);
        }
        if (pid == 0) {
            redirect_child_output_to_client_log();
            execl(runtime, runtime,
                  "--rom", slot.rom_path,
                  "--save", save_path,
                  "--config", config_path,
                  "--session-manifest", manifest_path,
                  "--runtime-result", result_path,
                  (char *)NULL);
            _exit(127);
        }
        monitor_save_sync_process(pid, state->login.server, state->login.token, game_session_id, fencing_token, &sync_slot, 1, true);
        _exit(0);
    }
#endif
    copy_text(state->login.status, sizeof(state->login.status), "MOBILE MODE STARTED");
    client_log(state,
               "gb_mobile_started",
               "mobile_session=%s game_session=%s scenario=%s",
               mobile_session_id,
               game_session_id,
               scenario->scenario_id);
}

static void start_local_gb_runtime(AppState *state)
{
    const IntegralConfigRomSlot *selected_slot1 = local_selected_rom_slot(state, 0);
    const IntegralConfigRomSlot *selected_slot2 = local_selected_rom_slot(state, 1);
    if (!selected_slot1) {
        copy_text(state->login.status, sizeof(state->login.status), "SLOT1 ROM REQUIRED");
        return;
    }
    if (access(integral_gb_runtime_server_path(), X_OK) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "GB_RUNTIME SERVER NOT FOUND");
        return;
    }
    if (state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        return;
    }
    if (selected_slot1->save_id[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER SLOT1 FIRST");
        return;
    }
    if (selected_slot2 && selected_slot2->save_id[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER SLOT2 FIRST");
        return;
    }
    if (!resolve_save_upload_outbox(state, selected_slot1->save_id) ||
        (selected_slot2 && !resolve_save_upload_outbox(state, selected_slot2->save_id))) {
        client_log(state, "local_start_blocked", "reason=outbox_recovery");
        return;
    }
    char error[160];
    char game_session_id[96];
    long long fencing_token = 0;
    if (integral_api_start_local_game(state->login.server,
                                 state->login.token,
                                 selected_slot1->save_id,
                                 selected_slot2 ? selected_slot2->save_id : NULL,
                                 game_session_id,
                                 sizeof(game_session_id),
                                 &fencing_token,
                                 error,
                                 sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "LOCAL GAME LOCK FAILED %s", error);
        return;
    }
    if (!runtime_session_id_is_path_safe(game_session_id)) {
        (void)integral_api_stop_local_game(state->login.server, state->login.token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "GB_RUNTIME SESSION ID INVALID");
        return;
    }
    IntegralConfigRomSlot slot1 = *selected_slot1;
    IntegralConfigRomSlot slot2;
    memset(&slot2, 0, sizeof(slot2));
    if (selected_slot2) {
        slot2 = *selected_slot2;
    }
    char runtime_dir[INTEGRAL_CONFIG_PATH_MAX];
    char slot1_save_path[INTEGRAL_CONFIG_PATH_MAX];
    char slot2_save_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!format_runtime_session_path(runtime_dir, sizeof(runtime_dir),
                                     "runtime/gb-sessions", game_session_id, NULL) ||
        !format_runtime_session_path(slot1_save_path, sizeof(slot1_save_path),
                                     "runtime/gb-sessions", game_session_id, "slot1.sav") ||
        !format_runtime_session_path(slot2_save_path, sizeof(slot2_save_path),
                                     "runtime/gb-sessions", game_session_id, "slot2.sav")) {
        (void)integral_api_stop_local_game(state->login.server, state->login.token,
                                           game_session_id, fencing_token,
                                           error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "GB_RUNTIME SESSION PATH INVALID");
        return;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory("runtime/gb-sessions") != 0 ||
        ensure_private_runtime_directory(runtime_dir) != 0) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "GB_RUNTIME SESSION CREATE FAILED");
        return;
    }

    LocalSyncSlot sync_slots[2];
    memset(sync_slots, 0, sizeof(sync_slots));
    unsigned sync_count = 0;
    if (sync_slot_from_server(state, &slot1, slot1_save_path, &sync_slots[sync_count]) != 0) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        return;
    }
    sync_count++;
    if (selected_slot2) {
        if (sync_slot_from_server(state, &slot2, slot2_save_path, &sync_slots[sync_count]) != 0) {
            integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
            return;
        }
        sync_count++;
    }
    char rtc_offset_text[32];
    if (!server_rtc_offset_text(state, rtc_offset_text, sizeof(rtc_offset_text))) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        return;
    }

#ifdef _WIN32
    intptr_t spawned;
    if (selected_slot2) {
        spawned = _spawnl(_P_NOWAIT,
                          integral_gb_runtime_server_path(),
                          integral_gb_runtime_server_path(),
                          "--rom1",
                          slot1.rom_path,
                          "--save1",
                          slot1_save_path,
                          "--rom2",
                          slot2.rom_path,
                          "--save2",
                          slot2_save_path,
                          "--bind",
                          "0.0.0.0",
                          "--port",
                          INTEGRAL_GB_RUNTIME_PORT,
                          "--rtc-offset-seconds",
                          rtc_offset_text,
                          "--display",
                          "--remote-input",
                          "--display-slots",
                          "2",
                          "--slot1-keys",
                          state->keys.slot1,
                          "--slot2-keys",
                          state->keys.slot2,
                          "--fast-key",
                          state->keys.fast,
                          "--screenshot-key",
                          state->keys.screenshot,
                          "--escape-key",
                          state->keys.escape,
                          "--turbo-hold-key",
                          state->keys.turbo_hold,
                          "--reset-key",
                          state->keys.reset,
                          "--audio",
                          NULL);
    }
    else {
        spawned = _spawnl(_P_NOWAIT,
                          integral_gb_runtime_server_path(),
                          integral_gb_runtime_server_path(),
                          "--rom1",
                          slot1.rom_path,
                          "--save1",
                          slot1_save_path,
                          "--self",
                          "--rtc-offset-seconds",
                          rtc_offset_text,
                          "--display",
                          "--display-slots",
                          "1",
                          "--slot1-keys",
                          state->keys.slot1,
                          "--fast-key",
                          state->keys.fast,
                          "--screenshot-key",
                          state->keys.screenshot,
                          "--escape-key",
                          state->keys.escape,
                          "--turbo-hold-key",
                          state->keys.turbo_hold,
                          "--reset-key",
                          state->keys.reset,
                          "--audio",
                          NULL);
    }
    if (spawned == -1) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "GB_RUNTIME START FAILED");
        client_log(state, "gb_runtime_server_spawn_failed", "errno=%d", errno);
        return;
    }
    copy_text(state->login.status,
              sizeof(state->login.status),
              selected_slot2 ? "GB_RUNTIME SERVER2 STARTED" : "GB_RUNTIME LOCAL STARTED");
    client_log(state, "gb_runtime_server_started", "pid=%ld", (long)spawned);
    start_save_sync_thread(spawned, state->login.server, state->login.token, game_session_id, fencing_token, sync_slots, sync_count, true);
    return;
#else
    pid_t monitor_pid = fork();
    if (monitor_pid < 0) {
        integral_api_stop_local_game(state->login.server, state->login.token, game_session_id, fencing_token, error, sizeof(error));
        copy_text(state->login.status, sizeof(state->login.status), "GB_RUNTIME START FAILED");
        return;
    }
    if (monitor_pid == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            integral_api_stop_local_game(state->login.server,
                                    state->login.token,
                                    game_session_id,
                                    fencing_token,
                                    error,
                                    sizeof(error));
            _exit(127);
        }
        if (pid == 0) {
            if (selected_slot2) {
                execl(integral_gb_runtime_server_path(),
                      integral_gb_runtime_server_path(),
                      "--rom1",
                      slot1.rom_path,
                      "--save1",
                      slot1_save_path,
                      "--rom2",
                      slot2.rom_path,
                      "--save2",
                      slot2_save_path,
                      "--bind",
                      "0.0.0.0",
                      "--port",
                      INTEGRAL_GB_RUNTIME_PORT,
                      "--rtc-offset-seconds",
                      rtc_offset_text,
                      "--display",
                      "--remote-input",
                      "--display-slots",
                      "2",
                      "--slot1-keys",
                      state->keys.slot1,
                      "--slot2-keys",
                      state->keys.slot2,
                      "--fast-key",
                      state->keys.fast,
                      "--screenshot-key",
                      state->keys.screenshot,
                      "--escape-key",
                      state->keys.escape,
                      "--turbo-hold-key",
                      state->keys.turbo_hold,
                      "--reset-key",
                      state->keys.reset,
                      "--audio",
                      (char *)NULL);
            }
            else {
                execl(integral_gb_runtime_server_path(),
                      integral_gb_runtime_server_path(),
                      "--rom1",
                      slot1.rom_path,
                      "--save1",
                      slot1_save_path,
                      "--self",
                      "--rtc-offset-seconds",
                      rtc_offset_text,
                      "--display",
                      "--display-slots",
                      "1",
                      "--slot1-keys",
                      state->keys.slot1,
                      "--fast-key",
                      state->keys.fast,
                      "--screenshot-key",
                      state->keys.screenshot,
                      "--escape-key",
                      state->keys.escape,
                      "--turbo-hold-key",
                      state->keys.turbo_hold,
                      "--reset-key",
                      state->keys.reset,
                      "--audio",
                      (char *)NULL);
            }
            _exit(127);
        }
        monitor_save_sync_process(pid, state->login.server, state->login.token, game_session_id, fencing_token, sync_slots, sync_count, true);
        _exit(0);
    }
    copy_text(state->login.status,
              sizeof(state->login.status),
              selected_slot2 ? "GB_RUNTIME SERVER2 STARTED" : "GB_RUNTIME LOCAL STARTED");
    client_log(state, "gb_runtime_server_monitor_started", "pid=%ld", (long)monitor_pid);
#endif
}

static void apply_selected_rom_slot_to_server(AppState *state,
                                              bool confirm_delete_saves,
                                              bool confirm_initial_save_import)
{
    unsigned slot_index = state->rom_selected;
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
            if (state->rom_slots[i].rom_path[0] != '\0' && state->rom_slots[i].rom_id[0] == '\0') {
                slot_index = i;
                break;
            }
        }
    }
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        copy_text(state->login.status, sizeof(state->login.status), "NO ROM CHANGES");
        return;
    }
    IntegralConfigRomSlot *slot = &state->rom_slots[slot_index];
    if (slot->rom_path[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "ROM REQUIRED");
        return;
    }
    RomHeaderInfo header;
    if (read_supported_rom_header(slot->rom_path, &header) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "SUPPORTED ROM REQUIRED");
        return;
    }
    if (state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        return;
    }
    char sha256[65];
    if (sha256_file_hex(slot->rom_path, sha256, sizeof(sha256)) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "ROM FILE READ FAILED");
        return;
    }
    char sha1[41];
    if (sha1_file_hex(slot->rom_path, sha1, sizeof(sha1)) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "ROM FILE READ FAILED");
        return;
    }
    char error[160];
    int requires_confirmation = 0;
    unsigned char initial_save_data[INTEGRAL_MAX_SAVE_BYTES];
    unsigned char *initial_save_ptr = NULL;
    size_t initial_save_size = 0;
    bool import_selected = state->allow_user_initial_save_import &&
                           state->rom_initial_save_import_slot == (int)slot_index;
    const IntegralApiRomSlot *server_slot = &state->server_rom_slots[slot_index];
    if (import_selected && server_slot->save_id[0] != '\0' &&
        strcmp(server_slot->sha256, sha256) == 0) {
        state->rom_confirm_initial_save_import = false;
        copy_text(state->login.status, sizeof(state->login.status),
                  "INITIAL SAV REJECTED USE ADMIN SAV REPLACE");
        return;
    }
    if (import_selected && !confirm_initial_save_import) {
        state->rom_confirm_initial_save_import = true;
        copy_text(state->login.status, sizeof(state->login.status), "INITIAL SAV SEND CONFIRM REQUIRED");
        return;
    }
    if (import_selected) {
        if (state->rom_initial_save_import_path[0] == '\0' ||
            read_binary_file(state->rom_initial_save_import_path, initial_save_data,
                             sizeof(initial_save_data), &initial_save_size) != 0 ||
            initial_save_size == 0) {
            state->rom_confirm_initial_save_import = false;
            copy_text(state->login.status, sizeof(state->login.status), "SELECTED INITIAL SAV READ FAILED");
            return;
        }
        initial_save_ptr = initial_save_data;
        client_log(state, "rom_slot_initial_save_import_confirmed",
                   "slot=%u path=%s bytes=%zu", slot_index + 1,
                   state->rom_initial_save_import_path, initial_save_size);
    }
    if (integral_api_apply_rom_slot(state->login.server,
                               state->login.token,
                               slot_index + 1,
                               title_for_rom_path(slot->rom_path),
                               sha256,
                               sha1,
                               header.platform,
                               header.region,
                               header.header_title,
                               initial_save_ptr,
                               initial_save_size,
                               confirm_delete_saves ? 1 : 0,
                               slot->rom_id,
                               sizeof(slot->rom_id),
                               slot->save_id,
                               sizeof(slot->save_id),
                               &requires_confirmation,
                               error,
                               sizeof(error)) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "REGISTER FAILED %s", error);
        return;
    }
    if (requires_confirmation) {
        state->rom_confirm_initial_save_import = false;
        state->rom_confirm_delete = true;
        copy_text(state->login.status, sizeof(state->login.status), "SAV DELETE CONFIRM REQUIRED");
        return;
    }
    state->rom_confirm_delete = false;
    state->rom_confirm_initial_save_import = false;
    state->rom_initial_save_import_slot = -1;
    state->rom_initial_save_import_path[0] = '\0';
    if (integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTERED CONFIG SAVE FAILED");
        return;
    }
    if (!refresh_rom_slots_from_server(state)) {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTERED BUT SYNC FAILED");
        return;
    }
    snprintf(state->login.status, sizeof(state->login.status), "ROM%u REGISTERED", slot_index + 1);
}

static void handle_rom_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    if (state->rom_confirm_initial_save_import) {
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                state->rom_confirm_initial_save_import = false;
                copy_text(state->login.status, sizeof(state->login.status), "REGISTER CANCELED");
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                state->rom_confirm_initial_save_import = false;
                apply_selected_rom_slot_to_server(state, false, true);
                break;
            default:
                break;
        }
        return;
    }
    if (state->rom_confirm_delete) {
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                state->rom_confirm_delete = false;
                copy_text(state->login.status, sizeof(state->login.status), "REGISTER CANCELED");
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                apply_selected_rom_slot_to_server(state, true, true);
                break;
            default:
                break;
        }
        return;
    }
    if (state->rom_edit_target != ROM_EDIT_NONE) {
        char *value = rom_edit_value(state);
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                finish_rom_edit(state);
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                finish_rom_edit(state);
                break;
            case SDLK_BACKSPACE:
                if (value) {
                    remove_last_char(value);
                }
                break;
            default:
                break;
        }
        return;
    }
    if (state->rom_browser_active) {
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                state->rom_browser_active = false;
                copy_text(state->login.status, sizeof(state->login.status), "ROM REGISTER");
                break;
            case SDLK_TAB:
            case SDLK_DOWN:
            case SDLK_RIGHT:
                if (state->rom_browser_count > 0) {
                    state->rom_browser_selected = (state->rom_browser_selected + 1) % state->rom_browser_count;
                }
                break;
            case SDLK_UP:
            case SDLK_LEFT:
                if (state->rom_browser_count > 0) {
                    state->rom_browser_selected =
                        state->rom_browser_selected == 0 ? state->rom_browser_count - 1 : state->rom_browser_selected - 1;
                }
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                apply_browser_rom(state);
                break;
            default:
                break;
        }
        return;
    }

    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            leave_rom_register(state);
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_rom_selection(state, 1);
            break;
        case SDLK_UP:
            move_rom_selection(state, -1);
            break;
        case SDLK_RIGHT:
            cycle_rom_for_selected_slot(state, 1);
            break;
        case SDLK_LEFT:
            cycle_rom_for_selected_slot(state, -1);
            break;
        case SDLK_F4:
            scan_rom_folder(state);
            break;
        case SDLK_F5:
            if (!state->allow_user_initial_save_import) {
                break;
            }
            if (state->rom_selected >= INTEGRAL_ROM_SLOTS ||
                state->rom_slots[state->rom_selected].rom_path[0] == '\0') {
                copy_text(state->login.status, sizeof(state->login.status), "SELECT A ROM SLOT FIRST");
                break;
            }
            if (state->rom_initial_save_import_slot == (int)state->rom_selected) {
                state->rom_initial_save_import_slot = -1;
                state->rom_initial_save_import_path[0] = '\0';
                copy_text(state->login.status, sizeof(state->login.status), "INITIAL SAV IMPORT CLEARED");
            }
            else {
                state->rom_initial_save_import_path[0] = '\0';
                begin_rom_edit(state, ROM_EDIT_INITIAL_SAVE);
            }
            break;
        case SDLK_BACKSPACE:
            clear_selected_rom_slot(state);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->rom_selected == INTEGRAL_ROM_REGISTER_ROW) {
                apply_selected_rom_slot_to_server(state, false, false);
            }
            else if (state->rom_selected == INTEGRAL_ROM_EXPORT_ROW) {
                export_registered_saves(state);
            }
            else if (state->rom_selected == INTEGRAL_ROM_BACK_ROW) {
                leave_rom_register(state);
            }
            else {
                begin_rom_edit(state, ROM_EDIT_ROM);
            }
            break;
        default:
            break;
    }
}

static void handle_rom_text_input(AppState *state, const SDL_TextInputEvent *text)
{
    char *value = rom_edit_value(state);
    size_t capacity = rom_edit_capacity(state);
    if (value && capacity > 0) {
        append_text(value, capacity, text->text);
    }
}

static void handle_room_text_input(AppState *state, const SDL_TextInputEvent *text)
{
    if (state->room_selected != 4 || !state->room_chat_editing) {
        return;
    }
    state->room_chat_composition[0] = '\0';
    append_text(state->room_chat_input, sizeof(state->room_chat_input), text->text);
}

static void handle_room_text_editing(AppState *state, const SDL_TextEditingEvent *edit)
{
    if (state->room_selected != 4 || !state->room_chat_editing) {
        return;
    }
    copy_text(state->room_chat_composition, sizeof(state->room_chat_composition), edit->text);
}

static void draw_app(SDL_Renderer *renderer, const AppState *state)
{
    switch (state->screen) {
        case SCREEN_LOGIN:
            draw_login(renderer, &state->login);
            break;
        case SCREEN_PASSWORD_CHANGE:
            draw_password_change(renderer, state);
            break;
        case SCREEN_MAIN_MENU:
            draw_main_menu(renderer, state);
            break;
        case SCREEN_LOCAL_MODE:
            draw_local_mode(renderer, state);
            break;
        case SCREEN_LOCAL:
            draw_local(renderer, state);
            break;
        case SCREEN_GB_MOBILE:
            draw_gb_mobile(renderer, state);
            break;
        case SCREEN_ROOM_MODE:
            draw_room_mode(renderer, state);
            break;
        case SCREEN_N64_RUNTIME:
            draw_n64_runtime(renderer, state);
            break;
        case SCREEN_JOIN_ROOM:
            draw_join_room(renderer, state);
            break;
        case SCREEN_ROOM:
            draw_room(renderer, state);
            break;
        case SCREEN_N64_ROOM:
            draw_n64_room(renderer, state);
            break;
        case SCREEN_KEY_CONFIG:
            draw_key_config(renderer, state);
            break;
        case SCREEN_ROM_REGISTER:
            draw_rom_register(renderer, state);
            break;
    }
}

static Uint32 main_loop_delay_ms(const AppState *state)
{
    if (state && state->screen == SCREEN_N64_ROOM && state->n64_runtime_media_paired) {
        if (strcmp(state->n64_runtime_media_role, "remote") == 0 &&
            integral_n64_runtime_media_stream_is_video_vsync_paced(state->n64_runtime_media_stream)) {
            return 0;
        }
        return 1;
    }
    return 16;
}

static void poll_server_state(AppState *state)
{
    Uint32 now = SDL_GetTicks();
    monitor_room_gb_runtime_client_exit(state);
    poll_n64_room_async(state, now);
    if (state->screen != SCREEN_N64_ROOM) send_room_heartbeat(state, now);
    if (state->screen == SCREEN_ROOM) {
        if (now - state->last_room_poll_ticks >= 1000u) {
            state->last_room_poll_ticks = now;
            refresh_room_quiet(state);
            if (!state->room_client_started && (current_room_has_link_session(state) || current_room_is_ready_to_start(state))) {
                maybe_start_room_session(state);
                if (state->room_link_session_id[0] == '\0' && state->room_start_requested) {
                    copy_text(state->login.status, sizeof(state->login.status), "BOTH READY  SERVER START PENDING");
                }
            }
        }
    }
    else if (state->screen == SCREEN_N64_ROOM) {
        (void)poll_n64_runtime_media_transport(state, now);
    }
}

typedef enum N64RoomAutoRole {
    N64_ROOM_AUTO_NONE,
    N64_ROOM_AUTO_HOST,
    N64_ROOM_AUTO_REMOTE,
} N64RoomAutoRole;

typedef struct N64RoomAutoOptions {
    N64RoomAutoRole role;
    const char *server;
    const char *server_id;
    const char *username;
    const char *password_env;
    const char *room_code;
    const char *status_file;
    unsigned duration_seconds;
    unsigned timeout_seconds;
    bool remote_vsync_enabled;
    const char *remote_renderer_driver;
    bool remote_renderer_driver_set;
} N64RoomAutoOptions;

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

static void write_n64_room_auto_status(const N64RoomAutoOptions *options,
                                       const char *state_name,
                                       const AppState *state,
                                       const char *detail)
{
    FILE *file;
    const IntegralApiRoom *room = NULL;
    if (!options || !options->status_file || !options->status_file[0]) return;
    if (state && state->room_number >= 1u && state->room_number <= INTEGRAL_API_ROOMS) {
        room = &state->current_room;
    }
    file = fopen(options->status_file, "wb");
    if (!file) return;
    fprintf(file,
            "state=%s\nrole=%s\nroom=%u\nroom_code=%s\npaired=%d\ndetail=%s\n",
            state_name ? state_name : "UNKNOWN",
            options->role == N64_ROOM_AUTO_HOST ? "host" : "remote",
            state ? state->room_number : 0u,
            room && room->room_code[0] ? room->room_code : "",
            state && state->n64_runtime_media_paired ? 1 : 0,
            detail ? detail : "");
    fclose(file);
}

static bool start_n64_room_auto(AppState *state,
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
    if (state->room_number != 0u) leave_current_room(state, false);
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
    activate_matched_room(state, &room);
    write_n64_room_auto_status(options, "WAITING_PEER", state, "room active; selection published");
    return true;
}

static int save_screenshot(SDL_Renderer *renderer, const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0,
                                                          INTEGRAL_WINDOW_WIDTH,
                                                          INTEGRAL_WINDOW_HEIGHT,
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

int main(int argc, char **argv)
{
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
    bool smoke_test = argc > 1 && strcmp(argv[1], "--smoke-test") == 0;
    bool login_smoke = argc > 1 && strcmp(argv[1], "--login-smoke") == 0;
    bool config_smoke = argc > 1 && strcmp(argv[1], "--config-smoke") == 0;
    bool outbox_smoke = argc > 1 && strcmp(argv[1], "--outbox-smoke") == 0;
    bool log_permission_smoke = argc > 1 && strcmp(argv[1], "--log-permission-smoke") == 0;
    bool runtime_path_cleanup_smoke = argc > 1 && strcmp(argv[1], "--runtime-path-cleanup-smoke") == 0;
    bool local_navigation_smoke = argc > 1 && strcmp(argv[1], "--local-navigation-smoke") == 0;
    const char *screenshot_path = NULL;
    const char *config_path = INTEGRAL_CONFIG_DEFAULT_PATH;
    const char *log_path = "integral_client.log";
    N64RoomAutoOptions n64_auto;
    char n64_auto_error[160] = "";
    if (!parse_n64_room_auto_options(argc, argv, &n64_auto,
                                     n64_auto_error, sizeof(n64_auto_error))) {
        fprintf(stderr, "N64 ROOM auto option error: %s\n", n64_auto_error);
        return 2;
    }
    for (int i = 1; i + 1 < argc; i++) {
        if (strcmp(argv[i], "--config") == 0) {
            config_path = argv[i + 1];
        }
        else if (strcmp(argv[i], "--log-file") == 0) {
            log_path = argv[i + 1];
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
                                                       &pending);
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
                                             &pending);
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
                                             &pending);
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
        local_config.slot1_index = 0;
        local_config.slot2_index = -1;
        if (integral_config_save_local(argv[2], &local_config) != 0) {
            fprintf(stderr, "local config save failed\n");
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
        if (save_login_form_config(&login_form_state) != 0) {
            fprintf(stderr, "login form config save failed\n");
            return 1;
        }
        memset(&key_config, 0, sizeof(key_config));
        if (integral_config_load_login(argv[2], &login_config) != 0 ||
            integral_config_load_local(argv[2], &local_config) != 0 ||
            integral_config_load_rom_slots(argv[2], slots, INTEGRAL_ROM_SLOTS) != 0 ||
            integral_config_load_keys(argv[2], &key_config) != 0) {
            fprintf(stderr, "config reload failed\n");
            return 1;
        }
        printf("config ok %s %s %s %s local%d\n",
               slots[0].rom_path,
               slots[0].save_id,
               login_config.username,
               key_config.screenshot,
               local_config.slot1_index);
        return strcmp(slots[0].save_id, "save_test") == 0 &&
                       login_config.remember == 0 &&
                       strcmp(login_config.server, "https://xxxxx.jp/zzz") == 0 &&
                       strcmp(login_config.server_id, "primary") == 0 &&
                       login_config.username[0] == '\0' &&
                       strcmp(key_config.screenshot, "O") == 0 &&
                       local_config.slot1_index == 0
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
        app_state_init(&navigation, config_path);
        /* Smoke navigation must not launch a real configured ROM or contact a server. */
        memset(navigation.rom_slots, 0, sizeof(navigation.rom_slots));
        navigation.login.token[0] = '\0';
        navigation.screen = SCREEN_MAIN_MENU;
        navigation.main_selected = 0;
        key.keysym.sym = SDLK_RETURN;
        handle_main_key(&navigation, &key);
        if (navigation.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }

        navigation.local_mode_selected = 0;
        handle_local_mode_key(&navigation, &key);
        if (navigation.screen != SCREEN_LOCAL) {
            return 1;
        }
        key.keysym.sym = SDLK_ESCAPE;
        handle_local_key(&navigation, &key);
        if (navigation.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }

        navigation.local_mode_selected = 1;
        key.keysym.sym = SDLK_RETURN;
        handle_local_mode_key(&navigation, &key);
        if (navigation.screen != SCREEN_GB_MOBILE) {
            return 1;
        }
        handle_local_key(&navigation, &key);
        if (strstr(navigation.login.status, "SLOT1 ROM REQUIRED") == NULL) {
            return 1;
        }
        navigation.local_selected = 2;
        navigation.mobile_scenario_count = 3;
        navigation.mobile_scenario_selected = 0;
        copy_text(navigation.mobile_scenarios[0].display_name,
                  sizeof(navigation.mobile_scenarios[0].display_name), "SYNTHETIC ALPHA");
        copy_text(navigation.mobile_scenarios[1].display_name,
                  sizeof(navigation.mobile_scenarios[1].display_name), "SYNTHETIC BETA");
        copy_text(navigation.mobile_scenarios[2].display_name,
                  sizeof(navigation.mobile_scenarios[2].display_name), "SYNTHETIC GAMMA");
        key.keysym.sym = SDLK_LEFT;
        handle_local_key(&navigation, &key);
        if (navigation.mobile_scenario_selected != 2u ||
            strstr(navigation.login.status, "SYNTHETIC GAMMA") == NULL) {
            return 1;
        }
        key.keysym.sym = SDLK_RIGHT;
        handle_local_key(&navigation, &key);
        if (navigation.mobile_scenario_selected != 0u ||
            strstr(navigation.login.status, "SYNTHETIC ALPHA") == NULL) {
            return 1;
        }
        key.keysym.sym = SDLK_RETURN;
        handle_local_key(&navigation, &key);
        if (navigation.mobile_scenario_selected != 1u ||
            strstr(navigation.login.status, "SYNTHETIC BETA") == NULL) {
            return 1;
        }
        key.keysym.sym = SDLK_ESCAPE;
        handle_local_key(&navigation, &key);
        if (navigation.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }

        navigation.local_mode_selected = 2;
        key.keysym.sym = SDLK_RETURN;
        handle_local_mode_key(&navigation, &key);
        if (navigation.screen != SCREEN_N64_RUNTIME) {
            return 1;
        }
        key.keysym.sym = SDLK_ESCAPE;
        handle_n64_runtime_key(&navigation, &key);
        if (navigation.screen != SCREEN_LOCAL_MODE) {
            return 1;
        }
        static const AppScreen keyboard_only_screens[] = {
            SCREEN_LOGIN, SCREEN_PASSWORD_CHANGE, SCREEN_MAIN_MENU,
            SCREEN_LOCAL_MODE, SCREEN_LOCAL, SCREEN_GB_MOBILE,
            SCREEN_N64_RUNTIME, SCREEN_ROOM_MODE, SCREEN_JOIN_ROOM,
            SCREEN_ROOM, SCREEN_ROM_REGISTER,
        };
        for (size_t index = 0;
             index < sizeof(keyboard_only_screens) / sizeof(keyboard_only_screens[0]);
             index++) {
            navigation.screen = keyboard_only_screens[index];
            navigation.n64_runtime_media_authenticated = true;
            if (client_game_input_required(&navigation)) return 1;
        }
        navigation.screen = SCREEN_KEY_CONFIG;
        if (!client_game_input_required(&navigation)) return 1;
        navigation.screen = SCREEN_N64_ROOM;
        navigation.n64_runtime_media_authenticated = false;
        if (client_game_input_required(&navigation)) return 1;
        navigation.n64_runtime_media_authenticated = true;
        if (!client_game_input_required(&navigation)) return 1;
        IntegralConfigKeys fixed_host_keys;
        memset(&fixed_host_keys, 0, sizeof(fixed_host_keys));
        copy_text(fixed_host_keys.slot1, sizeof(fixed_host_keys.slot1), "SLOT1-JOYCON");
        copy_text(fixed_host_keys.slot2, sizeof(fixed_host_keys.slot2), "SLOT2-JOYCON");
        if (strcmp(gb_runtime_fixed_host_key_spec_for_role("host", &fixed_host_keys),
                   "SLOT1-JOYCON") != 0 ||
            strcmp(gb_runtime_fixed_host_key_spec_for_role("remote", &fixed_host_keys),
                   "SLOT2-JOYCON") != 0) {
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
            screenshot_path = argv[i + 1];
        }
    }
    client_log_open(log_path);
    client_log(NULL, "client_start", "version=%s config=%s log=%s", INTEGRAL_CLIENT_VERSION, config_path, log_path);
    SDL_SetHint("SDL_IME_SHOW_UI", "0");
    SDL_SetHint("SDL_IME_INTERNAL_EDITING", "1");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_init_failed", "error=%s", SDL_GetError());
        client_log_close();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("INTEGRAL EMULATOR Login",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          INTEGRAL_WINDOW_WIDTH,
                                          INTEGRAL_WINDOW_HEIGHT,
                                          0);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_window_failed", "error=%s", SDL_GetError());
        SDL_Quit();
        client_log_close();
        return 1;
    }
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_renderer_failed", "error=%s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }

    AppState state;
    app_state_init(&state, config_path);
    state.n64_runtime_media_stream = integral_n64_runtime_media_stream_create(renderer);
    if (!state.n64_runtime_media_stream) {
        fprintf(stderr, "N64 Runtime media stream allocation failed\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    if (n64_auto.role == N64_ROOM_AUTO_REMOTE) {
        integral_n64_runtime_media_stream_set_video_vsync_enabled(
            state.n64_runtime_media_stream, n64_auto.remote_vsync_enabled);
        if (n64_auto.remote_renderer_driver_set) {
            integral_n64_runtime_media_stream_set_video_renderer_driver(
                state.n64_runtime_media_stream, n64_auto.remote_renderer_driver);
        }
        client_log(&state, "n64_auto_remote_vsync",
                   "requested=%s renderer=%s",
                   n64_auto.remote_vsync_enabled ? "on" : "off",
                   n64_auto.remote_renderer_driver ? n64_auto.remote_renderer_driver : "auto");
    }
    state.n64_room_poll_worker = integral_room_poll_worker_create();
    if (!state.n64_room_poll_worker) {
        fprintf(stderr, "N64 ROOM poll worker allocation failed: %s\n", SDL_GetError());
        integral_n64_runtime_media_stream_destroy(state.n64_runtime_media_stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    client_log(&state, "app_state_ready", "server=%s remembered=%d", state.login.server, state.login.remember_login ? 1 : 0);
    if (n64_auto.role != N64_ROOM_AUTO_NONE) {
        write_n64_room_auto_status(&n64_auto, "STARTING", &state, "login and room setup");
        if (!start_n64_room_auto(&state, &n64_auto,
                                 n64_auto_error, sizeof(n64_auto_error))) {
            client_log(&state, "n64_auto_failed", "stage=start error=%s", n64_auto_error);
            write_n64_room_auto_status(&n64_auto, "FAILED", &state, n64_auto_error);
            integral_room_poll_worker_destroy(state.n64_room_poll_worker);
            integral_n64_runtime_media_stream_destroy(state.n64_runtime_media_stream);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            client_log_close();
            return 1;
        }
    }
    if (smoke_test) {
        if (state.login.server[0] == '\0') {
            copy_text(state.login.server,
                      sizeof(state.login.server),
                      "https://xxxxx.jp/zzz");
        }
        for (int i = 1; i + 1 < argc; i++) {
            if (strcmp(argv[i], "--screen") == 0) {
                if (strcmp(argv[i + 1], "main") == 0) {
                    state.screen = SCREEN_MAIN_MENU;
                    copy_text(state.login.status, sizeof(state.login.status), "SELECT A MENU ITEM");
                }
                else if (strcmp(argv[i + 1], "room-mode") == 0) {
                    state.screen = SCREEN_ROOM_MODE;
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER1");
                    copy_text(state.login.status, sizeof(state.login.status), "SELECT COMMUNICATION MODE");
                }
                else if (strcmp(argv[i + 1], "join-room") == 0) {
                    state.screen = SCREEN_JOIN_ROOM;
                    state.room_code_editing = true;
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER2");
                    copy_text(state.room_code_input, sizeof(state.room_code_input), "483");
                    copy_text(state.login.status, sizeof(state.login.status), "ENTER ROOM CODE");
                }
                else if (strcmp(argv[i + 1], "local-mode") == 0) {
                    state.screen = SCREEN_LOCAL_MODE;
                    state.local_mode_selected = 0;
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER1");
                    copy_text(state.login.status, sizeof(state.login.status), "SELECT LOCAL MODE");
                }
                else if (strcmp(argv[i + 1], "local") == 0) {
                    state.screen = SCREEN_LOCAL;
                    copy_text(state.login.status, sizeof(state.login.status), "GB MODE");
                    copy_text(state.rom_slots[0].rom_path, sizeof(state.rom_slots[0].rom_path), "roms/sample_game.gbc");
                    copy_text(state.rom_slots[0].rom_id, sizeof(state.rom_slots[0].rom_id), "rom_test");
                    copy_text(state.rom_slots[0].save_id, sizeof(state.rom_slots[0].save_id), "save_test");
                    state.local_slot_indices[0] = 0;
                }
                else if (strcmp(argv[i + 1], "gb-mobile") == 0) {
                    state.screen = SCREEN_GB_MOBILE;
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER1");
                    copy_text(state.login.status, sizeof(state.login.status), "SELECT A MOBILE-CAPABLE ROM");
                    copy_text(state.rom_slots[0].rom_path, sizeof(state.rom_slots[0].rom_path), "roms/sample_game.gbc");
                    copy_text(state.rom_slots[0].rom_id, sizeof(state.rom_slots[0].rom_id), "rom_test");
                    copy_text(state.rom_slots[0].save_id, sizeof(state.rom_slots[0].save_id), "save_test");
                    state.local_slot_indices[0] = 0;
                }
                else if (strcmp(argv[i + 1], "n64_runtime") == 0) {
                    state.screen = SCREEN_N64_RUNTIME;
                    copy_text(state.login.status, sizeof(state.login.status), "N64 MODE");
                    copy_text(state.rom_slots[0].rom_path,
                              sizeof(state.rom_slots[0].rom_path),
                              "../runtimes/n64/roms/sample_n64.z64");
                    state.integral_n64_runtime_n64_slot_index = 0;
                }
                else if (strcmp(argv[i + 1], "room") == 0) {
                    state.screen = SCREEN_ROOM;
                    state.room_number = 1;
                    state.room_slot_index = 0;
                    state.room_ready_self = false;
                    state.room_ready_peer = true;
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER1");
                    IntegralApiRoom *room = &state.current_room;
                    room->room_number = 1;
                    copy_text(room->room_code, sizeof(room->room_code), "48321");
                    copy_text(room->room_type, sizeof(room->room_type), "link_cable");
                    copy_text(room->user1, sizeof(room->user1), "TESTUSER1");
                    copy_text(room->user2, sizeof(room->user2), "TESTUSER2");
                    copy_text(room->slot1, sizeof(room->slot1), "ROM1");
                    copy_text(room->slot2, sizeof(room->slot2), "ROM2");
                    copy_text(room->link_mode, sizeof(room->link_mode), "battle");
                    room->ready2 = 1;
                    copy_text(state.rom_slots[0].rom_path, sizeof(state.rom_slots[0].rom_path), "roms/sample_game.gbc");
                    copy_text(state.room_chat_log[27], sizeof(state.room_chat_log[27]), "TESTUSER1: こんにちは");
                    copy_text(state.room_chat_log[28], sizeof(state.room_chat_log[28]), "TESTUSER2: じゅんびOK?");
                    copy_text(state.room_chat_log[29], sizeof(state.room_chat_log[29]), "TESTUSER1: SLOT ROM1");
                    copy_text(state.room_chat_log[30], sizeof(state.room_chat_log[30]), "TESTUSER2: READY");
                    copy_text(state.room_chat_log[31], sizeof(state.room_chat_log[31]), "TESTUSER1: 5けんめ");
                    copy_text(state.room_chat_input, sizeof(state.room_chat_input), "よろしく");
                    copy_text(state.login.status, sizeof(state.login.status), "ROOM");
                }
                else if (strcmp(argv[i + 1], "room-chat-active") == 0) {
                    state.screen = SCREEN_ROOM;
                    state.room_number = 1;
                    state.room_selected = 3;
                    state.room_slot_index = 0;
                    state.room_chat_editing = true;
                    state.room_ready_self = false;
                    state.room_ready_peer = true;
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER1");
                    copy_text(state.rom_slots[0].rom_path, sizeof(state.rom_slots[0].rom_path), "roms/sample_game.gbc");
                    copy_text(state.room_chat_log[30], sizeof(state.room_chat_log[30]), "TESTUSER2: じゅんびOK?");
                    copy_text(state.room_chat_log[31], sizeof(state.room_chat_log[31]), "TESTUSER1: 5けんめ");
                    copy_text(state.room_chat_input, sizeof(state.room_chat_input), "よろしく");
                    copy_text(state.room_chat_composition, sizeof(state.room_chat_composition), "入力中");
                    copy_text(state.login.status, sizeof(state.login.status), "CHAT INPUT ACTIVE");
                }
                else if (strcmp(argv[i + 1], "n64-room") == 0 ||
                         strcmp(argv[i + 1], "n64-room-ready-error") == 0) {
                    state.screen = SCREEN_N64_ROOM;
                    state.room_number = 65;
                    state.room_selected = 3;
                    state.n64_room_n64_slot_index = 0;
                    state.n64_room_user1_gb_slot_index = 1;
                    state.n64_room_user2_gb_slot_index = 2;
                    IntegralApiRoom *room = &state.current_room;
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
                    copy_text(state.login.username, sizeof(state.login.username), "TESTUSER1");
                    copy_text(state.room_chat_log[30], sizeof(state.room_chat_log[30]), "PLAYER001: N64 MODE");
                    copy_text(state.room_chat_log[31], sizeof(state.room_chat_log[31]), "PLAYER002: READY?");
                    copy_text(state.login.status,
                              sizeof(state.login.status),
                              strcmp(argv[i + 1], "n64-room-ready-error") == 0
                                  ? "READY FAILED: USER1 NEEDS MATCHING ROM IN ROM1-8"
                                  : "ROOM65 N64 MODE");
                }
                else if (strcmp(argv[i + 1], "login-active") == 0) {
                    state.screen = SCREEN_LOGIN;
                    state.login.selected = FIELD_USERNAME;
                    state.login.editing = true;
                    copy_text(state.login.username, sizeof(state.login.username), "testuser");
                    copy_text(state.login.status, sizeof(state.login.status), "TEXT INPUT ACTIVE");
                }
                else if (strcmp(argv[i + 1], "password-change") == 0) {
                    state.screen = SCREEN_PASSWORD_CHANGE;
                    copy_text(state.password_change.status,
                              sizeof(state.password_change.status),
                              "PASSWORD CHANGE REQUIRED");
                }
                else if (strcmp(argv[i + 1], "password-change-active") == 0) {
                    state.screen = SCREEN_PASSWORD_CHANGE;
                    state.password_change.selected = PASSWORD_CHANGE_NEW;
                    state.password_change.editing = true;
                    copy_text(state.password_change.new_password,
                              sizeof(state.password_change.new_password),
                              "newpass1");
                    copy_text(state.password_change.status,
                              sizeof(state.password_change.status),
                              "TEXT INPUT ACTIVE");
                }
                else if (strcmp(argv[i + 1], "keys") == 0) {
                    state.screen = SCREEN_KEY_CONFIG;
                    copy_text(state.login.status, sizeof(state.login.status), "KEY CONFIG");
                }
                else if (strcmp(argv[i + 1], "rom") == 0) {
                    state.screen = SCREEN_ROM_REGISTER;
                    copy_text(state.login.status, sizeof(state.login.status), "ROM REGISTER");
                    copy_text(state.rom_slots[0].rom_path, sizeof(state.rom_slots[0].rom_path), "roms/sample_game.gbc");
                }
                else if (strcmp(argv[i + 1], "rom-browser") == 0) {
                    state.screen = SCREEN_ROM_REGISTER;
                    state.rom_browser_active = true;
                    state.rom_browser_count = 3;
                    state.rom_browser_selected = 1;
                    copy_text(state.login.status, sizeof(state.login.status), "SELECT ROM FROM FOLDER");
                    copy_text(state.rom_browser_entries[0], sizeof(state.rom_browser_entries[0]), "roms/sample_a.gbc");
                    copy_text(state.rom_browser_entries[1], sizeof(state.rom_browser_entries[1]), "roms/sample_b.gbc");
                    copy_text(state.rom_browser_entries[2], sizeof(state.rom_browser_entries[2]), "roms/sample_c.gbc");
                }
            }
        }
    }
    draw_app(renderer, &state);
    if (screenshot_path && save_screenshot(renderer, screenshot_path) != 0) {
        client_log(&state, "screenshot_failed", "path=%s", screenshot_path);
        integral_room_poll_worker_destroy(state.n64_room_poll_worker);
        integral_n64_runtime_media_stream_destroy(state.n64_runtime_media_stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    if (smoke_test) {
        client_log(&state, "smoke_test_done", "");
        integral_room_poll_worker_destroy(state.n64_room_poll_worker);
        integral_n64_runtime_media_stream_destroy(state.n64_runtime_media_stream);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 0;
    }

    Uint32 n64_auto_started_ticks = SDL_GetTicks();
    Uint32 n64_auto_both_present_ticks = 0u;
    Uint32 n64_auto_paired_ticks = 0u;
    int n64_auto_exit_code = 0;
    while (!state.quit) {
        (void)set_game_input_active(
            &state, client_game_input_required(&state) && !state.runtime_exit_confirming);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (handle_runtime_exit_confirmation_event(&state, &event)) {
                continue;
            }
            if (event.type == SDL_QUIT && n64_room_runtime_active(&state)) {
                request_runtime_exit_confirmation(&state, true);
            }
            else if (event.type == SDL_QUIT) {
                client_log(&state, "client_quit_event", "status=%s", state.login.status);
                leave_current_room(&state, false);
                state.quit = true;
            }
            else if (event.type == SDL_KEYDOWN) {
                if (state.screen == SCREEN_LOGIN) {
                    handle_login_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_PASSWORD_CHANGE) {
                    handle_password_change_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_MAIN_MENU) {
                    handle_main_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_LOCAL_MODE) {
                    handle_local_mode_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_ROOM_MODE) {
                    handle_room_mode_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_LOCAL || state.screen == SCREEN_GB_MOBILE) {
                    handle_local_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_N64_RUNTIME) {
                    handle_n64_runtime_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_JOIN_ROOM) {
                    handle_join_room_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_ROOM) {
                    handle_room_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_N64_ROOM) {
                    handle_n64_room_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_KEY_CONFIG) {
                    handle_key_config_key(&state, &event.key);
                }
                else if (state.screen == SCREEN_ROM_REGISTER) {
                    handle_rom_key(&state, &event.key);
                }
            }
            else if (state.game_input_active &&
                     (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
                      event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED)) {
                integral_gb_runtime_key_config_handle_device_event(&event);
                if (event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
                    state.key_capture_wait_release = false;
                    state.key_capture_release_binding = SDLK_UNKNOWN;
                }
                client_log(&state, "controller_device_changed", "event=%u", event.type);
            }
            else if (state.screen == SCREEN_KEY_CONFIG && game_controller_input_event(event.type)) {
                handle_key_config_controller_event(&state, &event);
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.event == SDL_WINDOWEVENT_CLOSE &&
                     integral_n64_runtime_media_stream_is_video_window(state.n64_runtime_media_stream,
                                                                 event.window.windowID)) {
                request_runtime_exit_confirmation(&state, false);
            }
            else if (event.type == SDL_TEXTINPUT) {
                if (state.screen == SCREEN_LOGIN && state.login.editing) {
                    handle_text_input(&state.login, &event.text);
                    if (state.login.selected == FIELD_SERVER &&
                        save_login_form_config(&state) != 0) {
                        copy_text(state.login.status, sizeof(state.login.status), "LOGIN PREF SAVE FAILED");
                    }
                }
                else if (state.screen == SCREEN_PASSWORD_CHANGE) {
                    handle_password_change_text_input(&state, &event.text);
                }
                else if (state.screen == SCREEN_ROM_REGISTER && state.rom_edit_target != ROM_EDIT_NONE) {
                    handle_rom_text_input(&state, &event.text);
                }
                else if (state.screen == SCREEN_JOIN_ROOM) {
                    handle_join_room_text_input(&state, &event.text);
                }
                else if (state.screen == SCREEN_ROOM) {
                    handle_room_text_input(&state, &event.text);
                }
            }
            else if (event.type == SDL_TEXTEDITING) {
                if (state.screen == SCREEN_ROOM) {
                    handle_room_text_editing(&state, &event.edit);
                }
            }
        }
        poll_server_state(&state);
        if (n64_auto.role != N64_ROOM_AUTO_NONE) {
            Uint32 now = SDL_GetTicks();
            const IntegralApiRoom *room = NULL;
            if (state.room_number >= 1u && state.room_number <= INTEGRAL_API_ROOMS) {
                room = &state.current_room;
            }
            if (n64_auto_paired_ticks != 0u &&
                now - n64_auto_paired_ticks >= n64_auto.duration_seconds * 1000u) {
                if (state.n64_runtime_media_saw_positive_video) {
                    client_log(&state, "n64_auto_complete", "duration_seconds=%u", n64_auto.duration_seconds);
                    write_n64_room_auto_status(&n64_auto, "COMPLETE", &state, "measurement window complete");
                }
                else {
                    copy_text(n64_auto_error, sizeof(n64_auto_error),
                              "measurement completed without a video frame");
                    client_log(&state, "n64_auto_failed", "stage=metrics error=%s", n64_auto_error);
                    write_n64_room_auto_status(&n64_auto, "FAILED", &state, n64_auto_error);
                    n64_auto_exit_code = 1;
                }
                state.quit = true;
            }
            else if (state.n64_runtime_media_paired) {
                if (n64_auto_paired_ticks == 0u) {
                    n64_auto_paired_ticks = now;
                    client_log(&state, "n64_auto_paired", "duration_seconds=%u", n64_auto.duration_seconds);
                    write_n64_room_auto_status(&n64_auto, "MEASURING", &state, "media paired");
                }
            }
            else if (n64_auto_paired_ticks != 0u) {
                Uint32 measured_ms = now - n64_auto_paired_ticks;
                Uint32 target_ms = n64_auto.duration_seconds * 1000u;
                if (measured_ms + 1000u >= target_ms) {
                    if (state.n64_runtime_media_saw_positive_video) {
                        client_log(&state,
                                   "n64_auto_complete",
                                   "duration_seconds=%u peer_closed_at_ms=%u",
                                   n64_auto.duration_seconds,
                                   measured_ms);
                        write_n64_room_auto_status(&n64_auto, "COMPLETE", &state,
                                                   "peer closed within final measurement second");
                    }
                    else {
                        copy_text(n64_auto_error, sizeof(n64_auto_error),
                                  "peer closed at boundary without a video frame");
                        client_log(&state, "n64_auto_failed", "stage=metrics error=%s", n64_auto_error);
                        write_n64_room_auto_status(&n64_auto, "FAILED", &state, n64_auto_error);
                        n64_auto_exit_code = 1;
                    }
                }
                else {
                    snprintf(n64_auto_error, sizeof(n64_auto_error),
                             "media pair lost after %u milliseconds",
                             measured_ms);
                    client_log(&state, "n64_auto_failed", "stage=media error=%s", n64_auto_error);
                    write_n64_room_auto_status(&n64_auto, "FAILED", &state, n64_auto_error);
                    n64_auto_exit_code = 1;
                }
                state.quit = true;
            }
            else if (room && room->user1[0] && room->user2[0]) {
                if (n64_auto_both_present_ticks == 0u) {
                    n64_auto_both_present_ticks = now;
                    write_n64_room_auto_status(&n64_auto, "READY_DELAY", &state,
                                               "both users present; waiting for selection propagation");
                }
                if (!state.n64_room_ready && now - n64_auto_both_present_ticks >= 2000u) {
                    if (!resolve_n64_room_local_outbox(&state) || !sync_n64_room_state(&state, true)) {
                        snprintf(n64_auto_error, sizeof(n64_auto_error),
                                 "READY failed: %.120s", state.login.status);
                        client_log(&state, "n64_auto_failed", "stage=ready error=%s", n64_auto_error);
                        write_n64_room_auto_status(&n64_auto, "FAILED", &state, n64_auto_error);
                        n64_auto_exit_code = 1;
                        state.quit = true;
                    }
                    else {
                        state.n64_room_ready = true;
                        write_n64_room_auto_status(&n64_auto, "WAITING_PAIR", &state,
                                                   "READY published; waiting for media pair");
                    }
                }
            }
            if (!state.quit &&
                now - n64_auto_started_ticks >= n64_auto.timeout_seconds * 1000u) {
                snprintf(n64_auto_error, sizeof(n64_auto_error),
                         "timeout after %u seconds: %.100s",
                         n64_auto.timeout_seconds, state.login.status);
                client_log(&state, "n64_auto_failed", "stage=timeout error=%s", n64_auto_error);
                write_n64_room_auto_status(&n64_auto, "FAILED", &state, n64_auto_error);
                n64_auto_exit_code = 1;
                state.quit = true;
            }
        }
        draw_app(renderer, &state);
        draw_runtime_exit_confirmation(renderer, &state);
        Uint32 delay_ms = main_loop_delay_ms(&state);
        if (delay_ms > 0) SDL_Delay(delay_ms);
    }

    (void)set_game_input_active(&state, false);

    SDL_StopTextInput();
    client_log(&state, "client_shutdown", "status=%s", state.login.status);
    leave_current_room(&state, false);
    integral_room_poll_worker_destroy(state.n64_room_poll_worker);
    integral_n64_runtime_media_stream_destroy(state.n64_runtime_media_stream);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    client_log_close();
    return n64_auto_exit_code;
}

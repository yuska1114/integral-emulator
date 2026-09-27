/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_runtime_support.h"
#include "client_key_config.h"
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


#define INTEGRAL_GB_RUNTIME_DUAL_SERVER "../runtimes/gb/build_exp/integral_gb_runtime_dual_server"
#define INTEGRAL_GB_RUNTIME_MOBILE_RUNTIME "../runtimes/gb/build_exp/integral_gb_runtime_mobile_runtime"
#define INTEGRAL_N64_RUNTIME_HOME "../runtimes/n64"
#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

static bool integral_n64_runtime_path(char *out, size_t out_size, const char *suffix);
static long long days_from_civil(int year, unsigned month, unsigned day);


const char *integral_gb_runtime_server_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER");
    return value && value[0] ? value : INTEGRAL_GB_RUNTIME_DUAL_SERVER;
}


const char *integral_gb_runtime_mobile_runtime_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME");
    return value && value[0] ? value : INTEGRAL_GB_RUNTIME_MOBILE_RUNTIME;
}


const char *integral_n64_runtime_home_path(void)
{
    const char *value = getenv("INTEGRAL_EMULATOR_N64_RUNTIME_HOME");
    return value && value[0] ? value : INTEGRAL_N64_RUNTIME_HOME;
}


int integral_runtime_frontend_access(const char *path)
{
#ifdef _WIN32
    /* MSVCRT _access() does not support the POSIX X_OK mode. */
    return access(path, F_OK);
#else
    return access(path, X_OK);
#endif
}


static bool integral_n64_runtime_path(char *out, size_t out_size, const char *suffix)
{
    int written = snprintf(out, out_size, "%s/%s", integral_n64_runtime_home_path(), suffix);
    if (written < 0 || (size_t)written >= out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return false;
    }
    return true;
}


void copy_text(char *dest, size_t dest_size, const char *src)
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


bool parse_iso8601_unix(const char *value, long long *unix_time_out)
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


void append_text(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(dest);
    if (len >= dest_size) {
        return;
    }
    copy_text(dest + len, dest_size - len, src);
}


bool append_ascii_text(char *dest, size_t dest_size, const char *src)
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


bool append_alnum_text(char *dest, size_t dest_size, const char *src)
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


void remove_last_char(char *text)
{
    size_t len = strlen(text);
    if (len > 0) {
        text[len - 1] = '\0';
    }
}


void remove_last_utf8_char(char *text)
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


const char *path_file_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;
    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}


bool game_controller_input_event(Uint32 type)
{
    return type == SDL_CONTROLLERBUTTONDOWN || type == SDL_CONTROLLERBUTTONUP ||
           type == SDL_CONTROLLERAXISMOTION || type == SDL_JOYBUTTONDOWN ||
           type == SDL_JOYBUTTONUP || type == SDL_JOYAXISMOTION ||
           type == SDL_JOYHATMOTION;
}


bool integral_n64_runtime_paths(char *frontend,
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


int n64_key_name_to_scancode(const char *name)
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


bool configured_controller_binding(const char *name)
{
    return strncmp(name, "PAD@", 4) == 0 || strncmp(name, "JOY@", 4) == 0 ||
           strncmp(name, "PAD_", 4) == 0 || strncmp(name, "JOY_", 4) == 0;
}


bool make_n64_runtime_keymap_spec(const char *spec, char *out, size_t out_size)
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

/* Mupen CoreEvents order: Stop=0, Reset=5, Screenshot=8; all other
 * hotkeys remain unbound, including savestates and speed controls. */
bool make_n64_runtime_util_hotkeys(const IntegralConfigKeys *keys, bool allow_reset,
                                   char *out, size_t capacity)
{
    Uint32 controller_flags = SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER;
    bool temporary_controller_scope =
        (SDL_WasInit(controller_flags) & controller_flags) != controller_flags;
    bool result = false;
    if (temporary_controller_scope && SDL_InitSubSystem(controller_flags) != 0) return false;
    (void)integral_gb_runtime_key_config_open_game_controllers();
    size_t used = 0;
    for (unsigned i = 0; i < 27; ++i) {
        const char *name = i == 0 ? keys->escape :
                           i == 8 ? keys->screenshot :
                           i == 5 && allow_reset ? keys->reset : NULL;
        int kind = 0, index = 0, direction = 0, device = -1;
        if (name) {
            if (configured_controller_binding(name)) {
                SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(name);
                if (!integral_gb_runtime_key_config_binding_to_n64(binding, &device, &kind, &index, &direction)) goto done;
            } else {
                index = n64_key_name_to_scancode(name);
                if (index <= SDL_SCANCODE_UNKNOWN) goto done;
                kind = 1;
            }
        }
        int n = snprintf(out + used, capacity - used, "%s%d:%d:%d:%d",
                         i ? "," : "", kind, index, direction, device);
        if (n < 0 || (size_t)n >= capacity - used) goto done;
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

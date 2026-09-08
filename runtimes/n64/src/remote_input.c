/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remote_input.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define INTEGRAL_N64_RUNTIME_REMOTE_EXPORT __declspec(dllexport)
#else
#define INTEGRAL_N64_RUNTIME_REMOTE_EXPORT __attribute__((visibility("default")))
#endif

static const char *g_remote_input_path;

static uint32_t read_be32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | (uint32_t)data[3];
}

static uint64_t read_be64(const unsigned char *data)
{
    uint64_t value = 0u;
    unsigned int index;
    for (index = 0u; index < 8u; ++index) {
        value = (value << 8u) | data[index];
    }
    return value;
}

static uint64_t wall_clock_ms(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) return 0u;
    return (uint64_t)value.tv_sec * 1000u + (uint64_t)value.tv_nsec / 1000000u;
}

void integral_n64_runtime_remote_input_configure(const char *path)
{
    g_remote_input_path = path != NULL && path[0] != '\0' ? path : NULL;
}

int integral_n64_runtime_remote_input_parse(const unsigned char *record,
                                 size_t record_size,
                                 uint64_t now_ms,
                                 uint64_t *buttons_out)
{
    uint64_t written_ms;
    uint64_t buttons;
    if (buttons_out == NULL) return -1;
    *buttons_out = 0u;
    if (record == NULL || record_size != INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE ||
        memcmp(record, "S64C", 4u) != 0 || record[4] != 1u ||
        record[5] != 0u || record[6] != 0u || record[7] != 0u ||
        read_be32(record + 8u) == 0u) {
        return -1;
    }
    written_ms = read_be64(record + 12u);
    buttons = read_be64(record + 20u);
    if ((buttons & ~((uint64_t)INTEGRAL_N64_RUNTIME_REMOTE_INPUT_BUTTON_MASK)) != 0u ||
        written_ms == 0u || now_ms < written_ms ||
        now_ms - written_ms > INTEGRAL_N64_RUNTIME_REMOTE_INPUT_MAX_AGE_MS) {
        return -1;
    }
    *buttons_out = buttons;
    return 0;
}

INTEGRAL_N64_RUNTIME_REMOTE_EXPORT int integral_n64_runtime_remote_controller_state(uint64_t *buttons_out)
{
#ifdef _WIN32
    unsigned char record[INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE + 1u];
#else
    unsigned char record[INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE];
#endif
#ifdef _WIN32
    HANDLE file;
    DWORD bytes_read = 0u;
#else
    FILE *file;
    int extra;
#endif
    if (buttons_out == NULL) return 0;
    *buttons_out = 0u;
    if (g_remote_input_path == NULL) return 0;
#ifdef _WIN32
    file = CreateFileA(g_remote_input_path, GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 1;
    if (!ReadFile(file, record, (DWORD)sizeof(record), &bytes_read, NULL) ||
        !CloseHandle(file) ||
        bytes_read != (DWORD)INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE ||
        integral_n64_runtime_remote_input_parse(record, INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE,
                                     wall_clock_ms(),
                                     buttons_out) != 0) {
        *buttons_out = 0u;
    }
#else
    file = fopen(g_remote_input_path, "rb");
    if (file == NULL) return 1;
    if (fread(record, 1u, sizeof(record), file) != sizeof(record)) {
        (void)fclose(file);
        return 1;
    }
    extra = fgetc(file);
    if (fclose(file) != 0 || extra != EOF ||
        integral_n64_runtime_remote_input_parse(record, sizeof(record), wall_clock_ms(),
                                     buttons_out) != 0) {
        *buttons_out = 0u;
    }
#endif
    return 1;
}

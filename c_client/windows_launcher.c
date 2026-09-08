/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <wchar.h>

static bool join_path(wchar_t *out, size_t out_count, const wchar_t *base, const wchar_t *suffix)
{
    int written = swprintf(out, out_count, L"%ls\\%ls", base, suffix);
    return written > 0 && (size_t)written < out_count;
}

static void ensure_directory(const wchar_t *path)
{
    CreateDirectoryW(path, NULL);
}

static bool file_exists(const wchar_t *path)
{
    DWORD attributes = GetFileAttributesW(path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool prepend_runtime_path(const wchar_t *release_dir)
{
    wchar_t dll_dir[MAX_PATH];
    wchar_t client_dir[MAX_PATH];
    wchar_t integral_gb_runtime_dir[MAX_PATH];
    if (!join_path(dll_dir, MAX_PATH, release_dir, L"dll") ||
        !join_path(client_dir, MAX_PATH, release_dir, L"client") ||
        !join_path(integral_gb_runtime_dir, MAX_PATH, release_dir, L"runtimes\\gb")) {
        return false;
    }

    DWORD old_size = GetEnvironmentVariableW(L"PATH", NULL, 0);
    wchar_t *old_path = NULL;
    if (old_size > 0) {
        old_path = (wchar_t *)calloc(old_size, sizeof(wchar_t));
        if (!old_path) return false;
        GetEnvironmentVariableW(L"PATH", old_path, old_size);
    }

    size_t needed = wcslen(dll_dir) + wcslen(client_dir) + wcslen(integral_gb_runtime_dir) + 4u;
    if (old_path) needed += wcslen(old_path);
    wchar_t *path = (wchar_t *)calloc(needed, sizeof(wchar_t));
    if (!path) {
        free(old_path);
        return false;
    }
    swprintf(path, needed, L"%ls;%ls;%ls;%ls", dll_dir, client_dir, integral_gb_runtime_dir,
             old_path ? old_path : L"");
    BOOL ok = SetEnvironmentVariableW(L"PATH", path);
    free(path);
    free(old_path);
    return ok != 0;
}

static const wchar_t *skip_first_argument(const wchar_t *command_line)
{
    const wchar_t *cursor = command_line;
    while (*cursor == L' ' || *cursor == L'\t') cursor++;
    if (*cursor == L'"') {
        cursor++;
        while (*cursor && *cursor != L'"') cursor++;
        if (*cursor == L'"') cursor++;
    }
    else {
        while (*cursor && *cursor != L' ' && *cursor != L'\t') cursor++;
    }
    while (*cursor == L' ' || *cursor == L'\t') cursor++;
    return cursor;
}

static bool set_packaged_environment(const wchar_t *release_dir)
{
    wchar_t path[MAX_PATH];
    if (!prepend_runtime_path(release_dir)) return false;

    if (join_path(path, MAX_PATH, release_dir, L"runtimes\\gb\\integral_gb_runtime_dual_server.exe")) {
        SetEnvironmentVariableW(L"INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER", path);
    }
    if (join_path(path, MAX_PATH, release_dir, L"runtimes\\gb\\integral_gb_runtime_fixed_host.exe")) {
        SetEnvironmentVariableW(L"INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME", path);
    }
    if (join_path(path, MAX_PATH, release_dir, L"runtimes\\gb\\integral_gb_runtime_mobile_runtime.exe")) {
        SetEnvironmentVariableW(L"INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME", path);
    }
    if (join_path(path, MAX_PATH, release_dir, L"runtimes\\n64")) {
        SetEnvironmentVariableW(L"INTEGRAL_EMULATOR_N64_RUNTIME_HOME", path);
    }
    if (join_path(path, MAX_PATH, release_dir, L"ssl\\cert.pem") && file_exists(path)) {
        SetEnvironmentVariableW(L"SSL_CERT_FILE", path);
    }
    return true;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command_line, int show_command)
{
    (void)instance;
    (void)previous;
    (void)command_line;
    (void)show_command;

    wchar_t release_dir[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, release_dir, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        MessageBoxW(NULL, L"Failed to locate the launcher.", L"INTEGRAL EMULATOR", MB_ICONERROR);
        return 1;
    }
    wchar_t *slash = wcsrchr(release_dir, L'\\');
    if (!slash) {
        MessageBoxW(NULL, L"Failed to locate the release folder.", L"INTEGRAL EMULATOR", MB_ICONERROR);
        return 1;
    }
    *slash = L'\0';

    wchar_t path[MAX_PATH];
    if (join_path(path, MAX_PATH, release_dir, L"roms")) ensure_directory(path);
    if (join_path(path, MAX_PATH, release_dir, L"config")) ensure_directory(path);
    if (join_path(path, MAX_PATH, release_dir, L"export")) ensure_directory(path);

    if (!set_packaged_environment(release_dir)) {
        MessageBoxW(NULL, L"Failed to prepare the runtime environment.", L"INTEGRAL EMULATOR", MB_ICONERROR);
        return 1;
    }
    SetCurrentDirectoryW(release_dir);

    wchar_t client[MAX_PATH];
    if (!join_path(client, MAX_PATH, release_dir, L"client\\integral_client.exe") ||
        !file_exists(client)) {
        MessageBoxW(NULL, L"The client runtime was not found.", L"INTEGRAL EMULATOR", MB_ICONERROR);
        return 1;
    }

    const wchar_t *args = skip_first_argument(GetCommandLineW());
    size_t command_count = wcslen(client) + wcslen(args) + 5u;
    wchar_t *child_command = (wchar_t *)calloc(command_count, sizeof(wchar_t));
    if (!child_command) {
        MessageBoxW(NULL, L"Failed to allocate launcher command.", L"INTEGRAL EMULATOR", MB_ICONERROR);
        return 1;
    }
    swprintf(child_command, command_count, L"\"%ls\"%ls%ls", client, args[0] ? L" " : L"", args);

    STARTUPINFOW startup;
    PROCESS_INFORMATION process;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    BOOL ok = CreateProcessW(NULL, child_command, NULL, NULL, FALSE, 0, NULL, release_dir,
                             &startup, &process);
    free(child_command);
    if (!ok) {
        MessageBoxW(NULL, L"Failed to start INTEGRAL EMULATOR.", L"INTEGRAL EMULATOR", MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_WINDOWS_PROCESS_H
#define INTEGRAL_WINDOWS_PROCESS_H
#ifdef _WIN32
#include "../runtimes/gb/src/common/utf8_file.h"
#include <process.h>
#include <wchar.h>
/* _wspawnv joins argv without quoting. Escape for the child's CRT, not a shell. */
static inline wchar_t *integral_windows_quote(const wchar_t *arg)
{
    size_t length = wcslen(arg);
    wchar_t *out = malloc((length * 2 + 3) * sizeof(*out));
    if (!out) return NULL;
    wchar_t *p = out; *p++ = L'"';
    while (*arg) {
        size_t slashes = 0;
        while (*arg == L'\\') { slashes++; arg++; }
        size_t count = (*arg == L'"' || !*arg) ? slashes * 2 : slashes;
        while (count--) *p++ = L'\\';
        if (*arg == L'"') *p++ = L'\\';
        if (*arg) *p++ = *arg++;
    }
    *p++ = L'"'; *p = 0;
    return out;
}
static inline intptr_t integral_windows_spawnv(int mode, const char *path, const char *const argv[])
{
    size_t n = 0;
    while (argv[n]) n++;
    wchar_t *program = integral_utf8_wide(path);
    wchar_t **args = calloc(n + 1, sizeof(*args));
    intptr_t result = -1;
    if (!program || !args) goto done;
    for (size_t i = 0; i < n; i++) {
        wchar_t *wide = integral_utf8_wide(argv[i]);
        if (!wide) goto done;
        args[i] = integral_windows_quote(wide);
        free(wide);
        if (!args[i]) goto done;
    }
    result = _wspawnv(mode, program, (const wchar_t *const *)args);
done:;
    int error = errno;
    if (args) { for (size_t i = 0; i < n; i++) free(args[i]); free(args); }
    free(program); errno = error;
    return result;
}
#endif
#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_UTF8_FILE_H
#define INTEGRAL_UTF8_FILE_H
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
static inline wchar_t *integral_utf8_wide(const char *text)
{
    if (!text) { errno = EINVAL; return NULL; }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, NULL, 0);
    if (!n) { errno = EILSEQ; return NULL; }
    wchar_t *wide = malloc((size_t)n * sizeof(*wide));
    if (!wide) return NULL;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, n)) {
        free(wide); errno = EILSEQ; return NULL;
    }
    return wide;
}
#else
#include <unistd.h>
#endif
static inline FILE *integral_fopen(const char *path, const char *mode)
{
#ifdef _WIN32
    wchar_t *p = integral_utf8_wide(path), *m = integral_utf8_wide(mode);
    FILE *file = p && m ? _wfopen(p, m) : NULL;
    int error = errno; free(p); free(m); errno = error;
    return file;
#else
    return fopen(path, mode);
#endif
}
static inline bool integral_file_regular(const char *path)
{
#ifdef _WIN32
    wchar_t *p = integral_utf8_wide(path);
    struct _stat64 st;
    int rc = p ? _wstat64(p, &st) : -1;
    free(p);
    return rc == 0 && (st.st_mode & _S_IFMT) == _S_IFREG;
#else
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
#endif
}
static inline int integral_access(const char *path, int mode)
{
#ifdef _WIN32
    wchar_t *p = integral_utf8_wide(path);
    int rc = p ? _waccess(p, mode) : -1;
    int error = errno; free(p); errno = error;
    return rc;
#else
    return access(path, mode);
#endif
}
#endif

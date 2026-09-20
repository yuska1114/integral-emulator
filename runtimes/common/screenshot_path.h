/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_SCREENSHOT_PATH_H
#define INTEGRAL_SCREENSHOT_PATH_H
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#ifdef _WIN32
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif

/* Mode/role are product-owned labels, not ROM names or user supplied paths. */
static inline int integral_screenshot_path(const char *directory, const char *mode,
                                           const char *role, const char *extension,
                                           char *out, size_t capacity)
{
    static unsigned counter;
    time_t now = time(NULL);
    struct tm *calendar = localtime(&now);
    struct tm stamp;
    if (!calendar || !directory || !mode || !role || !out) return -1;
    stamp = *calendar;
#ifdef _WIN32
    int result = _mkdir(directory);
    unsigned pid = (unsigned)_getpid();
#else
    int result = mkdir(directory, 0755);
    unsigned pid = (unsigned)getpid();
#endif
    if (result != 0 && errno != EEXIST) return -1;
    for (unsigned attempt = 0; attempt < 1000; attempt++) {
        int n = snprintf(out, capacity, "%s/%s_%s_%04d%02d%02d_%02d%02d%02d_%u_%u.%s",
                         directory, mode, role, stamp.tm_year + 1900, stamp.tm_mon + 1,
                         stamp.tm_mday, stamp.tm_hour, stamp.tm_min, stamp.tm_sec,
                         pid, counter++, extension);
        struct stat info;
        if (n < 0 || (size_t)n >= capacity) return -1;
        if (stat(out, &info) != 0 && errno == ENOENT) return 0;
    }
    return -1;
}
#endif

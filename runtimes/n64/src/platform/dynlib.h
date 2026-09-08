/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_PLATFORM_DYNLIB_H
#define INTEGRAL_N64_RUNTIME_PLATFORM_DYNLIB_H

#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

typedef struct IntegralN64RuntimeDynlib {
#ifdef _WIN32
    HMODULE handle;
#else
    void *handle;
#endif
} IntegralN64RuntimeDynlib;

bool integral_n64_runtime_dynlib_open(IntegralN64RuntimeDynlib *library, const char *path);
bool integral_n64_runtime_dynlib_symbol(
    const IntegralN64RuntimeDynlib *library,
    const char *name,
    void *destination,
    size_t destination_size);
void integral_n64_runtime_dynlib_close(IntegralN64RuntimeDynlib *library);
const char *integral_n64_runtime_dynlib_error(void);

#endif

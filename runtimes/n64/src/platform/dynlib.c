/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "dynlib.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32

static char g_error[64];

bool integral_n64_runtime_dynlib_open(IntegralN64RuntimeDynlib *library, const char *path)
{
    library->handle = LoadLibraryA(path);
    return library->handle != NULL;
}

bool integral_n64_runtime_dynlib_symbol(
    const IntegralN64RuntimeDynlib *library,
    const char *name,
    void *destination,
    size_t destination_size)
{
    FARPROC symbol = GetProcAddress(library->handle, name);
    if (symbol == NULL || destination_size != sizeof(symbol)) {
        return false;
    }
    memcpy(destination, &symbol, sizeof(symbol));
    return true;
}

void integral_n64_runtime_dynlib_close(IntegralN64RuntimeDynlib *library)
{
    if (library->handle != NULL) {
        FreeLibrary(library->handle);
        library->handle = NULL;
    }
}

const char *integral_n64_runtime_dynlib_error(void)
{
    (void)snprintf(g_error, sizeof(g_error), "Windows error %lu", GetLastError());
    return g_error;
}

#else

#include <dlfcn.h>

bool integral_n64_runtime_dynlib_open(IntegralN64RuntimeDynlib *library, const char *path)
{
    library->handle = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    return library->handle != NULL;
}

bool integral_n64_runtime_dynlib_symbol(
    const IntegralN64RuntimeDynlib *library,
    const char *name,
    void *destination,
    size_t destination_size)
{
    void *symbol;
    (void)dlerror();
    symbol = dlsym(library->handle, name);
    if (symbol == NULL || dlerror() != NULL || destination_size != sizeof(symbol)) {
        return false;
    }
    memcpy(destination, &symbol, sizeof(symbol));
    return true;
}

void integral_n64_runtime_dynlib_close(IntegralN64RuntimeDynlib *library)
{
    if (library->handle != NULL) {
        (void)dlclose(library->handle);
        library->handle = NULL;
    }
}

const char *integral_n64_runtime_dynlib_error(void)
{
    const char *error = dlerror();
    return error != NULL ? error : "dynamic library operation failed";
}

#endif

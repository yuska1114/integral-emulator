/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_PLATFORM_THREAD_H
#define INTEGRAL_N64_RUNTIME_PLATFORM_THREAD_H

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <pthread.h>
#endif

typedef int (*IntegralN64RuntimeThreadEntry)(void *context);

typedef struct IntegralN64RuntimeThread {
#ifdef _WIN32
    HANDLE handle;
#else
    pthread_t handle;
#endif
    IntegralN64RuntimeThreadEntry entry;
    void *context;
    int started;
} IntegralN64RuntimeThread;

int integral_n64_runtime_thread_start(IntegralN64RuntimeThread *thread,
                           IntegralN64RuntimeThreadEntry entry, void *context);
int integral_n64_runtime_thread_join(IntegralN64RuntimeThread *thread, int *result);
void integral_n64_runtime_thread_sleep(unsigned int milliseconds);

#endif

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform/thread.h"

#include <stdint.h>
#include <string.h>

#ifdef _WIN32
static DWORD WINAPI thread_trampoline(LPVOID context)
{
    IntegralN64RuntimeThread *thread = context;
    return (DWORD)thread->entry(thread->context);
}
#else
#include <errno.h>
#include <time.h>

static void *thread_trampoline(void *context)
{
    IntegralN64RuntimeThread *thread = context;
    return (void *)(intptr_t)thread->entry(thread->context);
}
#endif

int integral_n64_runtime_thread_start(IntegralN64RuntimeThread *thread,
                           IntegralN64RuntimeThreadEntry entry, void *context)
{
    if (thread == NULL || entry == NULL) {
        return -1;
    }
    memset(thread, 0, sizeof(*thread));
    thread->entry = entry;
    thread->context = context;
#ifdef _WIN32
    thread->handle = CreateThread(NULL, 0, thread_trampoline, thread, 0, NULL);
    if (thread->handle == NULL) {
        return -1;
    }
#else
    if (pthread_create(&thread->handle, NULL, thread_trampoline, thread) != 0) {
        return -1;
    }
#endif
    thread->started = 1;
    return 0;
}

int integral_n64_runtime_thread_join(IntegralN64RuntimeThread *thread, int *result)
{
    if (thread == NULL || !thread->started) {
        return -1;
    }
#ifdef _WIN32
    {
        DWORD code;
        if (WaitForSingleObject(thread->handle, INFINITE) != WAIT_OBJECT_0 ||
            !GetExitCodeThread(thread->handle, &code)) {
            return -1;
        }
        CloseHandle(thread->handle);
        if (result != NULL) *result = (int)code;
    }
#else
    {
        void *value = NULL;
        if (pthread_join(thread->handle, &value) != 0) {
            return -1;
        }
        if (result != NULL) *result = (int)(intptr_t)value;
    }
#endif
    thread->started = 0;
    return 0;
}

void integral_n64_runtime_thread_sleep(unsigned int milliseconds)
{
#ifdef _WIN32
    Sleep(milliseconds);
#else
    struct timespec request;
    struct timespec remaining;
    request.tv_sec = (time_t)(milliseconds / 1000u);
    request.tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
    while (nanosleep(&request, &remaining) != 0 && errno == EINTR) {
        request = remaining;
    }
#endif
}

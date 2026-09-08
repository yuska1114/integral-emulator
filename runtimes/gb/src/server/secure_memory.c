/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "secure_memory.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void integral_gb_runtime_secure_zero(void *memory, size_t size)
{
    volatile uint8_t *cursor = (volatile uint8_t *)memory;
    if (!memory) {
        return;
    }
    while (size-- > 0u) {
        *cursor++ = 0u;
    }
}

static bool lock_pages(void *memory, size_t size)
{
    if (!memory || size == 0u) {
        return true;
    }
#ifdef _WIN32
    return VirtualLock(memory, size) != 0;
#else
    return mlock(memory, size) == 0;
#endif
}

static void unlock_pages(void *memory, size_t size)
{
    if (!memory || size == 0u) {
        return;
    }
#ifdef _WIN32
    (void)VirtualUnlock(memory, size);
#else
    (void)munlock(memory, size);
#endif
}

bool integral_gb_runtime_secure_buffer_init(
    IntegralGBRuntimeSecureBuffer *buffer,
    size_t size,
    IntegralGBRuntimeMemoryLockPolicy lock_policy)
{
    uint8_t *copy;
    bool locked;
    if (!buffer || size > INTEGRAL_GB_RUNTIME_SECURE_BUFFER_MAX_SIZE ||
        (lock_policy != INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT &&
         lock_policy != INTEGRAL_GB_RUNTIME_MEMORY_LOCK_REQUIRED)) {
        return false;
    }
    memset(buffer, 0, sizeof(*buffer));
    if (size == 0u) {
        return true;
    }
    copy = (uint8_t *)malloc(size);
    if (!copy) {
        return false;
    }
    memset(copy, 0, size);
    locked = lock_pages(copy, size);
    if (!locked && lock_policy == INTEGRAL_GB_RUNTIME_MEMORY_LOCK_REQUIRED) {
        integral_gb_runtime_secure_zero(copy, size);
        free(copy);
        return false;
    }
    buffer->data = copy;
    buffer->size = size;
    buffer->locked = locked;
    return true;
}

bool integral_gb_runtime_secure_buffer_init_copy(
    IntegralGBRuntimeSecureBuffer *buffer,
    const uint8_t *source,
    size_t size,
    IntegralGBRuntimeMemoryLockPolicy lock_policy)
{
    if ((size != 0u && !source) ||
        !integral_gb_runtime_secure_buffer_init(buffer, size, lock_policy)) {
        return false;
    }
    if (size != 0u) {
        memcpy(buffer->data, source, size);
    }
    return true;
}

void integral_gb_runtime_secure_buffer_clear(
    IntegralGBRuntimeSecureBuffer *buffer)
{
    if (!buffer || !buffer->data) {
        return;
    }
    integral_gb_runtime_secure_zero(buffer->data, buffer->size);
}

void integral_gb_runtime_secure_buffer_release(
    IntegralGBRuntimeSecureBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    if (buffer->data) {
        integral_gb_runtime_secure_zero(buffer->data, buffer->size);
        if (buffer->locked) {
            unlock_pages(buffer->data, buffer->size);
        }
        free(buffer->data);
    }
    memset(buffer, 0, sizeof(*buffer));
}

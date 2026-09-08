/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SECURE_MEMORY_H
#define INTEGRAL_GB_RUNTIME_SECURE_MEMORY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_SECURE_BUFFER_MAX_SIZE 1048576u

typedef enum IntegralGBRuntimeMemoryLockPolicy {
    INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT = 0,
    INTEGRAL_GB_RUNTIME_MEMORY_LOCK_REQUIRED = 1,
} IntegralGBRuntimeMemoryLockPolicy;

typedef struct IntegralGBRuntimeSecureBuffer {
    uint8_t *data;
    size_t size;
    bool locked;
} IntegralGBRuntimeSecureBuffer;

void integral_gb_runtime_secure_zero(void *memory, size_t size);
bool integral_gb_runtime_secure_buffer_init(
    IntegralGBRuntimeSecureBuffer *buffer,
    size_t size,
    IntegralGBRuntimeMemoryLockPolicy lock_policy);
bool integral_gb_runtime_secure_buffer_init_copy(
    IntegralGBRuntimeSecureBuffer *buffer,
    const uint8_t *source,
    size_t size,
    IntegralGBRuntimeMemoryLockPolicy lock_policy);
void integral_gb_runtime_secure_buffer_clear(
    IntegralGBRuntimeSecureBuffer *buffer);
void integral_gb_runtime_secure_buffer_release(
    IntegralGBRuntimeSecureBuffer *buffer);

#endif

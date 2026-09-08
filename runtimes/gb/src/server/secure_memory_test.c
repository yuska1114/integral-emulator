/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "secure_memory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static bool all_zero(const uint8_t *data, size_t size)
{
    size_t index;
    for (index = 0u; index < size; index++) {
        if (data[index] != 0u) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    IntegralGBRuntimeSecureBuffer buffer;
    uint8_t source[256];
    uint8_t stack_secret[37];
    size_t index;
    for (index = 0u; index < sizeof(source); index++) {
        source[index] = (uint8_t)(index ^ 0xa5u);
    }
    memset(stack_secret, 0x5au, sizeof(stack_secret));

    CHECK(!integral_gb_runtime_secure_buffer_init_copy(
        NULL, source, sizeof(source),
        INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
    CHECK(!integral_gb_runtime_secure_buffer_init_copy(
        &buffer, NULL, 1u, INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
    CHECK(!integral_gb_runtime_secure_buffer_init_copy(
        &buffer, source, INTEGRAL_GB_RUNTIME_SECURE_BUFFER_MAX_SIZE + 1u,
        INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
    CHECK(!integral_gb_runtime_secure_buffer_init_copy(
        &buffer, source, sizeof(source),
        (IntegralGBRuntimeMemoryLockPolicy)99));
    CHECK(!integral_gb_runtime_secure_buffer_init(
        &buffer, INTEGRAL_GB_RUNTIME_SECURE_BUFFER_MAX_SIZE + 1u,
        INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));

    CHECK(integral_gb_runtime_secure_buffer_init_copy(
        &buffer, NULL, 0u, INTEGRAL_GB_RUNTIME_MEMORY_LOCK_REQUIRED));
    CHECK(buffer.data == NULL && buffer.size == 0u && !buffer.locked);
    integral_gb_runtime_secure_buffer_release(&buffer);

    CHECK(integral_gb_runtime_secure_buffer_init(
        &buffer, sizeof(source),
        INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
    CHECK(buffer.size == sizeof(source));
    CHECK(all_zero(buffer.data, buffer.size));
    integral_gb_runtime_secure_buffer_release(&buffer);

    CHECK(integral_gb_runtime_secure_buffer_init_copy(
        &buffer, source, sizeof(source),
        INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
    CHECK(buffer.data != source);
    CHECK(buffer.size == sizeof(source));
    CHECK(memcmp(buffer.data, source, sizeof(source)) == 0);
    source[0] ^= 0xffu;
    CHECK(buffer.data[0] != source[0]);
    integral_gb_runtime_secure_buffer_clear(&buffer);
    CHECK(all_zero(buffer.data, buffer.size));
    integral_gb_runtime_secure_buffer_release(&buffer);
    CHECK(buffer.data == NULL && buffer.size == 0u && !buffer.locked);

    integral_gb_runtime_secure_zero(stack_secret, sizeof(stack_secret));
    CHECK(all_zero(stack_secret, sizeof(stack_secret)));
    integral_gb_runtime_secure_zero(NULL, 10u);
    integral_gb_runtime_secure_buffer_clear(NULL);
    integral_gb_runtime_secure_buffer_release(NULL);

    printf("PASS secure memory checks=%u\n", checks);
    return 0;
}

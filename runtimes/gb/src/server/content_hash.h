/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_CONTENT_HASH_H
#define INTEGRAL_GB_RUNTIME_CONTENT_HASH_H

#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_CONTENT_SHA256_SIZE 32u

typedef struct IntegralGBRuntimeContentSha256 {
    uint32_t h[8];
    uint64_t bytes;
    uint8_t block[64];
    size_t block_size;
} IntegralGBRuntimeContentSha256;

void integral_gb_runtime_content_sha256_init(IntegralGBRuntimeContentSha256 *state);
void integral_gb_runtime_content_sha256_update(IntegralGBRuntimeContentSha256 *state,
                                    const void *data,
                                    size_t size);
void integral_gb_runtime_content_sha256_finish(IntegralGBRuntimeContentSha256 *state,
                                    uint8_t digest[INTEGRAL_GB_RUNTIME_CONTENT_SHA256_SIZE]);
void integral_gb_runtime_content_sha256(const void *data,
                             size_t size,
                             uint8_t digest[INTEGRAL_GB_RUNTIME_CONTENT_SHA256_SIZE]);
void integral_gb_runtime_content_sha256_hex(const uint8_t digest[INTEGRAL_GB_RUNTIME_CONTENT_SHA256_SIZE],
                                 char hex[65]);

#endif

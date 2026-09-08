/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_MOBILE_BOUNDED_BUFFER_H
#define INTEGRAL_GB_RUNTIME_MOBILE_BOUNDED_BUFFER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum IntegralGBRuntimeMobileBufferState {
    INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_OPEN = 0,
    INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_SEALED,
    INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_CANCELED,
    INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_TIMED_OUT,
} IntegralGBRuntimeMobileBufferState;

typedef struct IntegralGBRuntimeMobileBoundedBuffer {
    uint8_t *data;
    size_t capacity;
    size_t length;
    size_t cursor;
    IntegralGBRuntimeMobileBufferState state;
} IntegralGBRuntimeMobileBoundedBuffer;

void integral_gb_runtime_mobile_bounded_buffer_init(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                            uint8_t *storage,
                                            size_t capacity);
void integral_gb_runtime_mobile_bounded_buffer_reset(IntegralGBRuntimeMobileBoundedBuffer *buffer);
bool integral_gb_runtime_mobile_bounded_buffer_append(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                              const void *data,
                                              size_t size);
bool integral_gb_runtime_mobile_bounded_buffer_truncate(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                                size_t length);
bool integral_gb_runtime_mobile_bounded_buffer_seal(IntegralGBRuntimeMobileBoundedBuffer *buffer);
void integral_gb_runtime_mobile_bounded_buffer_cancel(IntegralGBRuntimeMobileBoundedBuffer *buffer);
void integral_gb_runtime_mobile_bounded_buffer_timeout(IntegralGBRuntimeMobileBoundedBuffer *buffer);
int integral_gb_runtime_mobile_bounded_buffer_read(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                           void *destination,
                                           size_t size);

#endif

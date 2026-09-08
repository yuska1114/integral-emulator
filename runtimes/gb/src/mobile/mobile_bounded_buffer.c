/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_bounded_buffer.h"

#include <limits.h>
#include <string.h>

void integral_gb_runtime_mobile_bounded_buffer_init(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                            uint8_t *storage,
                                            size_t capacity)
{
    if (!buffer) {
        return;
    }
    memset(buffer, 0, sizeof(*buffer));
    buffer->data = storage;
    buffer->capacity = capacity;
}

void integral_gb_runtime_mobile_bounded_buffer_reset(IntegralGBRuntimeMobileBoundedBuffer *buffer)
{
    if (!buffer) {
        return;
    }
    buffer->length = 0;
    buffer->cursor = 0;
    buffer->state = INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_OPEN;
}

bool integral_gb_runtime_mobile_bounded_buffer_append(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                              const void *data,
                                              size_t size)
{
    if (!buffer || !buffer->data || (!data && size != 0) ||
        buffer->state != INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_OPEN ||
        size > buffer->capacity - buffer->length) {
        return false;
    }
    if (size != 0) {
        memcpy(buffer->data + buffer->length, data, size);
        buffer->length += size;
    }
    return true;
}

bool integral_gb_runtime_mobile_bounded_buffer_truncate(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                                size_t length)
{
    if (!buffer || length > buffer->length || buffer->cursor > length ||
        buffer->state == INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_CANCELED ||
        buffer->state == INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_TIMED_OUT) {
        return false;
    }
    buffer->length = length;
    return true;
}

bool integral_gb_runtime_mobile_bounded_buffer_seal(IntegralGBRuntimeMobileBoundedBuffer *buffer)
{
    if (!buffer || buffer->state != INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_OPEN) {
        return false;
    }
    buffer->state = INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_SEALED;
    return true;
}

void integral_gb_runtime_mobile_bounded_buffer_cancel(IntegralGBRuntimeMobileBoundedBuffer *buffer)
{
    if (buffer) {
        buffer->state = INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_CANCELED;
    }
}

void integral_gb_runtime_mobile_bounded_buffer_timeout(IntegralGBRuntimeMobileBoundedBuffer *buffer)
{
    if (buffer) {
        buffer->state = INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_TIMED_OUT;
    }
}

int integral_gb_runtime_mobile_bounded_buffer_read(IntegralGBRuntimeMobileBoundedBuffer *buffer,
                                           void *destination,
                                           size_t size)
{
    if (!buffer || (!destination && size != 0) ||
        buffer->state == INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_CANCELED ||
        buffer->state == INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_TIMED_OUT) {
        return -1;
    }
    if (buffer->cursor == buffer->length) {
        return buffer->state == INTEGRAL_GB_RUNTIME_MOBILE_BUFFER_SEALED ? -2 : 0;
    }
    size_t remaining = buffer->length - buffer->cursor;
    size_t count = remaining < size ? remaining : size;
    if (count > (size_t)INT_MAX) {
        count = INT_MAX;
    }
    if (count != 0) {
        memcpy(destination, buffer->data + buffer->cursor, count);
        buffer->cursor += count;
    }
    return (int)count;
}

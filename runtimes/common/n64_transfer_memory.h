/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_TRANSFER_MEMORY_H
#define INTEGRAL_N64_TRANSFER_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#define INTEGRAL_TRANSFER_MEMORY_VERSION 1u
/* Borrowed RAM belongs to the frontend until the Core has shut down. */
typedef struct IntegralTransferMemory {
    uint8_t *data;
    size_t size;
    uint8_t rtc[48];
    int has_rtc;
} IntegralTransferMemory;
static inline uint8_t *integral_transfer_memory_data(const void *storage)
{ return ((const IntegralTransferMemory *)storage)->data; }
static inline size_t integral_transfer_memory_size(const void *storage)
{ return ((const IntegralTransferMemory *)storage)->size; }
static inline void integral_transfer_memory_save(void *storage, size_t start, size_t size)
{ (void)storage; (void)start; (void)size; /* NO SAV OVERWRITE: already in RAM. */ }
#endif

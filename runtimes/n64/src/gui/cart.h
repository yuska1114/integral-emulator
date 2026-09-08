/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_GUI_CART_H
#define INTEGRAL_N64_RUNTIME_GUI_CART_H

#include <stddef.h>

enum {
    INTEGRAL_N64_RUNTIME_CART_MAX = 128,
    INTEGRAL_N64_RUNTIME_CART_PATH_MAX = 1024,
};

typedef struct IntegralN64RuntimeCartCatalog {
    char roms[INTEGRAL_N64_RUNTIME_CART_MAX][INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char saves[INTEGRAL_N64_RUNTIME_CART_MAX][INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    unsigned count;
} IntegralN64RuntimeCartCatalog;

void integral_n64_runtime_cart_catalog_scan(IntegralN64RuntimeCartCatalog *catalog);
int integral_n64_runtime_cart_catalog_find(const IntegralN64RuntimeCartCatalog *catalog,
                                const char *rom_path);
const char *integral_n64_runtime_cart_name(const IntegralN64RuntimeCartCatalog *catalog,
                                unsigned choice);
int integral_n64_runtime_cart_prepare_slot(const IntegralN64RuntimeCartCatalog *catalog,
                                unsigned choice, unsigned slot,
                                const char *storage);

#endif

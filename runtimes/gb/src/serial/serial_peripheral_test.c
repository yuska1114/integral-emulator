/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "serial_peripheral.h"

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

static void test_start(GB_gameboy_t *gb, bool bit)
{
    (void)gb;
    (void)bit;
}

static bool test_end(GB_gameboy_t *gb)
{
    (void)gb;
    return true;
}

int main(void)
{
    GB_gameboy_t *gb = calloc(1, sizeof(*gb));
    assert(gb);

    IntegralGBRuntimeSerialPeripheralRouter router;
    integral_gb_runtime_serial_peripheral_router_init(&router, gb);
    assert(integral_gb_runtime_serial_peripheral_router_active(&router) ==
           INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE);

    assert(integral_gb_runtime_serial_peripheral_router_attach(
               &router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK, test_start, test_end) == 0);
    assert(integral_gb_runtime_serial_peripheral_router_active(&router) ==
           INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK);
    assert(integral_gb_runtime_serial_peripheral_router_attach(
               &router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_MOBILE, test_start, test_end) != 0);
    assert(integral_gb_runtime_serial_peripheral_router_detach(
               &router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_MOBILE) != 0);
    assert(integral_gb_runtime_serial_peripheral_router_active(&router) ==
           INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK);

    assert(integral_gb_runtime_serial_peripheral_router_detach(
               &router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK) == 0);

    assert(integral_gb_runtime_serial_peripheral_router_attach(
               &router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_MOBILE, test_start, test_end) == 0);
    integral_gb_runtime_serial_peripheral_router_reset(&router);
    assert(integral_gb_runtime_serial_peripheral_router_active(&router) ==
           INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE);

    assert(integral_gb_runtime_serial_peripheral_router_attach(
               &router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK, test_start, test_end) == 0);
    integral_gb_runtime_serial_peripheral_router_shutdown(&router);
    assert(!router.gb);
    assert(integral_gb_runtime_serial_peripheral_router_active(&router) ==
           INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE);

    free(gb);
    puts("serial peripheral router tests passed");
    return 0;
}

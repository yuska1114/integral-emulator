/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "serial_peripheral.h"

#include <string.h>

void integral_gb_runtime_serial_peripheral_router_init(IntegralGBRuntimeSerialPeripheralRouter *router,
                                               GB_gameboy_t *gb)
{
    if (!router) {
        return;
    }
    memset(router, 0, sizeof(*router));
    router->gb = gb;
}

int integral_gb_runtime_serial_peripheral_router_attach(
    IntegralGBRuntimeSerialPeripheralRouter *router,
    IntegralGBRuntimeSerialPeripheralType type,
    GB_serial_transfer_bit_start_callback_t start_callback,
    GB_serial_transfer_bit_end_callback_t end_callback)
{
    if (!router || !router->gb || type == INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE ||
        !start_callback || !end_callback ||
        router->active_type != INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE) {
        return -1;
    }

    GB_set_serial_transfer_bit_start_callback(router->gb, start_callback);
    GB_set_serial_transfer_bit_end_callback(router->gb, end_callback);
    router->active_type = type;
    return 0;
}

int integral_gb_runtime_serial_peripheral_router_detach(IntegralGBRuntimeSerialPeripheralRouter *router,
                                                IntegralGBRuntimeSerialPeripheralType type)
{
    if (!router || !router->gb || type == INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE ||
        router->active_type != type) {
        return -1;
    }

    GB_set_serial_transfer_bit_start_callback(router->gb, NULL);
    GB_set_serial_transfer_bit_end_callback(router->gb, NULL);
    router->active_type = INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE;
    return 0;
}

void integral_gb_runtime_serial_peripheral_router_reset(IntegralGBRuntimeSerialPeripheralRouter *router)
{
    if (!router || !router->gb) {
        return;
    }
    GB_set_serial_transfer_bit_start_callback(router->gb, NULL);
    GB_set_serial_transfer_bit_end_callback(router->gb, NULL);
    router->active_type = INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE;
}

void integral_gb_runtime_serial_peripheral_router_shutdown(IntegralGBRuntimeSerialPeripheralRouter *router)
{
    integral_gb_runtime_serial_peripheral_router_reset(router);
    if (router) {
        router->gb = NULL;
    }
}

IntegralGBRuntimeSerialPeripheralType integral_gb_runtime_serial_peripheral_router_active(
    const IntegralGBRuntimeSerialPeripheralRouter *router)
{
    return router ? router->active_type : INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE;
}

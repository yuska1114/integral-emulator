/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_H
#define INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_H

#include "gb.h"

typedef enum IntegralGBRuntimeSerialPeripheralType {
    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_NONE = 0,
    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK,
    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_MOBILE,
} IntegralGBRuntimeSerialPeripheralType;

typedef struct IntegralGBRuntimeSerialPeripheralRouter {
    GB_gameboy_t *gb;
    IntegralGBRuntimeSerialPeripheralType active_type;
} IntegralGBRuntimeSerialPeripheralRouter;

void integral_gb_runtime_serial_peripheral_router_init(IntegralGBRuntimeSerialPeripheralRouter *router,
                                               GB_gameboy_t *gb);
int integral_gb_runtime_serial_peripheral_router_attach(
    IntegralGBRuntimeSerialPeripheralRouter *router,
    IntegralGBRuntimeSerialPeripheralType type,
    GB_serial_transfer_bit_start_callback_t start_callback,
    GB_serial_transfer_bit_end_callback_t end_callback);
int integral_gb_runtime_serial_peripheral_router_detach(IntegralGBRuntimeSerialPeripheralRouter *router,
                                                IntegralGBRuntimeSerialPeripheralType type);
void integral_gb_runtime_serial_peripheral_router_reset(IntegralGBRuntimeSerialPeripheralRouter *router);
void integral_gb_runtime_serial_peripheral_router_shutdown(IntegralGBRuntimeSerialPeripheralRouter *router);
IntegralGBRuntimeSerialPeripheralType integral_gb_runtime_serial_peripheral_router_active(
    const IntegralGBRuntimeSerialPeripheralRouter *router);

#endif

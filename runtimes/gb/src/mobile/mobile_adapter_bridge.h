/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_BRIDGE_H
#define INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "gb.h"
#include "mobile.h"
#include "../serial/serial_peripheral.h"

typedef struct IntegralGBRuntimeMobileAdapterBridge {
    struct mobile_adapter *adapter;
    GB_gameboy_t *gb;
    FILE *trace;
    IntegralGBRuntimeSerialPeripheralRouter *serial_router;
    uint8_t incoming_byte;
    uint8_t next_adapter_byte;
    uint8_t incoming_bit_index;
    bool transfer_active;
    bool connected;
    unsigned byte_transfers;
    unsigned callback_errors;
} IntegralGBRuntimeMobileAdapterBridge;

int integral_gb_runtime_mobile_adapter_bridge_connect(IntegralGBRuntimeMobileAdapterBridge *bridge,
                                              GB_gameboy_t *gb,
                                              IntegralGBRuntimeSerialPeripheralRouter *serial_router,
                                              struct mobile_adapter *adapter,
                                              FILE *trace);
void integral_gb_runtime_mobile_adapter_bridge_reset_pipeline(IntegralGBRuntimeMobileAdapterBridge *bridge);
void integral_gb_runtime_mobile_adapter_bridge_pump(IntegralGBRuntimeMobileAdapterBridge *bridge);
void integral_gb_runtime_mobile_adapter_bridge_disconnect(IntegralGBRuntimeMobileAdapterBridge *bridge);
void integral_gb_runtime_mobile_adapter_bridge_print_stats(const IntegralGBRuntimeMobileAdapterBridge *bridge);

#endif

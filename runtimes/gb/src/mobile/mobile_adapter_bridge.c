/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_adapter_bridge.h"

#include <string.h>

#include "memory.h"

static IntegralGBRuntimeMobileAdapterBridge *active_bridge;

static void serial_start(GB_gameboy_t *gb, bool bit_to_send)
{
    (void)bit_to_send;
    IntegralGBRuntimeMobileAdapterBridge *bridge = active_bridge;
    if (!bridge || !bridge->connected || bridge->gb != gb) {
        return;
    }
    if (bridge->transfer_active) {
        return;
    }

    uint8_t outgoing = GB_read_memory(gb, 0xFF00 + GB_IO_SB);
    bridge->incoming_byte = bridge->next_adapter_byte;
    bridge->next_adapter_byte = mobile_transfer(bridge->adapter, outgoing);
    bridge->incoming_bit_index = 0;
    bridge->transfer_active = true;
    bridge->byte_transfers++;

    if (bridge->trace) {
        fprintf(bridge->trace,
                "SERIAL BYTE %u TX=%02X RX=%02X NEXT=%02X\n",
                bridge->byte_transfers,
                outgoing,
                bridge->incoming_byte,
                bridge->next_adapter_byte);
        fflush(bridge->trace);
    }
}

static bool serial_end(GB_gameboy_t *gb)
{
    IntegralGBRuntimeMobileAdapterBridge *bridge = active_bridge;
    if (!bridge || !bridge->connected || bridge->gb != gb || !bridge->transfer_active) {
        if (bridge) {
            bridge->callback_errors++;
        }
        return true;
    }

    bool incoming = (bridge->incoming_byte &
                     (uint8_t)(0x80u >> bridge->incoming_bit_index)) != 0;
    bridge->incoming_bit_index++;
    if (bridge->incoming_bit_index == 8) {
        bridge->incoming_bit_index = 0;
        bridge->transfer_active = false;
    }
    return incoming;
}

int integral_gb_runtime_mobile_adapter_bridge_connect(IntegralGBRuntimeMobileAdapterBridge *bridge,
                                              GB_gameboy_t *gb,
                                              IntegralGBRuntimeSerialPeripheralRouter *serial_router,
                                              struct mobile_adapter *adapter,
                                              FILE *trace)
{
    if (!bridge || !gb || !serial_router || serial_router->gb != gb || !adapter || active_bridge) {
        return -1;
    }
    memset(bridge, 0, sizeof(*bridge));
    bridge->adapter = adapter;
    bridge->gb = gb;
    bridge->trace = trace;
    bridge->next_adapter_byte = MOBILE_SERIAL_IDLE_BYTE;
    bridge->serial_router = serial_router;
    active_bridge = bridge;
    if (integral_gb_runtime_serial_peripheral_router_attach(bridge->serial_router,
                                                    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_MOBILE,
                                                    serial_start,
                                                    serial_end) != 0) {
        active_bridge = NULL;
        return -1;
    }
    bridge->connected = true;
    return 0;
}

void integral_gb_runtime_mobile_adapter_bridge_reset_pipeline(IntegralGBRuntimeMobileAdapterBridge *bridge)
{
    if (!bridge) {
        return;
    }
    bridge->incoming_byte = MOBILE_SERIAL_IDLE_BYTE;
    bridge->next_adapter_byte = MOBILE_SERIAL_IDLE_BYTE;
    bridge->incoming_bit_index = 0;
    bridge->transfer_active = false;
}

void integral_gb_runtime_mobile_adapter_bridge_pump(IntegralGBRuntimeMobileAdapterBridge *bridge)
{
    if (bridge && bridge->connected) {
        mobile_loop(bridge->adapter);
    }
}

void integral_gb_runtime_mobile_adapter_bridge_disconnect(IntegralGBRuntimeMobileAdapterBridge *bridge)
{
    if (!bridge || !bridge->connected) {
        return;
    }
    (void)integral_gb_runtime_serial_peripheral_router_detach(
        bridge->serial_router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_MOBILE);
    bridge->connected = false;
    bridge->transfer_active = false;
    if (active_bridge == bridge) {
        active_bridge = NULL;
    }
}

void integral_gb_runtime_mobile_adapter_bridge_print_stats(const IntegralGBRuntimeMobileAdapterBridge *bridge)
{
    if (!bridge) {
        return;
    }
    printf("  mobile serial bytes: %u\n", bridge->byte_transfers);
    printf("  mobile callback errors: %u\n", bridge->callback_errors);
}

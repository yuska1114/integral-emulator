/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_LINK_BRIDGE_H
#define INTEGRAL_GB_RUNTIME_LINK_BRIDGE_H

#include <stdbool.h>
#include <stdint.h>

#include "slot.h"

typedef struct IntegralGBRuntimeLinkBridge {
    IntegralGBRuntimeSlot *slot1;
    IntegralGBRuntimeSlot *slot2;
    bool slot1_bit_to_send;
    bool slot2_bit_to_send;
    bool connected;
    unsigned slot1_serial_starts;
    unsigned slot1_serial_ends;
    unsigned slot2_serial_starts;
    unsigned slot2_serial_ends;
    unsigned slot1_peer_wait_assists;
    unsigned slot2_peer_wait_assists;
    unsigned slot1_peer_wait_misses;
    unsigned slot2_peer_wait_misses;
    uint8_t slot1_tx_shift;
    uint8_t slot1_tx_bit_count;
    uint8_t slot1_rx_shift;
    uint8_t slot1_rx_bit_count;
    uint8_t slot2_tx_shift;
    uint8_t slot2_tx_bit_count;
    uint8_t slot2_rx_shift;
    uint8_t slot2_rx_bit_count;
    uint8_t slot1_tx_sample[32];
    uint8_t slot1_rx_sample[32];
    uint8_t slot2_tx_sample[32];
    uint8_t slot2_rx_sample[32];
    unsigned slot1_tx_bytes;
    unsigned slot1_rx_bytes;
    unsigned slot2_tx_bytes;
    unsigned slot2_rx_bytes;
    unsigned slot1_tx_nonzero_bytes;
    unsigned slot1_rx_nonzero_bytes;
    unsigned slot2_tx_nonzero_bytes;
    unsigned slot2_rx_nonzero_bytes;
    unsigned slot1_ir_edges;
    unsigned slot2_ir_edges;
    char slot1_ir_sample[65];
    char slot2_ir_sample[65];
    unsigned current_frame;
    unsigned slot1_syncs;
    unsigned slot2_syncs;
    unsigned ir_trace_count;
    uint8_t ir_trace_slot[512];
    uint8_t ir_trace_value[512];
    unsigned ir_trace_frame[512];
    unsigned ir_trace_slot1_syncs[512];
    unsigned ir_trace_slot2_syncs[512];
    unsigned slot1_trace_count;
    unsigned slot1_trace_assists[256];
    unsigned slot1_trace_ir1_delta[256];
    unsigned slot1_trace_ir2_delta[256];
    uint8_t slot1_trace_incoming[256];
    uint8_t slot1_trace_outgoing[256];
    uint8_t slot1_trace_peer_bit_before[256];
    uint8_t slot1_trace_peer_bit_after[256];
    unsigned slot1_trace_last_assists;
    unsigned slot1_trace_last_ir1;
    unsigned slot1_trace_last_ir2;
    bool synchronizing_peer;
} IntegralGBRuntimeLinkBridge;

int integral_gb_runtime_link_bridge_connect(IntegralGBRuntimeLinkBridge *bridge,
                                   IntegralGBRuntimeSlot *slot1,
                                   IntegralGBRuntimeSlot *slot2);
void integral_gb_runtime_link_bridge_disconnect(IntegralGBRuntimeLinkBridge *bridge);
void integral_gb_runtime_link_bridge_print_stats(const IntegralGBRuntimeLinkBridge *bridge);

#endif

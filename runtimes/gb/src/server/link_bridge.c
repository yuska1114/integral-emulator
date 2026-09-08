/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "link_bridge.h"

#include <stdio.h>
#include <string.h>

static IntegralGBRuntimeLinkBridge *active_bridge;

static void record_byte(uint8_t *sample, unsigned *count, unsigned *nonzero_count, uint8_t byte)
{
    if (*count < 32) {
        sample[*count] = byte;
    }
    (*count)++;
    if (byte != 0) {
        (*nonzero_count)++;
    }
}

static void record_bit(uint8_t *shift,
                       uint8_t *bit_count,
                       uint8_t *sample,
                       unsigned *byte_count,
                       unsigned *nonzero_count,
                       bool bit)
{
    *shift = (uint8_t)((*shift << 1) | (bit ? 1u : 0u));
    (*bit_count)++;
    if (*bit_count == 8) {
        record_byte(sample, byte_count, nonzero_count, *shift);
        *shift = 0;
        *bit_count = 0;
    }
}

static void print_sample(const char *label, const uint8_t *sample, unsigned byte_count, unsigned nonzero_count)
{
    unsigned shown = byte_count < 32 ? byte_count : 32;
    printf("  link %s bytes: %u nonzero=%u", label, byte_count, nonzero_count);
    if (shown > 0) {
        printf(" first:");
        for (unsigned i = 0; i < shown; i++) {
            printf(" %02X", sample[i]);
        }
    }
    printf("\n");
}

static void run_peer_until_serial_active(IntegralGBRuntimeLinkBridge *bridge,
                                         IntegralGBRuntimeSlot *peer,
                                         unsigned *assists,
                                         unsigned *misses)
{
    if (bridge->synchronizing_peer || integral_gb_runtime_slot_serial_active(peer)) {
        return;
    }

    bridge->synchronizing_peer = true;
    for (unsigned i = 0; i < 64 && !integral_gb_runtime_slot_serial_active(peer); i++) {
        (void)integral_gb_runtime_slot_run_until_sync(peer);
        if (peer == bridge->slot1) {
            bridge->slot1_syncs++;
        }
        else if (peer == bridge->slot2) {
            bridge->slot2_syncs++;
        }
        (*assists)++;
    }
    if (!integral_gb_runtime_slot_serial_active(peer)) {
        (*misses)++;
    }
    bridge->synchronizing_peer = false;
}

static void record_ir_trace(IntegralGBRuntimeLinkBridge *bridge, unsigned slot, bool output)
{
    if (bridge->ir_trace_count >= 512) {
        return;
    }
    unsigned index = bridge->ir_trace_count++;
    bridge->ir_trace_slot[index] = (uint8_t)slot;
    bridge->ir_trace_value[index] = output ? 1u : 0u;
    bridge->ir_trace_frame[index] = bridge->current_frame;
    bridge->ir_trace_slot1_syncs[index] = bridge->slot1_syncs;
    bridge->ir_trace_slot2_syncs[index] = bridge->slot2_syncs;
}

static void slot1_serial_start(GB_gameboy_t *gb, bool bit_to_send)
{
    (void)gb;
    if (active_bridge) {
        active_bridge->slot1_bit_to_send = bit_to_send;
        active_bridge->slot1_serial_starts++;
    }
}

static bool slot1_serial_end(GB_gameboy_t *gb)
{
    (void)gb;
    if (!active_bridge || !active_bridge->connected) {
        return true;
    }

    active_bridge->slot1_serial_ends++;
    bool peer_bit_before = GB_serial_get_data_bit(active_bridge->slot2->gb);
    run_peer_until_serial_active(active_bridge,
                                 active_bridge->slot2,
                                 &active_bridge->slot1_peer_wait_assists,
                                 &active_bridge->slot1_peer_wait_misses);
    bool incoming = GB_serial_get_data_bit(active_bridge->slot2->gb);
    GB_serial_set_data_bit(active_bridge->slot2->gb, active_bridge->slot1_bit_to_send);
    bool peer_bit_after = GB_serial_get_data_bit(active_bridge->slot2->gb);
    if (active_bridge->slot1_trace_count < 256) {
        unsigned index = active_bridge->slot1_trace_count++;
        active_bridge->slot1_trace_assists[index] =
            active_bridge->slot1_peer_wait_assists - active_bridge->slot1_trace_last_assists;
        active_bridge->slot1_trace_ir1_delta[index] =
            active_bridge->slot1_ir_edges - active_bridge->slot1_trace_last_ir1;
        active_bridge->slot1_trace_ir2_delta[index] =
            active_bridge->slot2_ir_edges - active_bridge->slot1_trace_last_ir2;
        active_bridge->slot1_trace_incoming[index] = incoming ? 1u : 0u;
        active_bridge->slot1_trace_outgoing[index] = active_bridge->slot1_bit_to_send ? 1u : 0u;
        active_bridge->slot1_trace_peer_bit_before[index] = peer_bit_before ? 1u : 0u;
        active_bridge->slot1_trace_peer_bit_after[index] = peer_bit_after ? 1u : 0u;
        active_bridge->slot1_trace_last_assists = active_bridge->slot1_peer_wait_assists;
        active_bridge->slot1_trace_last_ir1 = active_bridge->slot1_ir_edges;
        active_bridge->slot1_trace_last_ir2 = active_bridge->slot2_ir_edges;
    }
    record_bit(&active_bridge->slot1_tx_shift,
               &active_bridge->slot1_tx_bit_count,
               active_bridge->slot1_tx_sample,
               &active_bridge->slot1_tx_bytes,
               &active_bridge->slot1_tx_nonzero_bytes,
               active_bridge->slot1_bit_to_send);
    record_bit(&active_bridge->slot1_rx_shift,
               &active_bridge->slot1_rx_bit_count,
               active_bridge->slot1_rx_sample,
               &active_bridge->slot1_rx_bytes,
               &active_bridge->slot1_rx_nonzero_bytes,
               incoming);
    return incoming;
}

static void slot2_serial_start(GB_gameboy_t *gb, bool bit_to_send)
{
    (void)gb;
    if (active_bridge) {
        active_bridge->slot2_bit_to_send = bit_to_send;
        active_bridge->slot2_serial_starts++;
    }
}

static bool slot2_serial_end(GB_gameboy_t *gb)
{
    (void)gb;
    if (!active_bridge || !active_bridge->connected) {
        return true;
    }

    active_bridge->slot2_serial_ends++;
    run_peer_until_serial_active(active_bridge,
                                 active_bridge->slot1,
                                 &active_bridge->slot2_peer_wait_assists,
                                 &active_bridge->slot2_peer_wait_misses);
    bool incoming = GB_serial_get_data_bit(active_bridge->slot1->gb);
    GB_serial_set_data_bit(active_bridge->slot1->gb, active_bridge->slot2_bit_to_send);
    record_bit(&active_bridge->slot2_tx_shift,
               &active_bridge->slot2_tx_bit_count,
               active_bridge->slot2_tx_sample,
               &active_bridge->slot2_tx_bytes,
               &active_bridge->slot2_tx_nonzero_bytes,
               active_bridge->slot2_bit_to_send);
    record_bit(&active_bridge->slot2_rx_shift,
               &active_bridge->slot2_rx_bit_count,
               active_bridge->slot2_rx_sample,
               &active_bridge->slot2_rx_bytes,
               &active_bridge->slot2_rx_nonzero_bytes,
               incoming);
    return incoming;
}

static void slot1_infrared(GB_gameboy_t *gb, bool output)
{
    (void)gb;
    if (active_bridge && active_bridge->connected) {
        if (active_bridge->slot1_ir_edges < sizeof(active_bridge->slot1_ir_sample) - 1) {
            active_bridge->slot1_ir_sample[active_bridge->slot1_ir_edges] = output ? '1' : '0';
            active_bridge->slot1_ir_sample[active_bridge->slot1_ir_edges + 1] = '\0';
        }
        active_bridge->slot1_ir_edges++;
        record_ir_trace(active_bridge, 1, output);
        GB_set_infrared_input(active_bridge->slot2->gb, output);
    }
}

static void slot2_infrared(GB_gameboy_t *gb, bool output)
{
    (void)gb;
    if (active_bridge && active_bridge->connected) {
        if (active_bridge->slot2_ir_edges < sizeof(active_bridge->slot2_ir_sample) - 1) {
            active_bridge->slot2_ir_sample[active_bridge->slot2_ir_edges] = output ? '1' : '0';
            active_bridge->slot2_ir_sample[active_bridge->slot2_ir_edges + 1] = '\0';
        }
        active_bridge->slot2_ir_edges++;
        record_ir_trace(active_bridge, 2, output);
        GB_set_infrared_input(active_bridge->slot1->gb, output);
    }
}

int integral_gb_runtime_link_bridge_connect(IntegralGBRuntimeLinkBridge *bridge,
                                   IntegralGBRuntimeSlot *slot1,
                                   IntegralGBRuntimeSlot *slot2)
{
    if (!bridge || !slot1 || !slot1->gb || !slot2 || !slot2->gb || active_bridge) {
        return -1;
    }
    memset(bridge, 0, sizeof(*bridge));
    bridge->slot1 = slot1;
    bridge->slot2 = slot2;
    bridge->slot1_bit_to_send = true;
    bridge->slot2_bit_to_send = true;
    active_bridge = bridge;

    if (integral_gb_runtime_serial_peripheral_router_attach(&slot1->serial_router,
                                                    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK,
                                                    slot1_serial_start,
                                                    slot1_serial_end) != 0 ||
        integral_gb_runtime_serial_peripheral_router_attach(&slot2->serial_router,
                                                    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK,
                                                    slot2_serial_start,
                                                    slot2_serial_end) != 0) {
        if (integral_gb_runtime_serial_peripheral_router_active(&slot1->serial_router) ==
            INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK) {
            (void)integral_gb_runtime_serial_peripheral_router_detach(
                &slot1->serial_router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK);
        }
        active_bridge = NULL;
        return -1;
    }
    GB_set_infrared_callback(slot1->gb, slot1_infrared);
    GB_set_infrared_callback(slot2->gb, slot2_infrared);
    bridge->connected = true;
    return 0;
}

void integral_gb_runtime_link_bridge_disconnect(IntegralGBRuntimeLinkBridge *bridge)
{
    if (!bridge || !bridge->connected) {
        return;
    }

    (void)integral_gb_runtime_serial_peripheral_router_detach(
        &bridge->slot1->serial_router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK);
    (void)integral_gb_runtime_serial_peripheral_router_detach(
        &bridge->slot2->serial_router, INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK);
    GB_set_infrared_callback(bridge->slot1->gb, NULL);
    GB_set_infrared_callback(bridge->slot2->gb, NULL);
    bridge->connected = false;
    if (active_bridge == bridge) {
        active_bridge = NULL;
    }
}

void integral_gb_runtime_link_bridge_print_stats(const IntegralGBRuntimeLinkBridge *bridge)
{
    if (!bridge) {
        return;
    }

    printf("  link serial starts: slot1=%u slot2=%u\n",
           bridge->slot1_serial_starts,
           bridge->slot2_serial_starts);
    printf("  link serial ends: slot1=%u slot2=%u\n",
           bridge->slot1_serial_ends,
           bridge->slot2_serial_ends);
    printf("  link peer wait assists: slot1=%u slot2=%u\n",
           bridge->slot1_peer_wait_assists,
           bridge->slot2_peer_wait_assists);
    printf("  link peer wait misses: slot1=%u slot2=%u\n",
           bridge->slot1_peer_wait_misses,
           bridge->slot2_peer_wait_misses);
    print_sample("slot1 tx",
                 bridge->slot1_tx_sample,
                 bridge->slot1_tx_bytes,
                 bridge->slot1_tx_nonzero_bytes);
    print_sample("slot1 rx",
                 bridge->slot1_rx_sample,
                 bridge->slot1_rx_bytes,
                 bridge->slot1_rx_nonzero_bytes);
    print_sample("slot2 tx",
                 bridge->slot2_tx_sample,
                 bridge->slot2_tx_bytes,
                 bridge->slot2_tx_nonzero_bytes);
    print_sample("slot2 rx",
                 bridge->slot2_rx_sample,
                 bridge->slot2_rx_bytes,
                 bridge->slot2_rx_nonzero_bytes);
    printf("  link infrared edges: slot1=%u slot2=%u\n",
           bridge->slot1_ir_edges,
           bridge->slot2_ir_edges);
    printf("  link infrared sample: slot1=%s slot2=%s\n",
           bridge->slot1_ir_sample,
           bridge->slot2_ir_sample);
    printf("  link infrared trace count: %u\n", bridge->ir_trace_count);
    unsigned ir_shown = bridge->ir_trace_count < 64 ? bridge->ir_trace_count : 64;
    printf("  link infrared trace first:");
    for (unsigned i = 0; i < ir_shown; i++) {
        printf(" #%u:f%u:s%u:v%u:c%u/%u",
               i,
               bridge->ir_trace_frame[i],
               bridge->ir_trace_slot[i],
               bridge->ir_trace_value[i],
               bridge->ir_trace_slot1_syncs[i],
               bridge->ir_trace_slot2_syncs[i]);
    }
    printf("\n");
    printf("  link infrared trace after1450:");
    unsigned after_count = 0;
    for (unsigned i = 0; i < bridge->ir_trace_count && after_count < 64; i++) {
        if (bridge->ir_trace_frame[i] >= 1450) {
            printf(" #%u:f%u:s%u:v%u:c%u/%u",
                   i,
                   bridge->ir_trace_frame[i],
                   bridge->ir_trace_slot[i],
                   bridge->ir_trace_value[i],
                   bridge->ir_trace_slot1_syncs[i],
                   bridge->ir_trace_slot2_syncs[i]);
            after_count++;
        }
    }
    printf("\n");
    printf("  link slot1 bit trace count: %u\n", bridge->slot1_trace_count);
    unsigned assists_zero = 0;
    unsigned assists_64 = 0;
    unsigned assists_other = 0;
    unsigned ir_delta_nonzero = 0;
    for (unsigned i = 0; i < bridge->slot1_trace_count; i++) {
        if (bridge->slot1_trace_assists[i] == 0) {
            assists_zero++;
        }
        else if (bridge->slot1_trace_assists[i] == 64) {
            assists_64++;
        }
        else {
            assists_other++;
        }
        if (bridge->slot1_trace_ir1_delta[i] != 0 || bridge->slot1_trace_ir2_delta[i] != 0) {
            ir_delta_nonzero++;
        }
    }
    printf("  link slot1 bit trace assists: zero=%u sixty-four=%u other=%u ir-nonzero=%u\n",
           assists_zero,
           assists_64,
           assists_other,
           ir_delta_nonzero);
    unsigned shown = bridge->slot1_trace_count < 64 ? bridge->slot1_trace_count : 64;
    printf("  link slot1 bit trace first:");
    for (unsigned i = 0; i < shown; i++) {
        printf(" #%u:a%u:i%u:o%u:b%u>A%u:ir%u/%u",
               i,
               bridge->slot1_trace_assists[i],
               bridge->slot1_trace_incoming[i],
               bridge->slot1_trace_outgoing[i],
               bridge->slot1_trace_peer_bit_before[i],
               bridge->slot1_trace_peer_bit_after[i],
               bridge->slot1_trace_ir1_delta[i],
               bridge->slot1_trace_ir2_delta[i]);
    }
    printf("\n");
    printf("  link slot1 bit trace ir-events:");
    for (unsigned i = 0; i < bridge->slot1_trace_count; i++) {
        if (bridge->slot1_trace_ir1_delta[i] != 0 || bridge->slot1_trace_ir2_delta[i] != 0) {
            printf(" #%u:a%u:ir%u/%u",
                   i,
                   bridge->slot1_trace_assists[i],
                   bridge->slot1_trace_ir1_delta[i],
                   bridge->slot1_trace_ir2_delta[i]);
        }
    }
    printf("\n");
}

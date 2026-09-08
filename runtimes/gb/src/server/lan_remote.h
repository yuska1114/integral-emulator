/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SERVER_LAN_REMOTE_H
#define INTEGRAL_GB_RUNTIME_SERVER_LAN_REMOTE_H

#include <stdbool.h>
#include <stdint.h>

#include "net_compat.h"
#include "slot.h"

typedef struct IntegralGBRuntimeLanRemote {
    integral_gb_runtime_socket_t listener;
    integral_gb_runtime_socket_t websocket;
    char host[64];
    uint8_t frame_rgb565[INTEGRAL_GB_RUNTIME_VIDEO_PAYLOAD_SIZE];
    uint8_t websocket_out[4u + 1u + INTEGRAL_GB_RUNTIME_VIDEO_PAYLOAD_SIZE];
    uint64_t last_frame_us;
    unsigned frame_seq;
    unsigned websocket_sent_frame_seq;
    unsigned port;
    size_t websocket_out_size;
    size_t websocket_out_sent;
    uint8_t buttons;
    bool frame_ready;
    bool fast_enabled;
    bool paused;
    bool open;
} IntegralGBRuntimeLanRemote;

int integral_gb_runtime_lan_remote_open(IntegralGBRuntimeLanRemote *remote, unsigned port);
void integral_gb_runtime_lan_remote_update_frame(IntegralGBRuntimeLanRemote *remote, const IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_lan_remote_queue_audio(IntegralGBRuntimeLanRemote *remote, const int16_t *samples, unsigned frames);
void integral_gb_runtime_lan_remote_poll(IntegralGBRuntimeLanRemote *remote, const IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_lan_remote_apply_input(const IntegralGBRuntimeLanRemote *remote, IntegralGBRuntimeSlot *slot);
bool integral_gb_runtime_lan_remote_fast_enabled(const IntegralGBRuntimeLanRemote *remote);
bool integral_gb_runtime_lan_remote_paused(const IntegralGBRuntimeLanRemote *remote);
const char *integral_gb_runtime_lan_remote_host(const IntegralGBRuntimeLanRemote *remote);
unsigned integral_gb_runtime_lan_remote_port(const IntegralGBRuntimeLanRemote *remote);
void integral_gb_runtime_lan_remote_close(IntegralGBRuntimeLanRemote *remote);

#endif

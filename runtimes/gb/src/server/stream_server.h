/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_STREAM_SERVER_H
#define INTEGRAL_GB_RUNTIME_STREAM_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "net_compat.h"
#include "protocol.h"
#include "slot.h"

typedef struct IntegralGBRuntimeStreamServer {
    integral_gb_runtime_socket_t listen_fd;
    integral_gb_runtime_socket_t client_fd;
    uint8_t remote_buttons;
    uint8_t remote_buttons_applied;
    uint32_t remote_input_seq;
    uint64_t remote_input_client_timestamp_us;
    uint32_t last_applied_input_seq;
    uint64_t last_applied_client_timestamp_us;
    uint64_t client_connected_at_us;
    char auth_token[INTEGRAL_GB_RUNTIME_AUTH_TOKEN_SIZE + 1u];
    uint8_t auth_buffer[INTEGRAL_GB_RUNTIME_AUTH_PACKET_SIZE];
    unsigned auth_buffer_size;
    uint8_t packet_buffer[INTEGRAL_GB_RUNTIME_INPUT_PACKET_SIZE];
    unsigned packet_buffer_size;
    unsigned packets_received;
    uint8_t downlink_buffer[INTEGRAL_GB_RUNTIME_MAX_DOWNLINK_PACKET_SIZE];
    unsigned downlink_buffer_size;
    unsigned downlink_buffer_sent;
    unsigned frame_seq;
    unsigned audio_seq;
    unsigned frames_queued;
    unsigned frames_sent;
    unsigned audio_packets_queued;
    unsigned audio_packets_sent;
    unsigned slot2_uploads_received;
    unsigned save_returns_sent;
    bool active;
    bool auth_required;
    bool authenticated;
    bool remote_input_applied;
    bool remote_release_pending;
} IntegralGBRuntimeStreamServer;

typedef bool (*IntegralGBRuntimeStreamWaitCallback)(void *user);

int integral_gb_runtime_stream_server_open(IntegralGBRuntimeStreamServer *server, const char *bind_host, unsigned port);
void integral_gb_runtime_stream_server_set_auth_token(IntegralGBRuntimeStreamServer *server, const char *auth_token);
void integral_gb_runtime_stream_server_poll(IntegralGBRuntimeStreamServer *server);
int integral_gb_runtime_stream_server_wait_for_slot2_upload(IntegralGBRuntimeStreamServer *server,
                                                  char *rom_path,
                                                  size_t rom_path_size,
                                                  char *save_path,
                                                  size_t save_path_size,
                                                  uint32_t *client_unix_time,
                                                  IntegralGBRuntimeStreamWaitCallback wait_callback,
                                                  void *wait_user);
void integral_gb_runtime_stream_server_apply_input(IntegralGBRuntimeStreamServer *server, IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_stream_server_queue_frame(IntegralGBRuntimeStreamServer *server, const IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_stream_server_queue_audio(IntegralGBRuntimeStreamServer *server, const int16_t *samples, unsigned frames);
int integral_gb_runtime_stream_server_send_save_file(IntegralGBRuntimeStreamServer *server, const char *save_path);
void integral_gb_runtime_stream_server_close(IntegralGBRuntimeStreamServer *server);
void integral_gb_runtime_stream_server_print_stats(const IntegralGBRuntimeStreamServer *server);

#endif

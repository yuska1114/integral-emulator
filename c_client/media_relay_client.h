/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_MEDIA_RELAY_CLIENT_H
#define INTEGRAL_MEDIA_RELAY_CLIENT_H

#include <stddef.h>
#include <stdint.h>

typedef struct IntegralMediaRelayConnection IntegralMediaRelayConnection;
int integral_media_relay_connect_timeout(const char *host, unsigned port,
    const char *transport, const char *session_id, const char *role, const char *scope,
    const char *ticket, const char *ca_file, IntegralMediaRelayConnection **connection_out,
    char *error_out, size_t error_out_size, unsigned timeout_seconds);

#define INTEGRAL_MEDIA_MESSAGE_CONTROLLER_INPUT 1u
#define INTEGRAL_MEDIA_MESSAGE_H264_CONFIG 2u
#define INTEGRAL_MEDIA_MESSAGE_H264_FRAME 3u
#define INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM 4u
#define INTEGRAL_MEDIA_MESSAGE_GB_INPUT 5u
#define INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK 6u
#define INTEGRAL_MEDIA_MESSAGE_GB_PING 7u
#define INTEGRAL_MEDIA_MESSAGE_GB_PONG 8u
#define INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE 9u
#define INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL 12u
#define INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL_ACK 13u
#define INTEGRAL_MEDIA_GB_INPUT_BYTES 16u
#define INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES 24u
#define INTEGRAL_MEDIA_GB_PING_BYTES 8u
#define INTEGRAL_MEDIA_GB_SESSION_STATE_BYTES 8u
#define INTEGRAL_MEDIA_GB_TERMINAL_BYTES 40u
#define INTEGRAL_MEDIA_FLAG_H264_KEYFRAME 1u
#define INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES (64u * 1024u)
#define INTEGRAL_MEDIA_MAX_H264_FRAME_BYTES (2u * 1024u * 1024u)
#define INTEGRAL_MEDIA_MAX_AUDIO_BYTES 8192u

int integral_media_relay_connect(const char *host,
                            unsigned port,
                            const char *transport,
                            const char *session_id,
                            const char *role,
                            const char *scope,
                            const char *ticket,
                            const char *ca_file,
                            IntegralMediaRelayConnection **connection_out,
                            char *error_out,
                            size_t error_out_size);

/* Returns 1 when paired, 0 while waiting, and -1 on connection failure. */
int integral_media_relay_poll(IntegralMediaRelayConnection *connection,
                         char *error_out,
                         size_t error_out_size);

/* Controller frames are carried only inside the authenticated relay stream. */
int integral_media_relay_send_controller(IntegralMediaRelayConnection *connection,
                                    uint32_t sequence,
                                    uint64_t buttons,
                                    char *error_out,
                                    size_t error_out_size);

/* Returns 1 for a new state, 2 when a redundant-validation failure was dropped,
 * 0 when none is ready, and -1 on connection failure. */
int integral_media_relay_poll_controller(IntegralMediaRelayConnection *connection,
                                    uint32_t *sequence_out,
                                    uint64_t *buttons_out,
                                    char *error_out,
                                    size_t error_out_size);

/* Prototype control transport. Remote may send GB input/ping; host may send
 * input acknowledgement/pong. Payload integers use network byte order. */
int integral_media_relay_send_control(IntegralMediaRelayConnection *connection,
                                      uint8_t message_type,
                                      uint32_t sequence,
                                      const void *payload,
                                      uint32_t payload_size,
                                      char *error_out,
                                      size_t error_out_size);
int integral_media_relay_poll_control(IntegralMediaRelayConnection *connection,
                                      uint8_t *message_type_out,
                                      uint32_t *sequence_out,
                                      void *payload_out,
                                      size_t payload_capacity,
                                      uint32_t *payload_size_out,
                                      char *error_out,
                                      size_t error_out_size);

/* Host-only media send. Returns 1 when flushed, 0 when accepted into the queue, -1 on failure. */
int integral_media_relay_send_media(IntegralMediaRelayConnection *connection,
                               uint8_t message_type,
                               uint16_t flags,
                               uint32_t sequence,
                               const void *payload,
                               uint32_t payload_size,
                               char *error_out,
                               size_t error_out_size);

/* Remote-only media receive. Returns 1 for a frame, 0 when idle, -1 on failure. */
int integral_media_relay_poll_media(IntegralMediaRelayConnection *connection,
                               uint8_t *message_type_out,
                               uint16_t *flags_out,
                               uint32_t *sequence_out,
                               void *payload_out,
                               size_t payload_capacity,
                               uint32_t *payload_size_out,
                               char *error_out,
                               size_t error_out_size);

void integral_media_relay_close(IntegralMediaRelayConnection *connection);

#endif

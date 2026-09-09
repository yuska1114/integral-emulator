/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_relay_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static void wait_milliseconds(unsigned milliseconds) { Sleep(milliseconds); }
#else
#include <time.h>
static void wait_milliseconds(unsigned milliseconds)
{
    struct timespec delay = {
        .tv_sec = (time_t)(milliseconds / 1000u),
        .tv_nsec = (long)(milliseconds % 1000u) * 1000000L,
    };
    nanosleep(&delay, NULL);
}
#endif

int main(int argc, char **argv)
{
    if (argc != 9) {
        fprintf(stderr, "usage: %s HOST PORT TRANSPORT SESSION ROLE SCOPE TICKET CERT\n", argv[0]);
        return 2;
    }
    char error[160];
    IntegralMediaRelayConnection *connection = NULL;
    if (integral_media_relay_connect(argv[1],
                                (unsigned)strtoul(argv[2], NULL, 10),
                                argv[3],
                                argv[4],
                                argv[5],
                                argv[6],
                                argv[7],
                                strcmp(argv[3], "tls") == 0 ? argv[8] : NULL,
                                &connection,
                                error,
                                sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    int paired = 0;
    int input_sent = 0;
    int input_received = 0;
    unsigned media_sent = 0;
    unsigned media_received = 0;
    static const unsigned char config_payload[] = {
        0x02, 0x80, 0x01, 0xe0, 0x00, 0x04, 0x00, 0x03,
        's', 'p', 's', '1', 'p', 'p', 's'
    };
    static const unsigned char video_payload[] = {
        0, 0, 0, 0, 0, 0, 0, 1, 'f', 'r', 'a', 'm', 'e'
    };
    static const unsigned char audio_payload[] = {
        0x00, 0x00, 0xac, 0x44, 0x00, 0x02, 0x00, 0x04,
        0, 0, 0, 0, 0, 0, 0, 1,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    static unsigned char received_media_payload[INTEGRAL_MEDIA_MAX_H264_FRAME_BYTES];
    for (unsigned attempt = 0; attempt < 240; attempt++) {
        if (!paired) {
            int status = integral_media_relay_poll(connection, error, sizeof(error));
            if (status > 0) {
                paired = 1;
                printf("MEDIA %s PAIRED\n", argv[3]);
            }
            else if (status < 0) {
                fprintf(stderr, "%s\n", error);
                integral_media_relay_close(connection);
                return 1;
            }
        }
        else if (strcmp(argv[5], "remote") == 0) {
            if (!input_sent) {
                int sent = integral_media_relay_send_controller(connection, 1u, 0x153u, error, sizeof(error));
                if (sent < 0) {
                    fprintf(stderr, "%s\n", error);
                    integral_media_relay_close(connection);
                    return 1;
                }
                if (sent > 0) {
                    input_sent = 1;
                    puts("MEDIA INPUT SENT 0x0153");
                }
            }
            uint8_t type = 0;
            uint16_t flags = 0;
            uint32_t sequence = 0;
            uint32_t payload_size = 0;
            int received = integral_media_relay_poll_media(connection,
                                                       &type,
                                                       &flags,
                                                       &sequence,
                                                       received_media_payload,
                                                       sizeof(received_media_payload),
                                                       &payload_size,
                                                       error,
                                                       sizeof(error));
            if (received < 0) {
                fprintf(stderr, "%s\n", error);
                integral_media_relay_close(connection);
                return 1;
            }
            if (received > 0) {
                media_received++;
                printf("MEDIA FRAME RECEIVED type=%u sequence=%u bytes=%u\n",
                       (unsigned)type, (unsigned)sequence, (unsigned)payload_size);
            }
            if (input_sent && media_received == 3) {
                integral_media_relay_close(connection);
                return 0;
            }
        }
        else {
            while (media_sent < 3) {
                const void *payload = media_sent == 0 ? (const void *)config_payload
                                      : media_sent == 1 ? (const void *)video_payload
                                                        : (const void *)audio_payload;
                uint32_t payload_size = media_sent == 0 ? (uint32_t)sizeof(config_payload)
                                              : media_sent == 1 ? (uint32_t)sizeof(video_payload)
                                                                : (uint32_t)sizeof(audio_payload);
                uint8_t type = media_sent == 0 ? INTEGRAL_MEDIA_MESSAGE_H264_CONFIG
                                 : media_sent == 1 ? INTEGRAL_MEDIA_MESSAGE_H264_FRAME
                                                   : INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM;
                uint16_t flags = type == INTEGRAL_MEDIA_MESSAGE_H264_FRAME
                                     ? INTEGRAL_MEDIA_FLAG_H264_KEYFRAME
                                     : 0;
                uint32_t sequence = type == INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM ? 1 : media_sent + 1;
                int sent = integral_media_relay_send_media(connection,
                                                      type,
                                                      flags,
                                                      sequence,
                                                      payload,
                                                      payload_size,
                                                      error,
                                                      sizeof(error));
                if (sent < 0) {
                    fprintf(stderr, "%s\n", error);
                    integral_media_relay_close(connection);
                    return 1;
                }
                media_sent++;
                if (sent == 0) break;
            }
            uint32_t sequence = 0;
            uint64_t buttons = 0;
            int received = integral_media_relay_poll_controller(connection,
                                                           &sequence,
                                                           &buttons,
                                                           error,
                                                           sizeof(error));
            if (received < 0) {
                fprintf(stderr, "%s\n", error);
                integral_media_relay_close(connection);
                return 1;
            }
            if (received == 2) {
                puts("MEDIA INPUT DROPPED");
                continue;
            }
            if (received > 0 && sequence == 1u && buttons == 0x153u) {
                puts("MEDIA INPUT RECEIVED 0x0153");
                input_received = 1;
            }
            if (input_received && media_sent == 3) {
                integral_media_relay_close(connection);
                return 0;
            }
        }
        wait_milliseconds(25);
    }
    fprintf(stderr, "MEDIA RELAY CONTROL TIMEOUT paired=%d sent=%d\n", paired, input_sent);
    integral_media_relay_close(connection);
    return 1;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_MEDIA_STREAM_H
#define INTEGRAL_N64_RUNTIME_MEDIA_STREAM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL.h>

#include "media_relay_client.h"

typedef struct IntegralN64RuntimeMediaStream IntegralN64RuntimeMediaStream;

typedef struct IntegralN64RuntimeMediaMetrics {
    uint32_t window_ms;
    uint32_t capture_frames;
    uint32_t capture_overwrites;
    uint32_t source_core_callbacks;
    uint32_t source_capture_due;
    uint32_t source_capture_success;
    uint32_t source_capture_failures;
    uint32_t source_callback_interval_avg_us;
    uint32_t source_callback_interval_max_us;
    uint32_t source_readback_avg_us;
    uint32_t source_readback_max_us;
    uint32_t source_ipc_write_avg_us;
    uint32_t source_ipc_write_max_us;
    uint32_t encoded_frames;
    uint32_t received_frames;
    uint32_t decoded_frames;
    uint32_t presented_frames;
    uint32_t capture_age_avg_us;
    uint32_t capture_age_max_us;
    uint32_t encode_avg_us;
    uint32_t encode_max_us;
    uint32_t receive_age_avg_us;
    uint32_t receive_age_max_us;
    uint32_t receive_age_invalid;
    uint32_t decode_avg_us;
    uint32_t decode_max_us;
    uint32_t decode_queue_peak;
    uint32_t display_overwrites;
    uint32_t present_p50_us;
    uint32_t present_p95_us;
    uint32_t present_max_us;
    uint32_t present_call_avg_us;
    uint32_t present_call_max_us;
    uint32_t video_refresh_hz;
    uint32_t relay_pending_ms;
    uint32_t relay_pending_max_ms;
    uint32_t audio_source_frames;
    uint32_t audio_sent_frames;
    uint32_t audio_received_frames;
    uint32_t audio_jitter_packets;
    uint32_t audio_jitter_peak_packets;
    uint32_t audio_conceals;
    uint32_t audio_queue_ms;
    uint32_t audio_queue_max_ms;
    uint32_t audio_queue_clears;
} IntegralN64RuntimeMediaMetrics;

IntegralN64RuntimeMediaStream *integral_n64_runtime_media_stream_create(SDL_Renderer *renderer);
void integral_n64_runtime_media_stream_set_window_title(IntegralN64RuntimeMediaStream *stream,
                                                 const char *title);
/* Sets the initial decoded-video window scale. The default is 1; callers may
 * override it before the first decoded frame creates the dedicated window. */
void integral_n64_runtime_media_stream_set_window_scale(IntegralN64RuntimeMediaStream *stream,
                                                 unsigned scale);
/* Test/diagnostic override applied before the first remote video frame.
 * Product behavior defaults to enabled. */
void integral_n64_runtime_media_stream_set_video_vsync_enabled(IntegralN64RuntimeMediaStream *stream,
                                                        bool enabled);
/* Diagnostic override applied before the dedicated Remote window is created.
 * NULL/empty keeps SDL's platform default renderer selection. */
void integral_n64_runtime_media_stream_set_video_renderer_driver(IntegralN64RuntimeMediaStream *stream,
                                                          const char *driver_name);
void integral_n64_runtime_media_stream_reset(IntegralN64RuntimeMediaStream *stream);

/* Resend cached codec configuration and recreate the Host encoder so a newly
 * resumed Remote starts from configuration plus a fresh keyframe. */
int integral_n64_runtime_media_stream_prepare_video_resume(
    IntegralN64RuntimeMediaStream *stream,
    IntegralMediaRelayConnection *connection,
    char *error_out,
    size_t error_out_size);
void integral_n64_runtime_media_stream_destroy(IntegralN64RuntimeMediaStream *stream);

/* Opens the host-side, read-only N64 Runtime RGB/PCM shared file. */
int integral_n64_runtime_media_stream_open_host(IntegralN64RuntimeMediaStream *stream,
                                         const char *ipc_path,
                                         char *error_out,
                                         size_t error_out_size);

/* Pulls new RGB/PCM snapshots, encodes them, and queues them to the TLS relay. */
int integral_n64_runtime_media_stream_pump_host(IntegralN64RuntimeMediaStream *stream,
                                         IntegralMediaRelayConnection *connection,
                                         uint64_t now_us,
                                         char *error_out,
                                         size_t error_out_size);

/* Direct host source used by the gb-runtime-fixed-host GB prototype. The stream owns no
 * ROM or SAV data; callers provide one captured RGB frame and PCM block. */
int integral_n64_runtime_media_stream_send_host_rgb24(IntegralN64RuntimeMediaStream *stream,
                                               IntegralMediaRelayConnection *connection,
                                               const uint8_t *rgb,
                                               uint16_t width,
                                               uint16_t height,
                                               bool bottom_up,
                                               uint64_t now_us,
                                               char *error_out,
                                               size_t error_out_size);

/* Fixed-Host GB path: copy the newest raw frame into a one-frame mailbox for
 * the encoder worker, then drain completed H.264 output from the existing
 * single TLS-writer thread.  Raw frames may be replaced before encoding, but
 * an encoded frame is retained until the Relay accepts it. */
int integral_n64_runtime_media_stream_submit_host_rgb24(IntegralN64RuntimeMediaStream *stream,
                                                 const uint8_t *rgb,
                                                 uint16_t width,
                                                 uint16_t height,
                                                 bool bottom_up,
                                                 uint64_t now_us,
                                                 char *error_out,
                                                 size_t error_out_size);
int integral_n64_runtime_media_stream_pump_host_video_output(
    IntegralN64RuntimeMediaStream *stream,
    IntegralMediaRelayConnection *connection,
    uint64_t now_us,
    char *error_out,
    size_t error_out_size);
int integral_n64_runtime_media_stream_send_host_pcm(IntegralN64RuntimeMediaStream *stream,
                                             IntegralMediaRelayConnection *connection,
                                             const int16_t *pcm_stereo,
                                             uint16_t frame_count,
                                             uint32_t sample_rate,
                                             uint64_t now_us,
                                             char *error_out,
                                             size_t error_out_size);

/* Pulls relay media, decodes it, updates the video texture, and queues audio. */
int integral_n64_runtime_media_stream_pump_remote(IntegralN64RuntimeMediaStream *stream,
                                           IntegralMediaRelayConnection *connection,
                                           uint64_t now_us,
                                           char *error_out,
                                           size_t error_out_size);

/* Returns one non-media prototype control message consumed by pump_remote. */
bool integral_n64_runtime_media_stream_take_remote_control(IntegralN64RuntimeMediaStream *stream,
                                                    uint8_t *message_type_out,
                                                    uint32_t *sequence_out,
                                                    void *payload_out,
                                                    size_t payload_capacity,
                                                    uint32_t *payload_size_out);

/* Returns true after the first decoded frame and presents its dedicated window. */
bool integral_n64_runtime_media_stream_render(IntegralN64RuntimeMediaStream *stream,
                                       SDL_Renderer *renderer,
                                       const SDL_Rect *bounds);
void integral_n64_runtime_media_stream_set_exit_confirmation(
    IntegralN64RuntimeMediaStream *stream, bool active, bool yes_selected);

/* True when the dedicated remote-video renderer is actually VSync-paced. */
bool integral_n64_runtime_media_stream_is_video_vsync_paced(const IntegralN64RuntimeMediaStream *stream);
const char *integral_n64_runtime_media_stream_video_renderer_driver(
    const IntegralN64RuntimeMediaStream *stream);

/* Identifies the dedicated remote-video window for close-event handling. */
bool integral_n64_runtime_media_stream_is_video_window(const IntegralN64RuntimeMediaStream *stream,
                                                uint32_t window_id);

/* Returns one five-second measurement window and resets its interval counters. */
bool integral_n64_runtime_media_stream_take_metrics(IntegralN64RuntimeMediaStream *stream,
                                             uint64_t now_us,
                                             IntegralN64RuntimeMediaMetrics *metrics);

#endif

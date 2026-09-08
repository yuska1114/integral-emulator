/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_REMOTE_MEDIA_IPC_H
#define INTEGRAL_N64_RUNTIME_REMOTE_MEDIA_IPC_H

#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_N64_RUNTIME_MEDIA_MAX_WIDTH 1920u
#define INTEGRAL_N64_RUNTIME_MEDIA_MAX_HEIGHT 1080u
#define INTEGRAL_N64_RUNTIME_MEDIA_MAX_VIDEO_BYTES \
    (INTEGRAL_N64_RUNTIME_MEDIA_MAX_WIDTH * INTEGRAL_N64_RUNTIME_MEDIA_MAX_HEIGHT * 3u)
#define INTEGRAL_N64_RUNTIME_MEDIA_MAX_AUDIO_BYTES 16384u
#define INTEGRAL_N64_RUNTIME_MEDIA_VIDEO_BOTTOM_UP 1u

typedef struct IntegralN64RuntimeRemoteMediaProducerMetrics {
    uint64_t core_callbacks;
    uint64_t core_callback_interval_samples;
    uint64_t core_callback_interval_total_us;
    uint32_t core_callback_interval_max_us;
    uint64_t capture_due;
    uint64_t capture_success;
    uint64_t capture_failures;
    uint64_t readback_samples;
    uint64_t readback_total_us;
    uint32_t readback_max_us;
    uint64_t ipc_write_samples;
    uint64_t ipc_write_total_us;
    uint32_t ipc_write_max_us;
} IntegralN64RuntimeRemoteMediaProducerMetrics;

#ifdef _WIN32
#define INTEGRAL_N64_RUNTIME_MEDIA_EXPORT __declspec(dllexport)
#else
#define INTEGRAL_N64_RUNTIME_MEDIA_EXPORT __attribute__((visibility("default")))
#endif

int integral_n64_runtime_remote_media_open_writer(const char *path);
int integral_n64_runtime_remote_media_open_reader(const char *path);
void integral_n64_runtime_remote_media_close(void);

int integral_n64_runtime_remote_media_write_video(const void *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t flags);
int integral_n64_runtime_remote_media_read_video(uint32_t *sequence,
                                      void *pixels,
                                      size_t capacity,
                                      uint32_t *width,
                                      uint32_t *height,
                                      uint32_t *flags,
                                      uint32_t *byte_count,
                                      uint64_t *capture_age_us);
void integral_n64_runtime_remote_media_write_producer_metrics(
    const IntegralN64RuntimeRemoteMediaProducerMetrics *metrics);
int integral_n64_runtime_remote_media_read_producer_metrics(
    IntegralN64RuntimeRemoteMediaProducerMetrics *metrics);

INTEGRAL_N64_RUNTIME_MEDIA_EXPORT void integral_n64_runtime_remote_media_audio_pcm(
    const void *samples,
    size_t byte_count,
    unsigned sample_rate,
    unsigned channels);
int integral_n64_runtime_remote_media_read_audio(uint32_t *sequence,
                                      void *samples,
                                      size_t capacity,
                                      uint32_t *sample_rate,
                                      uint32_t *channels,
                                      uint32_t *byte_count,
                                      uint64_t *capture_age_us);

#endif

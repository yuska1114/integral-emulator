/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remote_media_ipc.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int print_producer_metrics(const char *path)
{
    IntegralN64RuntimeRemoteMediaProducerMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    if (integral_n64_runtime_remote_media_open_reader(path) != 0) {
        fprintf(stderr, "remote media IPC producer inspect: mapping/version invalid\n");
        return 1;
    }
    int result = integral_n64_runtime_remote_media_read_producer_metrics(&metrics);
    integral_n64_runtime_remote_media_close();
    if (result != 0 || metrics.core_callbacks == 0 ||
        metrics.core_callback_interval_samples == 0) {
        fprintf(stderr,
                "remote media IPC producer inspect: metrics invalid result=%d callbacks=%llu samples=%llu\n",
                result,
                (unsigned long long)metrics.core_callbacks,
                (unsigned long long)metrics.core_callback_interval_samples);
        return 1;
    }
    const double callback_seconds =
        (double)metrics.core_callback_interval_total_us / 1000000.0;
    const double callback_fps = callback_seconds > 0.0
                                    ? (double)metrics.core_callback_interval_samples /
                                          callback_seconds
                                    : 0.0;
    const double readback_avg_ms = metrics.readback_samples
                                       ? (double)metrics.readback_total_us /
                                             (double)metrics.readback_samples / 1000.0
                                       : 0.0;
    const double ipc_write_avg_ms = metrics.ipc_write_samples
                                        ? (double)metrics.ipc_write_total_us /
                                              (double)metrics.ipc_write_samples / 1000.0
                                        : 0.0;
    printf("remote media producer: callbacks=%llu callback_fps=%.2f "
           "callback_interval_max_ms=%.3f capture_due=%llu captures=%llu failures=%llu "
           "readback_avg_ms=%.3f readback_max_ms=%.3f "
           "ipc_write_avg_ms=%.3f ipc_write_max_ms=%.3f\n",
           (unsigned long long)metrics.core_callbacks,
           callback_fps,
           (double)metrics.core_callback_interval_max_us / 1000.0,
           (unsigned long long)metrics.capture_due,
           (unsigned long long)metrics.capture_success,
           (unsigned long long)metrics.capture_failures,
           readback_avg_ms,
           (double)metrics.readback_max_us / 1000.0,
           ipc_write_avg_ms,
           (double)metrics.ipc_write_max_us / 1000.0);
    return 0;
}

static int inspect_runtime(const char *path)
{
    uint8_t *video = malloc(INTEGRAL_N64_RUNTIME_MEDIA_MAX_VIDEO_BYTES);
    uint8_t audio[INTEGRAL_N64_RUNTIME_MEDIA_MAX_AUDIO_BYTES];
    uint32_t video_sequence = 0;
    uint32_t audio_sequence = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t flags = 0;
    uint32_t bytes = 0;
    uint32_t rate = 0;
    uint32_t channels = 0;
    uint64_t capture_age_us = 0;
    IntegralN64RuntimeRemoteMediaProducerMetrics metrics;
    memset(&metrics, 0, sizeof(metrics));
    if (!video) {
        fprintf(stderr, "remote media IPC inspect: video allocation failed\n");
        return 1;
    }
    if (integral_n64_runtime_remote_media_open_reader(path) != 0) {
        fprintf(stderr, "remote media IPC inspect: mapping/version invalid\n");
        free(video);
        return 1;
    }
    int video_result = integral_n64_runtime_remote_media_read_video(
        &video_sequence, video, INTEGRAL_N64_RUNTIME_MEDIA_MAX_VIDEO_BYTES,
        &width, &height, &flags, &bytes, &capture_age_us);
    if (video_result != 1 || !video_sequence || width == 0 || height == 0 ||
        bytes != width * height * 3u) {
        fprintf(stderr,
                "remote media IPC inspect: video invalid result=%d seq=%u size=%ux%u bytes=%u\n",
                video_result, video_sequence, width, height, bytes);
        free(video);
        integral_n64_runtime_remote_media_close();
        return 1;
    }
    int audio_result = integral_n64_runtime_remote_media_read_audio(
        &audio_sequence, audio, sizeof(audio), &rate, &channels, &bytes,
        &capture_age_us);
    if (audio_result != 1 ||
        !audio_sequence || rate < 8000 || channels != 2 || bytes == 0) {
        fprintf(stderr,
                "remote media IPC inspect: audio invalid result=%d seq=%u rate=%u channels=%u bytes=%u\n",
                audio_result, audio_sequence, rate, channels, bytes);
        free(video);
        integral_n64_runtime_remote_media_close();
        return 1;
    }
    int metrics_result = integral_n64_runtime_remote_media_read_producer_metrics(&metrics);
    if (metrics_result != 0 ||
        metrics.core_callbacks == 0 || metrics.capture_success == 0 ||
        metrics.readback_samples == 0 || metrics.ipc_write_samples == 0) {
        fprintf(stderr,
                "remote media IPC inspect: metrics invalid result=%d callbacks=%llu captures=%llu "
                "readbacks=%llu writes=%llu\n",
                metrics_result,
                (unsigned long long)metrics.core_callbacks,
                (unsigned long long)metrics.capture_success,
                (unsigned long long)metrics.readback_samples,
                (unsigned long long)metrics.ipc_write_samples);
        free(video);
        integral_n64_runtime_remote_media_close();
        return 1;
    }
    const double callback_seconds =
        (double)metrics.core_callback_interval_total_us / 1000000.0;
    const double callback_fps = callback_seconds > 0.0
                                    ? (double)metrics.core_callback_interval_samples /
                                          callback_seconds
                                    : 0.0;
    const double readback_avg_ms = metrics.readback_samples
                                       ? (double)metrics.readback_total_us /
                                             (double)metrics.readback_samples / 1000.0
                                       : 0.0;
    const double ipc_write_avg_ms = metrics.ipc_write_samples
                                        ? (double)metrics.ipc_write_total_us /
                                              (double)metrics.ipc_write_samples / 1000.0
                                        : 0.0;
    printf("remote media IPC runtime: video=%ux%u seq=%u audio=%uHz seq=%u "
           "callbacks=%llu callback_fps=%.2f captures=%llu failures=%llu "
           "readback_avg_ms=%.3f readback_max_ms=%.3f "
           "ipc_write_avg_ms=%.3f ipc_write_max_ms=%.3f\n",
           width, height, video_sequence, rate, audio_sequence,
           (unsigned long long)metrics.core_callbacks,
           callback_fps,
           (unsigned long long)metrics.capture_success,
           (unsigned long long)metrics.capture_failures,
           readback_avg_ms,
           (double)metrics.readback_max_us / 1000.0,
           ipc_write_avg_ms,
           (double)metrics.ipc_write_max_us / 1000.0);
    free(video);
    integral_n64_runtime_remote_media_close();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 3 && strcmp(argv[1], "--inspect-producer") == 0)
        return print_producer_metrics(argv[2]);
    if (argc == 3 && strcmp(argv[1], "--inspect") == 0) return inspect_runtime(argv[2]);
    if (argc != 2) return 2;
    uint8_t video[4 * 3 * 3];
    int16_t audio[8];
    for (unsigned index = 0; index < sizeof(video); index++) video[index] = (uint8_t)index;
    for (unsigned index = 0; index < 8; index++) audio[index] = (int16_t)(index * 100 - 300);
    assert(integral_n64_runtime_remote_media_open_writer(argv[1]) == 0);
    assert(integral_n64_runtime_remote_media_write_video(video, 4, 3, INTEGRAL_N64_RUNTIME_MEDIA_VIDEO_BOTTOM_UP) == 0);
    IntegralN64RuntimeRemoteMediaProducerMetrics written_metrics = {
        .core_callbacks = 151,
        .core_callback_interval_samples = 150,
        .core_callback_interval_total_us = 5000000,
        .core_callback_interval_max_us = 42000,
        .capture_due = 120,
        .capture_success = 119,
        .capture_failures = 1,
        .readback_samples = 119,
        .readback_total_us = 357000,
        .readback_max_us = 9000,
        .ipc_write_samples = 119,
        .ipc_write_total_us = 59500,
        .ipc_write_max_us = 1700,
    };
    integral_n64_runtime_remote_media_write_producer_metrics(&written_metrics);
    integral_n64_runtime_remote_media_audio_pcm(audio, sizeof(audio), 44100, 2);
    integral_n64_runtime_remote_media_close();

    uint8_t read_video[sizeof(video)];
    int16_t read_audio[8];
    uint32_t video_sequence = 0;
    uint32_t audio_sequence = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t flags = 0;
    uint32_t bytes = 0;
    uint64_t capture_age_us = 0;
    assert(integral_n64_runtime_remote_media_open_reader(argv[1]) == 0);
    assert(integral_n64_runtime_remote_media_read_video(&video_sequence, read_video, sizeof(read_video),
                                              &width, &height, &flags, &bytes,
                                              &capture_age_us) == 1);
    assert(video_sequence == 1 && width == 4 && height == 3 &&
           flags == INTEGRAL_N64_RUNTIME_MEDIA_VIDEO_BOTTOM_UP && bytes == sizeof(video));
    assert(capture_age_us < 5000000u);
    assert(memcmp(video, read_video, sizeof(video)) == 0);
    assert(integral_n64_runtime_remote_media_read_video(&video_sequence, read_video, sizeof(read_video),
                                              &width, &height, &flags, &bytes,
                                              &capture_age_us) == 0);
    IntegralN64RuntimeRemoteMediaProducerMetrics read_metrics;
    memset(&read_metrics, 0, sizeof(read_metrics));
    assert(integral_n64_runtime_remote_media_read_producer_metrics(&read_metrics) == 0);
    assert(read_metrics.core_callbacks == written_metrics.core_callbacks);
    assert(read_metrics.core_callback_interval_samples ==
           written_metrics.core_callback_interval_samples);
    assert(read_metrics.core_callback_interval_total_us ==
           written_metrics.core_callback_interval_total_us);
    assert(read_metrics.core_callback_interval_max_us ==
           written_metrics.core_callback_interval_max_us);
    assert(read_metrics.capture_due == written_metrics.capture_due);
    assert(read_metrics.capture_success == written_metrics.capture_success);
    assert(read_metrics.capture_failures == written_metrics.capture_failures);
    assert(read_metrics.readback_samples == written_metrics.readback_samples);
    assert(read_metrics.readback_total_us == written_metrics.readback_total_us);
    assert(read_metrics.readback_max_us == written_metrics.readback_max_us);
    assert(read_metrics.ipc_write_samples == written_metrics.ipc_write_samples);
    assert(read_metrics.ipc_write_total_us == written_metrics.ipc_write_total_us);
    assert(read_metrics.ipc_write_max_us == written_metrics.ipc_write_max_us);
    uint32_t sample_rate = 0;
    uint32_t channels = 0;
    assert(integral_n64_runtime_remote_media_read_audio(&audio_sequence, read_audio, sizeof(read_audio),
                                              &sample_rate, &channels, &bytes,
                                              &capture_age_us) == 1);
    assert(audio_sequence == 1 && sample_rate == 44100 && channels == 2 && bytes == sizeof(audio));
    assert(capture_age_us < 5000000u);
    assert(memcmp(audio, read_audio, sizeof(audio)) == 0);
    assert(integral_n64_runtime_remote_media_read_audio(&audio_sequence, read_audio, sizeof(read_audio),
                                              &sample_rate, &channels, &bytes,
                                              &capture_age_us) == 0);
    integral_n64_runtime_remote_media_close();
    remove(argv[1]);
    puts("remote media IPC test: OK");
    return 0;
}

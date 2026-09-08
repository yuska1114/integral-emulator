/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "remote_media_ipc.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#define INTEGRAL_N64_RUNTIME_MEDIA_IPC_MAGIC "S64IPC1"
#define INTEGRAL_N64_RUNTIME_MEDIA_IPC_VERSION 3u

typedef struct IntegralN64RuntimeRemoteMediaShared {
    char magic[8];
    uint32_t version;
    _Atomic uint32_t video_guard;
    uint32_t video_sequence;
    uint32_t video_width;
    uint32_t video_height;
    uint32_t video_flags;
    uint32_t video_bytes;
    uint64_t video_written_us;
    _Atomic uint32_t producer_metrics_guard;
    IntegralN64RuntimeRemoteMediaProducerMetrics producer_metrics;
    _Atomic uint32_t audio_guard;
    uint32_t audio_sequence;
    uint32_t audio_sample_rate;
    uint32_t audio_channels;
    uint32_t audio_bytes;
    uint64_t audio_written_us;
    uint8_t video[INTEGRAL_N64_RUNTIME_MEDIA_MAX_VIDEO_BYTES];
    uint8_t audio[INTEGRAL_N64_RUNTIME_MEDIA_MAX_AUDIO_BYTES];
} IntegralN64RuntimeRemoteMediaShared;

static IntegralN64RuntimeRemoteMediaShared *g_shared;
#ifdef _WIN32
static HANDLE g_file = INVALID_HANDLE_VALUE;
static HANDLE g_mapping;
#else
static int g_file = -1;
#endif

static uint64_t monotonic_us(void)
{
#ifdef _WIN32
    return (uint64_t)GetTickCount64() * 1000u;
#else
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0;
    return (uint64_t)value.tv_sec * 1000000u + (uint64_t)value.tv_nsec / 1000u;
#endif
}

static int map_file(const char *path, int writer)
{
    if (!path || !path[0] || g_shared) return -1;
#ifdef _WIN32
    g_file = CreateFileA(path,
                         GENERIC_READ | GENERIC_WRITE,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         writer ? CREATE_ALWAYS : OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (g_file == INVALID_HANDLE_VALUE) return -1;
    if (writer) {
        LARGE_INTEGER size;
        size.QuadPart = (LONGLONG)sizeof(*g_shared);
        if (!SetFilePointerEx(g_file, size, NULL, FILE_BEGIN) || !SetEndOfFile(g_file)) {
            integral_n64_runtime_remote_media_close();
            return -1;
        }
    }
    g_mapping = CreateFileMappingA(g_file, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (!g_mapping) {
        integral_n64_runtime_remote_media_close();
        return -1;
    }
    g_shared = MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*g_shared));
#else
    g_file = open(path, writer ? O_RDWR | O_CREAT | O_TRUNC : O_RDWR, 0600);
    if (g_file < 0) return -1;
    if (writer && ftruncate(g_file, (off_t)sizeof(*g_shared)) != 0) {
        integral_n64_runtime_remote_media_close();
        return -1;
    }
    g_shared = mmap(NULL,
                    sizeof(*g_shared),
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    g_file,
                    0);
    if (g_shared == MAP_FAILED) g_shared = NULL;
#endif
    if (!g_shared) {
        integral_n64_runtime_remote_media_close();
        return -1;
    }
    if (writer) {
        memset(g_shared, 0, sizeof(*g_shared));
        memcpy(g_shared->magic, INTEGRAL_N64_RUNTIME_MEDIA_IPC_MAGIC, 8);
        g_shared->version = INTEGRAL_N64_RUNTIME_MEDIA_IPC_VERSION;
    }
    else if (memcmp(g_shared->magic, INTEGRAL_N64_RUNTIME_MEDIA_IPC_MAGIC, 8) != 0 ||
             g_shared->version != INTEGRAL_N64_RUNTIME_MEDIA_IPC_VERSION) {
        integral_n64_runtime_remote_media_close();
        return -1;
    }
    return 0;
}

int integral_n64_runtime_remote_media_open_writer(const char *path)
{
    return map_file(path, 1);
}

int integral_n64_runtime_remote_media_open_reader(const char *path)
{
    return map_file(path, 0);
}

void integral_n64_runtime_remote_media_close(void)
{
#ifdef _WIN32
    if (g_shared) UnmapViewOfFile(g_shared);
    if (g_mapping) CloseHandle(g_mapping);
    if (g_file != INVALID_HANDLE_VALUE) CloseHandle(g_file);
    g_mapping = NULL;
    g_file = INVALID_HANDLE_VALUE;
#else
    if (g_shared) munmap(g_shared, sizeof(*g_shared));
    if (g_file >= 0) close(g_file);
    g_file = -1;
#endif
    g_shared = NULL;
}

int integral_n64_runtime_remote_media_write_video(const void *pixels,
                                       uint32_t width,
                                       uint32_t height,
                                       uint32_t flags)
{
    if (!g_shared || !pixels || !width || !height ||
        width > INTEGRAL_N64_RUNTIME_MEDIA_MAX_WIDTH || height > INTEGRAL_N64_RUNTIME_MEDIA_MAX_HEIGHT ||
        (flags & ~INTEGRAL_N64_RUNTIME_MEDIA_VIDEO_BOTTOM_UP)) return -1;
    size_t bytes = (size_t)width * height * 3u;
    uint32_t guard = atomic_load_explicit(&g_shared->video_guard, memory_order_relaxed);
    atomic_store_explicit(&g_shared->video_guard, guard | 1u, memory_order_release);
    memcpy(g_shared->video, pixels, bytes);
    g_shared->video_width = width;
    g_shared->video_height = height;
    g_shared->video_flags = flags;
    g_shared->video_bytes = (uint32_t)bytes;
    g_shared->video_written_us = monotonic_us();
    g_shared->video_sequence++;
    atomic_store_explicit(&g_shared->video_guard, (guard | 1u) + 1u, memory_order_release);
    return 0;
}

int integral_n64_runtime_remote_media_read_video(uint32_t *sequence,
                                      void *pixels,
                                      size_t capacity,
                                      uint32_t *width,
                                      uint32_t *height,
                                      uint32_t *flags,
                                      uint32_t *byte_count,
                                      uint64_t *capture_age_us)
{
    if (!g_shared || !sequence || !pixels || !width || !height || !flags || !byte_count ||
        !capture_age_us) return -1;
    for (unsigned attempt = 0; attempt < 4; attempt++) {
        uint32_t before = atomic_load_explicit(&g_shared->video_guard, memory_order_acquire);
        if (before & 1u) continue;
        uint32_t current_sequence = g_shared->video_sequence;
        uint32_t bytes = g_shared->video_bytes;
        uint64_t written_us = g_shared->video_written_us;
        if (!bytes || bytes > capacity || bytes > INTEGRAL_N64_RUNTIME_MEDIA_MAX_VIDEO_BYTES) return 0;
        if (current_sequence == *sequence) {
            uint32_t after = atomic_load_explicit(&g_shared->video_guard, memory_order_acquire);
            if (before == after && !(after & 1u)) return 0;
            continue;
        }
        memcpy(pixels, g_shared->video, bytes);
        uint32_t after = atomic_load_explicit(&g_shared->video_guard, memory_order_acquire);
        if (before == after && !(after & 1u)) {
            *sequence = current_sequence;
            *width = g_shared->video_width;
            *height = g_shared->video_height;
            *flags = g_shared->video_flags;
            *byte_count = bytes;
            uint64_t now_us = monotonic_us();
            *capture_age_us = now_us >= written_us ? now_us - written_us : 0;
            return 1;
        }
    }
    return 0;
}

void integral_n64_runtime_remote_media_write_producer_metrics(
    const IntegralN64RuntimeRemoteMediaProducerMetrics *metrics)
{
    if (!g_shared || !metrics) return;
    uint32_t guard = atomic_load_explicit(&g_shared->producer_metrics_guard,
                                          memory_order_relaxed);
    atomic_store_explicit(&g_shared->producer_metrics_guard, guard | 1u,
                          memory_order_release);
    g_shared->producer_metrics = *metrics;
    atomic_store_explicit(&g_shared->producer_metrics_guard, (guard | 1u) + 1u,
                          memory_order_release);
}

int integral_n64_runtime_remote_media_read_producer_metrics(
    IntegralN64RuntimeRemoteMediaProducerMetrics *metrics)
{
    if (!g_shared || !metrics) return -1;
    for (unsigned attempt = 0; attempt < 4; attempt++) {
        uint32_t before = atomic_load_explicit(&g_shared->producer_metrics_guard,
                                               memory_order_acquire);
        if (before & 1u) continue;
        IntegralN64RuntimeRemoteMediaProducerMetrics snapshot = g_shared->producer_metrics;
        uint32_t after = atomic_load_explicit(&g_shared->producer_metrics_guard,
                                              memory_order_acquire);
        if (before == after && !(after & 1u)) {
            *metrics = snapshot;
            return 0;
        }
    }
    return 1;
}

void integral_n64_runtime_remote_media_audio_pcm(const void *samples,
                                      size_t byte_count,
                                      unsigned sample_rate,
                                      unsigned channels)
{
    if (!g_shared || !samples || !byte_count || byte_count > INTEGRAL_N64_RUNTIME_MEDIA_MAX_AUDIO_BYTES ||
        sample_rate < 8000u || sample_rate > 192000u || channels != 2u) return;
    uint32_t guard = atomic_load_explicit(&g_shared->audio_guard, memory_order_relaxed);
    atomic_store_explicit(&g_shared->audio_guard, guard | 1u, memory_order_release);
    memcpy(g_shared->audio, samples, byte_count);
    g_shared->audio_sample_rate = sample_rate;
    g_shared->audio_channels = channels;
    g_shared->audio_bytes = (uint32_t)byte_count;
    g_shared->audio_written_us = monotonic_us();
    g_shared->audio_sequence++;
    atomic_store_explicit(&g_shared->audio_guard, (guard | 1u) + 1u, memory_order_release);
}

int integral_n64_runtime_remote_media_read_audio(uint32_t *sequence,
                                      void *samples,
                                      size_t capacity,
                                      uint32_t *sample_rate,
                                      uint32_t *channels,
                                      uint32_t *byte_count,
                                      uint64_t *capture_age_us)
{
    if (!g_shared || !sequence || !samples || !sample_rate || !channels || !byte_count ||
        !capture_age_us) return -1;
    for (unsigned attempt = 0; attempt < 4; attempt++) {
        uint32_t before = atomic_load_explicit(&g_shared->audio_guard, memory_order_acquire);
        if (before & 1u) continue;
        uint32_t current_sequence = g_shared->audio_sequence;
        uint32_t bytes = g_shared->audio_bytes;
        uint64_t written_us = g_shared->audio_written_us;
        if (!bytes || bytes > capacity || bytes > INTEGRAL_N64_RUNTIME_MEDIA_MAX_AUDIO_BYTES) return 0;
        if (current_sequence == *sequence) {
            uint32_t after = atomic_load_explicit(&g_shared->audio_guard, memory_order_acquire);
            if (before == after && !(after & 1u)) return 0;
            continue;
        }
        memcpy(samples, g_shared->audio, bytes);
        uint32_t after = atomic_load_explicit(&g_shared->audio_guard, memory_order_acquire);
        if (before == after && !(after & 1u)) {
            *sequence = current_sequence;
            *sample_rate = g_shared->audio_sample_rate;
            *channels = g_shared->audio_channels;
            *byte_count = bytes;
            uint64_t now_us = monotonic_us();
            *capture_age_us = now_us >= written_us ? now_us - written_us : 0;
            return 1;
        }
    }
    return 0;
}

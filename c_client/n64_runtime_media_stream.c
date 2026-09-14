/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "n64_runtime_media_stream.h"

#include "media_codec.h"
#include "media_h264.h"
#include "sdl_text.h"
#include "../runtimes/gb/src/common/display_scale.h"
#include "../runtimes/n64/src/remote_media_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define VIDEO_CAPACITY INTEGRAL_MEDIA_MAX_H264_FRAME_BYTES
#define RGB_CAPACITY ((size_t)INTEGRAL_N64_RUNTIME_MEDIA_MAX_VIDEO_BYTES)
#define METRICS_WINDOW_US 5000000u
#define PRESENT_HISTOGRAM_BUCKET_US 5000u
#define PRESENT_HISTOGRAM_BUCKETS 41u
#define RECEIVE_AGE_MAX_US 60000000u
#define AUDIO_QUEUE_CAP_MS 100u
#define DECODE_QUEUE_CAPACITY 32u

typedef struct DecodePacket {
    uint8_t type;
    uint8_t *payload;
    uint32_t payload_size;
} DecodePacket;

typedef struct DecodeWorkerMetrics {
    uint32_t decoded_frames;
    uint64_t receive_age_total_us;
    uint32_t receive_age_max_us;
    uint32_t receive_age_samples;
    uint32_t receive_age_invalid;
    uint64_t decode_total_us;
    uint32_t decode_max_us;
    uint32_t decode_samples;
    uint32_t queue_peak;
    uint32_t display_overwrites;
} DecodeWorkerMetrics;

typedef struct HostEncodeWorkerMetrics {
    uint32_t encoded_frames;
    uint64_t encode_total_us;
    uint32_t encode_max_us;
    uint32_t encode_samples;
} HostEncodeWorkerMetrics;

typedef struct MetricsAccumulator {
    uint64_t window_started_us;
    uint32_t capture_frames;
    uint32_t capture_overwrites;
    uint32_t encoded_frames;
    uint32_t received_frames;
    uint32_t decoded_frames;
    uint32_t presented_frames;
    uint64_t capture_age_total_us;
    uint32_t capture_age_max_us;
    uint64_t encode_total_us;
    uint32_t encode_max_us;
    uint32_t encode_samples;
    uint64_t receive_age_total_us;
    uint32_t receive_age_max_us;
    uint32_t receive_age_samples;
    uint32_t receive_age_invalid;
    uint64_t decode_total_us;
    uint32_t decode_max_us;
    uint32_t decode_samples;
    uint64_t last_present_us;
    uint32_t present_interval_count;
    uint32_t present_interval_max_us;
    uint32_t present_histogram[PRESENT_HISTOGRAM_BUCKETS];
    uint64_t present_call_total_us;
    uint32_t present_call_max_us;
    uint32_t present_call_samples;
    uint64_t relay_pending_since_us;
    uint32_t relay_pending_max_ms;
    uint32_t audio_source_frames;
    uint32_t audio_sent_frames;
    uint32_t audio_received_frames;
    uint32_t audio_jitter_peak_packets;
    uint32_t audio_conceals;
    uint32_t audio_queue_max_ms;
    uint32_t audio_queue_clears;
} MetricsAccumulator;

struct IntegralN64RuntimeMediaStream {
    SDL_Window *parent_window;
    SDL_Renderer *renderer;
    char window_title[128];
    unsigned video_window_scale;
    unsigned video_window_scale_resolved;
    unsigned video_window_target_width;
    unsigned video_window_target_height;
    bool video_vsync_enabled;
    char video_renderer_driver_requested[32];
    bool video_renderer_driver_required;
    char video_renderer_driver[32];
    char ipc_path[1024];
    bool ipc_open;
    uint32_t ipc_video_sequence;
    uint32_t ipc_audio_sequence;
    IntegralN64RuntimeRemoteMediaProducerMetrics producer_metrics_baseline;
    uint32_t video_sequence;
    uint32_t audio_sequence;
    IntegralH264Encoder *encoder;
    IntegralH264Decoder *decoder;
    uint16_t encoder_width;
    uint16_t encoder_height;
    uint8_t h264_config[INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES];
    uint32_t h264_config_size;
    uint8_t *rgb;
    uint8_t *decode_rgb;
    uint8_t *encoded;
    uint8_t *received;
    uint8_t *host_encode_rgb;
    uint8_t *host_encoded;
    SDL_Thread *host_encode_thread;
    SDL_mutex *host_encode_mutex;
    SDL_cond *host_encode_condition;
    bool host_encode_stop;
    bool host_encode_failed;
    char host_encode_error[160];
    bool host_input_ready;
    uint16_t host_input_width;
    uint16_t host_input_height;
    bool host_input_bottom_up;
    uint64_t host_input_timestamp_us;
    bool host_output_ready;
    bool host_output_config_pending;
    uint8_t host_output_config[INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES];
    uint32_t host_output_config_size;
    uint32_t host_output_frame_size;
    bool host_output_keyframe;
    HostEncodeWorkerMetrics host_encode_metrics;
    SDL_Thread *decode_thread;
    SDL_mutex *decode_mutex;
    SDL_cond *decode_condition;
    bool decode_stop;
    bool decode_failed;
    char decode_error[160];
    DecodePacket decode_queue[DECODE_QUEUE_CAPACITY];
    unsigned decode_queue_head;
    unsigned decode_queue_count;
    uint64_t decoded_frame_sequence;
    uint64_t displayed_frame_sequence;
    uint16_t decoded_width;
    uint16_t decoded_height;
    DecodeWorkerMetrics decode_metrics;
    SDL_Window *video_window;
    SDL_Renderer *video_renderer;
    bool video_present_vsync;
    uint32_t video_refresh_hz;
    uint32_t video_window_id;
    bool exit_confirming;
    bool exit_confirm_yes;
    bool exit_discard_warning;
    SDL_Texture *texture;
    uint16_t texture_width;
    uint16_t texture_height;
    bool texture_dirty;
    SDL_AudioDeviceID audio_device;
    unsigned audio_rate;
    IntegralAudioJitter jitter;
    bool remote_control_ready;
    uint8_t remote_control_type;
    uint32_t remote_control_sequence;
    uint8_t remote_control_payload[INTEGRAL_MEDIA_GB_TERMINAL_BYTES];
    uint32_t remote_control_payload_size;
    MetricsAccumulator metrics;
};

static uint64_t performance_us(void)
{
    uint64_t frequency = SDL_GetPerformanceFrequency();
    if (!frequency) return (uint64_t)SDL_GetTicks64() * 1000u;
    return SDL_GetPerformanceCounter() * 1000000u / frequency;
}

static uint64_t wall_clock_us(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) return 0;
    return (uint64_t)value.tv_sec * 1000000u + (uint64_t)value.tv_nsec / 1000u;
}

static uint32_t clamp_u64_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static void record_send_result(IntegralN64RuntimeMediaStream *stream, int result, uint64_t now_us)
{
    if (result == 0) {
        if (!stream->metrics.relay_pending_since_us) {
            stream->metrics.relay_pending_since_us = now_us ? now_us : 1u;
        }
    }
    else if (result > 0) {
        stream->metrics.relay_pending_since_us = 0;
    }
}

static void sample_relay_pending(IntegralN64RuntimeMediaStream *stream, uint64_t now_us)
{
    if (!stream->metrics.relay_pending_since_us || now_us < stream->metrics.relay_pending_since_us) return;
    uint32_t pending_ms = clamp_u64_u32((now_us - stream->metrics.relay_pending_since_us) / 1000u);
    if (pending_ms > stream->metrics.relay_pending_max_ms) {
        stream->metrics.relay_pending_max_ms = pending_ms;
    }
}

static uint32_t audio_queue_ms(const IntegralN64RuntimeMediaStream *stream)
{
    if (!stream->audio_device || !stream->audio_rate) return 0;
    uint64_t bytes_per_second = (uint64_t)stream->audio_rate * 4u;
    return bytes_per_second
               ? clamp_u64_u32((uint64_t)SDL_GetQueuedAudioSize(stream->audio_device) * 1000u /
                               bytes_per_second)
               : 0;
}

static void sample_audio_state(IntegralN64RuntimeMediaStream *stream)
{
    if (stream->jitter.packet_count > stream->metrics.audio_jitter_peak_packets) {
        stream->metrics.audio_jitter_peak_packets = stream->jitter.packet_count;
    }
    uint32_t queued_ms = audio_queue_ms(stream);
    if (queued_ms > stream->metrics.audio_queue_max_ms) {
        stream->metrics.audio_queue_max_ms = queued_ms;
    }
}

static void set_error(char *out, size_t size, const char *message)
{
    if (out && size) snprintf(out, size, "%s", message ? message : "media stream error");
}

static void put_be64(uint8_t *data, uint64_t value);

static int host_encode_worker_main(void *opaque)
{
    IntegralN64RuntimeMediaStream *stream = opaque;
    for (;;) {
        uint16_t width = 0, height = 0;
        bool bottom_up = false;
        uint64_t timestamp_us = 0;
        SDL_LockMutex(stream->host_encode_mutex);
        while (!stream->host_encode_stop &&
               (!stream->host_input_ready || stream->host_output_ready)) {
            SDL_CondWait(stream->host_encode_condition, stream->host_encode_mutex);
        }
        if (stream->host_encode_stop) {
            SDL_UnlockMutex(stream->host_encode_mutex);
            break;
        }
        width = stream->host_input_width;
        height = stream->host_input_height;
        bottom_up = stream->host_input_bottom_up;
        timestamp_us = stream->host_input_timestamp_us;
        size_t rgb_size = (size_t)width * height * 3u;
        memcpy(stream->rgb, stream->host_encode_rgb, rgb_size);
        stream->host_input_ready = false;
        SDL_UnlockMutex(stream->host_encode_mutex);

        char error[160] = {0};
        if (!stream->encoder || stream->encoder_width != width ||
            stream->encoder_height != height) {
            integral_h264_encoder_destroy(stream->encoder);
            stream->encoder = integral_h264_encoder_create(width, height,
                                                            error, sizeof(error));
            if (stream->encoder) {
                stream->encoder_width = width;
                stream->encoder_height = height;
            }
        }
        uint8_t config[INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES];
        uint32_t config_size = 0, frame_size = 0;
        bool keyframe = false;
        uint64_t encode_started_us = performance_us();
        int encoded = stream->encoder
                          ? integral_h264_encoder_encode_rgb24(
                                stream->encoder, stream->rgb, bottom_up,
                                timestamp_us, config, sizeof(config), &config_size,
                                stream->encoded + 8u, VIDEO_CAPACITY - 8u,
                                &frame_size, &keyframe, error, sizeof(error))
                          : -1;
        uint64_t encode_elapsed_us = performance_us() - encode_started_us;

        SDL_LockMutex(stream->host_encode_mutex);
        stream->host_encode_metrics.encode_samples++;
        stream->host_encode_metrics.encode_total_us += encode_elapsed_us;
        if (encode_elapsed_us > stream->host_encode_metrics.encode_max_us) {
            stream->host_encode_metrics.encode_max_us = clamp_u64_u32(encode_elapsed_us);
        }
        if (encoded < 0) {
            stream->host_encode_failed = true;
            snprintf(stream->host_encode_error, sizeof(stream->host_encode_error), "%s",
                     error[0] ? error : "N64 Runtime H264 encode worker failed");
            SDL_CondBroadcast(stream->host_encode_condition);
            SDL_UnlockMutex(stream->host_encode_mutex);
            break;
        }
        if (encoded > 0) {
            put_be64(stream->encoded, wall_clock_us());
            memcpy(stream->host_encoded, stream->encoded, frame_size + 8u);
            if (config_size) {
                memcpy(stream->host_output_config, config, config_size);
            }
            stream->host_output_config_size = config_size;
            stream->host_output_config_pending = config_size != 0u;
            stream->host_output_frame_size = frame_size + 8u;
            stream->host_output_keyframe = keyframe;
            stream->host_output_ready = true;
            stream->host_encode_metrics.encoded_frames++;
        }
        SDL_CondBroadcast(stream->host_encode_condition);
        SDL_UnlockMutex(stream->host_encode_mutex);
    }
    return 0;
}

static int ensure_host_encode_worker(IntegralN64RuntimeMediaStream *stream,
                                     char *error_out,
                                     size_t error_out_size)
{
    if (stream->host_encode_thread) return 0;
    stream->host_encode_stop = false;
    stream->host_encode_failed = false;
    stream->host_encode_error[0] = '\0';
    stream->host_encode_thread = SDL_CreateThread(host_encode_worker_main,
                                                   "integral-h264-host", stream);
    if (!stream->host_encode_thread) {
        set_error(error_out, error_out_size, SDL_GetError());
        return -1;
    }
    return 0;
}

static void stop_host_encode_worker(IntegralN64RuntimeMediaStream *stream)
{
    if (!stream || !stream->host_encode_mutex || !stream->host_encode_condition) return;
    SDL_LockMutex(stream->host_encode_mutex);
    stream->host_encode_stop = true;
    SDL_CondBroadcast(stream->host_encode_condition);
    SDL_UnlockMutex(stream->host_encode_mutex);
    if (stream->host_encode_thread) {
        SDL_WaitThread(stream->host_encode_thread, NULL);
        stream->host_encode_thread = NULL;
    }
    SDL_LockMutex(stream->host_encode_mutex);
    stream->host_encode_stop = false;
    stream->host_input_ready = false;
    stream->host_output_ready = false;
    stream->host_output_config_pending = false;
    stream->host_output_config_size = 0;
    stream->host_output_frame_size = 0;
    SDL_UnlockMutex(stream->host_encode_mutex);
}

static void put_be16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8); data[1] = (uint8_t)value;
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24); data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8); data[3] = (uint8_t)value;
}

static void put_be64(uint8_t *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) { data[index] = (uint8_t)value; value >>= 8; }
}

static uint16_t get_be16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] << 8 | data[1]);
}

static uint32_t get_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24 | (uint32_t)data[1] << 16 |
           (uint32_t)data[2] << 8 | data[3];
}

static uint64_t get_be64(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; index++) value = value << 8 | data[index];
    return value;
}

static void clear_decode_queue_locked(IntegralN64RuntimeMediaStream *stream)
{
    while (stream->decode_queue_count) {
        DecodePacket *packet = &stream->decode_queue[stream->decode_queue_head];
        free(packet->payload);
        memset(packet, 0, sizeof(*packet));
        stream->decode_queue_head =
            (stream->decode_queue_head + 1u) % DECODE_QUEUE_CAPACITY;
        stream->decode_queue_count--;
    }
    stream->decode_queue_head = 0;
}

static void fail_decode_worker(IntegralN64RuntimeMediaStream *stream, const char *message)
{
    SDL_LockMutex(stream->decode_mutex);
    stream->decode_failed = true;
    snprintf(stream->decode_error, sizeof(stream->decode_error), "%s",
             message && message[0] ? message : "N64 Runtime H264 decode worker failed");
    clear_decode_queue_locked(stream);
    SDL_UnlockMutex(stream->decode_mutex);
}

static int decode_worker_main(void *opaque)
{
    IntegralN64RuntimeMediaStream *stream = opaque;
    for (;;) {
        DecodePacket packet;
        memset(&packet, 0, sizeof(packet));
        SDL_LockMutex(stream->decode_mutex);
        while (!stream->decode_stop && !stream->decode_queue_count) {
            SDL_CondWait(stream->decode_condition, stream->decode_mutex);
        }
        if (stream->decode_stop) {
            SDL_UnlockMutex(stream->decode_mutex);
            break;
        }
        packet = stream->decode_queue[stream->decode_queue_head];
        memset(&stream->decode_queue[stream->decode_queue_head], 0,
               sizeof(stream->decode_queue[stream->decode_queue_head]));
        stream->decode_queue_head =
            (stream->decode_queue_head + 1u) % DECODE_QUEUE_CAPACITY;
        stream->decode_queue_count--;
        SDL_UnlockMutex(stream->decode_mutex);

        char error[160] = {0};
        if (packet.type == INTEGRAL_MEDIA_MESSAGE_H264_CONFIG) {
            integral_h264_decoder_destroy(stream->decoder);
            stream->decoder = integral_h264_decoder_create(packet.payload,
                                                       packet.payload_size,
                                                       error,
                                                       sizeof(error));
            free(packet.payload);
            if (!stream->decoder) {
                fail_decode_worker(stream, error);
                break;
            }
            continue;
        }
        if (packet.type != INTEGRAL_MEDIA_MESSAGE_H264_FRAME || packet.payload_size <= 8u ||
            !stream->decoder) {
            free(packet.payload);
            /* A retained Host socket can contain pre-pause frames when a new
             * Remote attaches.  Ignore them until the resume config/keyframe
             * epoch arrives instead of poisoning the replacement decoder. */
            continue;
        }

        uint64_t sent_wall_us = get_be64(packet.payload);
        uint64_t received_wall_us = wall_clock_us();
        uint64_t receive_age_us = 0;
        bool receive_age_valid =
            sent_wall_us >= UINT64_C(1500000000000000) && received_wall_us >= sent_wall_us &&
            received_wall_us - sent_wall_us <= RECEIVE_AGE_MAX_US;
        if (receive_age_valid) receive_age_us = received_wall_us - sent_wall_us;
        uint16_t width = 0, height = 0;
        uint64_t decode_started_us = performance_us();
        int decoded = integral_h264_decoder_decode_avcc(stream->decoder,
                                                    packet.payload + 8u,
                                                    packet.payload_size - 8u,
                                                    sent_wall_us,
                                                    stream->decode_rgb,
                                                    RGB_CAPACITY,
                                                    &width,
                                                    &height,
                                                    error,
                                                    sizeof(error));
        uint64_t decode_elapsed_us = performance_us() - decode_started_us;
        free(packet.payload);

        SDL_LockMutex(stream->decode_mutex);
        stream->decode_metrics.decode_samples++;
        stream->decode_metrics.decode_total_us += decode_elapsed_us;
        if (decode_elapsed_us > stream->decode_metrics.decode_max_us) {
            stream->decode_metrics.decode_max_us = clamp_u64_u32(decode_elapsed_us);
        }
        if (receive_age_valid) {
            stream->decode_metrics.receive_age_samples++;
            stream->decode_metrics.receive_age_total_us += receive_age_us;
            if (receive_age_us > stream->decode_metrics.receive_age_max_us) {
                stream->decode_metrics.receive_age_max_us = clamp_u64_u32(receive_age_us);
            }
        }
        else {
            stream->decode_metrics.receive_age_invalid++;
        }
        if (decoded > 0) {
            size_t rgb_bytes = (size_t)width * height * 3u;
            if (!width || !height || rgb_bytes > RGB_CAPACITY) {
                stream->decode_failed = true;
                snprintf(stream->decode_error, sizeof(stream->decode_error),
                         "N64 Runtime decoded frame dimensions invalid");
                clear_decode_queue_locked(stream);
                SDL_UnlockMutex(stream->decode_mutex);
                break;
            }
            if (stream->decoded_frame_sequence != stream->displayed_frame_sequence) {
                stream->decode_metrics.display_overwrites++;
            }
            memcpy(stream->rgb, stream->decode_rgb, rgb_bytes);
            stream->decoded_width = width;
            stream->decoded_height = height;
            stream->decoded_frame_sequence++;
            stream->decode_metrics.decoded_frames++;
        }
        SDL_UnlockMutex(stream->decode_mutex);
        if (decoded < 0) {
            fail_decode_worker(stream, error);
            break;
        }
    }
    integral_h264_decoder_destroy(stream->decoder);
    stream->decoder = NULL;
    return 0;
}

static int ensure_decode_worker(IntegralN64RuntimeMediaStream *stream,
                                char *error_out,
                                size_t error_out_size)
{
    if (stream->decode_thread) return 0;
    stream->decode_stop = false;
    stream->decode_failed = false;
    stream->decode_error[0] = '\0';
    stream->decode_thread = SDL_CreateThread(decode_worker_main,
                                             "gsc-h264-decode",
                                             stream);
    if (!stream->decode_thread) {
        set_error(error_out, error_out_size, SDL_GetError());
        return -1;
    }
    return 0;
}

static int enqueue_decode_packet(IntegralN64RuntimeMediaStream *stream,
                                 uint8_t type,
                                 const uint8_t *payload,
                                 uint32_t payload_size,
                                 char *error_out,
                                 size_t error_out_size)
{
    if (ensure_decode_worker(stream, error_out, error_out_size) != 0) return -1;
    uint8_t *copy = malloc(payload_size ? payload_size : 1u);
    if (!copy) {
        set_error(error_out, error_out_size, "N64 Runtime decode queue allocation failed");
        return -1;
    }
    if (payload_size) memcpy(copy, payload, payload_size);
    SDL_LockMutex(stream->decode_mutex);
    if (stream->decode_failed) {
        set_error(error_out, error_out_size, stream->decode_error);
        SDL_UnlockMutex(stream->decode_mutex);
        free(copy);
        return -1;
    }
    if (stream->decode_queue_count >= DECODE_QUEUE_CAPACITY) {
        SDL_UnlockMutex(stream->decode_mutex);
        free(copy);
        set_error(error_out, error_out_size, "N64 Runtime H264 decode queue full");
        return -1;
    }
    unsigned tail = (stream->decode_queue_head + stream->decode_queue_count) %
                    DECODE_QUEUE_CAPACITY;
    stream->decode_queue[tail].type = type;
    stream->decode_queue[tail].payload = copy;
    stream->decode_queue[tail].payload_size = payload_size;
    stream->decode_queue_count++;
    if (stream->decode_queue_count > stream->decode_metrics.queue_peak) {
        stream->decode_metrics.queue_peak = stream->decode_queue_count;
    }
    SDL_CondSignal(stream->decode_condition);
    SDL_UnlockMutex(stream->decode_mutex);
    return 0;
}

static void stop_decode_worker(IntegralN64RuntimeMediaStream *stream)
{
    if (!stream || !stream->decode_mutex) return;
    SDL_LockMutex(stream->decode_mutex);
    stream->decode_stop = true;
    if (stream->decode_condition) SDL_CondBroadcast(stream->decode_condition);
    SDL_UnlockMutex(stream->decode_mutex);
    if (stream->decode_thread) SDL_WaitThread(stream->decode_thread, NULL);
    stream->decode_thread = NULL;
    SDL_LockMutex(stream->decode_mutex);
    clear_decode_queue_locked(stream);
    stream->decode_stop = false;
    stream->decode_failed = false;
    stream->decode_error[0] = '\0';
    stream->decoded_frame_sequence = 0;
    stream->displayed_frame_sequence = 0;
    stream->decoded_width = stream->decoded_height = 0;
    memset(&stream->decode_metrics, 0, sizeof(stream->decode_metrics));
    SDL_UnlockMutex(stream->decode_mutex);
}

IntegralN64RuntimeMediaStream *integral_n64_runtime_media_stream_create(
    SDL_Window *parent_window,
    SDL_Renderer *renderer)
{
    IntegralN64RuntimeMediaStream *stream = calloc(1, sizeof(*stream));
    if (!stream) return NULL;
    stream->parent_window = parent_window;
    stream->renderer = renderer;
    stream->rgb = malloc(RGB_CAPACITY);
    stream->decode_rgb = malloc(RGB_CAPACITY);
    stream->encoded = malloc(VIDEO_CAPACITY);
    stream->received = malloc(VIDEO_CAPACITY);
    stream->host_encode_rgb = malloc(RGB_CAPACITY);
    stream->host_encoded = malloc(VIDEO_CAPACITY);
    stream->host_encode_mutex = SDL_CreateMutex();
    stream->host_encode_condition = SDL_CreateCond();
    stream->decode_mutex = SDL_CreateMutex();
    stream->decode_condition = SDL_CreateCond();
    if (!stream->rgb || !stream->decode_rgb || !stream->encoded || !stream->received ||
        !stream->host_encode_rgb || !stream->host_encoded ||
        !stream->host_encode_mutex || !stream->host_encode_condition ||
        !stream->decode_mutex || !stream->decode_condition) {
        integral_n64_runtime_media_stream_destroy(stream);
        return NULL;
    }
    integral_audio_jitter_init(&stream->jitter, 3, 60000);
    stream->video_window_scale = INTEGRAL_DISPLAY_SCALE_AUTO;
    stream->video_vsync_enabled = true;
#ifdef _WIN32
    /* SDL's legacy `direct3d` backend blocks dirty-only VSync Present for
     * about two refresh periods on the validated Windows desktop. Prefer the
     * hardware OpenGL renderer, but retain SDL auto selection on machines
     * where it is unavailable. */
    snprintf(stream->video_renderer_driver_requested,
             sizeof(stream->video_renderer_driver_requested), "%s", "opengl");
#endif
    snprintf(stream->window_title, sizeof(stream->window_title),
             "%s", "INTEGRAL EMULATOR - N64 Runtime");
    return stream;
}

void integral_n64_runtime_media_stream_set_window_title(IntegralN64RuntimeMediaStream *stream,
                                                 const char *title)
{
    if (!stream || stream->video_window || !title || !title[0]) return;
    snprintf(stream->window_title, sizeof(stream->window_title), "%s", title);
}

void integral_n64_runtime_media_stream_set_window_scale(IntegralN64RuntimeMediaStream *stream,
                                                 unsigned scale)
{
    if (!stream || stream->video_window || scale > INTEGRAL_DISPLAY_SCALE_MAX) return;
    stream->video_window_scale = scale;
}

void integral_n64_runtime_media_stream_set_window_target_size(
    IntegralN64RuntimeMediaStream *stream,
    unsigned width,
    unsigned height)
{
    if (!stream || stream->video_window || width < 160u || height < 144u) return;
    stream->video_window_target_width = width;
    stream->video_window_target_height = height;
}

void integral_n64_runtime_media_stream_set_video_vsync_enabled(IntegralN64RuntimeMediaStream *stream,
                                                        bool enabled)
{
    if (!stream || stream->video_window) return;
    stream->video_vsync_enabled = enabled;
}

void integral_n64_runtime_media_stream_set_video_renderer_driver(IntegralN64RuntimeMediaStream *stream,
                                                          const char *driver_name)
{
    if (!stream || stream->video_window) return;
    snprintf(stream->video_renderer_driver_requested,
             sizeof(stream->video_renderer_driver_requested), "%s",
             driver_name ? driver_name : "");
    stream->video_renderer_driver_required = driver_name && driver_name[0];
}

void integral_n64_runtime_media_stream_reset(IntegralN64RuntimeMediaStream *stream)
{
    if (!stream) return;
    stop_host_encode_worker(stream);
    stop_decode_worker(stream);
    if (stream->ipc_open) integral_n64_runtime_remote_media_close();
    stream->ipc_open = false;
    stream->ipc_path[0] = '\0';
    stream->ipc_video_sequence = 0;
    stream->ipc_audio_sequence = 0;
    memset(&stream->producer_metrics_baseline, 0,
           sizeof(stream->producer_metrics_baseline));
    stream->video_sequence = 0;
    stream->audio_sequence = 0;
    integral_h264_encoder_destroy(stream->encoder);
    integral_h264_decoder_destroy(stream->decoder);
    stream->encoder = NULL;
    stream->decoder = NULL;
    stream->encoder_width = stream->encoder_height = 0;
    stream->h264_config_size = 0;
    memset(stream->h264_config, 0, sizeof(stream->h264_config));
    if (stream->host_encode_mutex) {
        SDL_LockMutex(stream->host_encode_mutex);
        stream->host_encode_failed = false;
        stream->host_encode_error[0] = '\0';
        memset(&stream->host_encode_metrics, 0, sizeof(stream->host_encode_metrics));
        SDL_UnlockMutex(stream->host_encode_mutex);
    }
    if (stream->texture) SDL_DestroyTexture(stream->texture);
    stream->texture = NULL;
    stream->texture_width = stream->texture_height = 0;
    stream->texture_dirty = false;
    if (stream->video_renderer) SDL_DestroyRenderer(stream->video_renderer);
    stream->video_renderer = NULL;
    stream->video_present_vsync = false;
    if (stream->video_window) SDL_DestroyWindow(stream->video_window);
    stream->video_window = NULL;
    stream->video_window_id = 0;
    stream->video_window_scale_resolved = 0u;
    if (stream->audio_device) SDL_CloseAudioDevice(stream->audio_device);
    stream->audio_device = 0;
    stream->audio_rate = 0;
    stream->remote_control_ready = false;
    stream->remote_control_type = 0;
    stream->remote_control_sequence = 0;
    stream->remote_control_payload_size = 0;
    memset(stream->remote_control_payload, 0, sizeof(stream->remote_control_payload));
    integral_audio_jitter_init(&stream->jitter, 3, 60000);
    memset(&stream->metrics, 0, sizeof(stream->metrics));
}

int integral_n64_runtime_media_stream_prepare_video_resume(
    IntegralN64RuntimeMediaStream *stream,
    IntegralMediaRelayConnection *connection,
    char *error_out,
    size_t error_out_size)
{
    if (!stream || !connection || !stream->h264_config_size) {
        set_error(error_out, error_out_size,
                  "N64 Runtime H264 resume configuration unavailable");
        return -1;
    }
    stop_host_encode_worker(stream);
    int sent = integral_media_relay_send_media(
        connection, INTEGRAL_MEDIA_MESSAGE_H264_CONFIG, 0,
        ++stream->video_sequence, stream->h264_config,
        stream->h264_config_size, error_out, error_out_size);
    if (sent < 0) return -1;
    integral_h264_encoder_destroy(stream->encoder);
    stream->encoder = NULL;
    stream->encoder_width = 0;
    stream->encoder_height = 0;
    return 1;
}

void integral_n64_runtime_media_stream_destroy(IntegralN64RuntimeMediaStream *stream)
{
    if (!stream) return;
    integral_n64_runtime_media_stream_reset(stream);
    if (stream->host_encode_condition) SDL_DestroyCond(stream->host_encode_condition);
    if (stream->host_encode_mutex) SDL_DestroyMutex(stream->host_encode_mutex);
    if (stream->decode_condition) SDL_DestroyCond(stream->decode_condition);
    if (stream->decode_mutex) SDL_DestroyMutex(stream->decode_mutex);
    free(stream->rgb); free(stream->decode_rgb); free(stream->encoded); free(stream->received);
    free(stream->host_encode_rgb); free(stream->host_encoded);
    free(stream);
}

int integral_n64_runtime_media_stream_open_host(IntegralN64RuntimeMediaStream *stream,
                                         const char *ipc_path,
                                         char *error_out,
                                         size_t error_out_size)
{
    if (!stream || !ipc_path || !ipc_path[0]) return -1;
    if (stream->ipc_open && strcmp(stream->ipc_path, ipc_path) == 0) return 0;
    if (stream->ipc_open) integral_n64_runtime_remote_media_close();
    stream->ipc_open = false;
    if (integral_n64_runtime_remote_media_open_reader(ipc_path) != 0) {
        set_error(error_out, error_out_size, "N64 Runtime media IPC is not ready");
        return 1;
    }
    snprintf(stream->ipc_path, sizeof(stream->ipc_path), "%s", ipc_path);
    stream->ipc_open = true;
    return 0;
}

static int send_host_video(IntegralN64RuntimeMediaStream *stream,
                           IntegralMediaRelayConnection *connection,
                           uint64_t now_us,
                           char *error_out,
                           size_t error_out_size)
{
    uint32_t width = 0, height = 0, flags = 0, bytes = 0;
    uint64_t capture_age_us = 0;
    int read = integral_n64_runtime_remote_media_read_video(&stream->ipc_video_sequence,
                                                  stream->rgb,
                                                  RGB_CAPACITY,
                                                  &width,
                                                  &height,
                                                  &flags,
                                                  &bytes,
                                                  &capture_age_us);
    if (read <= 0) return read;
    stream->metrics.capture_age_total_us += capture_age_us;
    if (capture_age_us > stream->metrics.capture_age_max_us) {
        stream->metrics.capture_age_max_us = clamp_u64_u32(capture_age_us);
    }
    if (!width || !height || width > UINT16_MAX || height > UINT16_MAX ||
        bytes != width * height * 3u) {
        set_error(error_out, error_out_size, "N64 Runtime RGB frame invalid"); return -1;
    }
    return integral_n64_runtime_media_stream_send_host_rgb24(stream,
                                                      connection,
                                                      stream->rgb,
                                                      (uint16_t)width,
                                                      (uint16_t)height,
                                                      (flags & INTEGRAL_N64_RUNTIME_MEDIA_VIDEO_BOTTOM_UP) != 0,
                                                      now_us,
                                                      error_out,
                                                      error_out_size);
}

int integral_n64_runtime_media_stream_send_host_rgb24(IntegralN64RuntimeMediaStream *stream,
                                               IntegralMediaRelayConnection *connection,
                                               const uint8_t *rgb,
                                               uint16_t width,
                                               uint16_t height,
                                               bool bottom_up,
                                               uint64_t now_us,
                                               char *error_out,
                                               size_t error_out_size)
{
    size_t bytes = (size_t)width * height * 3u;
    if (!stream || !connection || !rgb || !width || !height || bytes > RGB_CAPACITY) {
        set_error(error_out, error_out_size, "GB prototype RGB frame invalid");
        return -1;
    }
    if (stream->host_encode_thread) {
        set_error(error_out, error_out_size, "GB prototype RGB mode conflict");
        return -1;
    }
    if (rgb != stream->rgb) memcpy(stream->rgb, rgb, bytes);
    stream->metrics.capture_frames++;
    if (!stream->encoder || stream->encoder_width != width || stream->encoder_height != height) {
        integral_h264_encoder_destroy(stream->encoder);
        stream->encoder = integral_h264_encoder_create((uint16_t)width, (uint16_t)height,
                                                  error_out, error_out_size);
        if (!stream->encoder) return -1;
        stream->encoder_width = (uint16_t)width;
        stream->encoder_height = (uint16_t)height;
    }
    uint8_t config[INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES];
    uint32_t config_size = 0, frame_size = 0;
    bool keyframe = false;
    uint64_t encode_started_us = performance_us();
    int encoded = integral_h264_encoder_encode_rgb24(stream->encoder,
                                                 stream->rgb,
                                                 bottom_up,
                                                 now_us,
                                                 config,
                                                 sizeof(config),
                                                 &config_size,
                                                 stream->encoded + 8,
                                                 VIDEO_CAPACITY - 8u,
                                                 &frame_size,
                                                 &keyframe,
                                                 error_out,
                                                 error_out_size);
    uint64_t encode_elapsed_us = performance_us() - encode_started_us;
    stream->metrics.encode_samples++;
    stream->metrics.encode_total_us += encode_elapsed_us;
    if (encode_elapsed_us > stream->metrics.encode_max_us) {
        stream->metrics.encode_max_us = clamp_u64_u32(encode_elapsed_us);
    }
    if (encoded <= 0) return encoded;
    stream->metrics.encoded_frames++;
    if (config_size) {
        memcpy(stream->h264_config, config, config_size);
        stream->h264_config_size = config_size;
        int sent = integral_media_relay_send_media(connection,
                                              INTEGRAL_MEDIA_MESSAGE_H264_CONFIG,
                                              0,
                                              ++stream->video_sequence,
                                              config,
                                              config_size,
                                              error_out,
                                              error_out_size);
        record_send_result(stream, sent, now_us);
        if (sent <= 0) return sent;
    }
    put_be64(stream->encoded, wall_clock_us());
    int sent = integral_media_relay_send_media(connection,
                                          INTEGRAL_MEDIA_MESSAGE_H264_FRAME,
                                          keyframe ? INTEGRAL_MEDIA_FLAG_H264_KEYFRAME : 0,
                                          ++stream->video_sequence,
                                          stream->encoded,
                                          frame_size + 8u,
                                          error_out,
                                          error_out_size);
    record_send_result(stream, sent, now_us);
    return sent;
}

int integral_n64_runtime_media_stream_submit_host_rgb24(IntegralN64RuntimeMediaStream *stream,
                                                 const uint8_t *rgb,
                                                 uint16_t width,
                                                 uint16_t height,
                                                 bool bottom_up,
                                                 uint64_t now_us,
                                                 char *error_out,
                                                 size_t error_out_size)
{
    size_t bytes = (size_t)width * height * 3u;
    if (!stream || !rgb || !width || !height || bytes > RGB_CAPACITY) {
        set_error(error_out, error_out_size, "GB async RGB frame invalid");
        return -1;
    }
    if (ensure_host_encode_worker(stream, error_out, error_out_size) != 0) return -1;
    SDL_LockMutex(stream->host_encode_mutex);
    if (stream->host_encode_failed) {
        set_error(error_out, error_out_size, stream->host_encode_error);
        SDL_UnlockMutex(stream->host_encode_mutex);
        return -1;
    }
    if (stream->host_input_ready) stream->metrics.capture_overwrites++;
    memcpy(stream->host_encode_rgb, rgb, bytes);
    stream->host_input_width = width;
    stream->host_input_height = height;
    stream->host_input_bottom_up = bottom_up;
    stream->host_input_timestamp_us = now_us;
    stream->host_input_ready = true;
    stream->metrics.capture_frames++;
    SDL_CondSignal(stream->host_encode_condition);
    SDL_UnlockMutex(stream->host_encode_mutex);
    return 1;
}

int integral_n64_runtime_media_stream_pump_host_video_output(
    IntegralN64RuntimeMediaStream *stream,
    IntegralMediaRelayConnection *connection,
    uint64_t now_us,
    char *error_out,
    size_t error_out_size)
{
    if (!stream || !connection) return -1;
    SDL_LockMutex(stream->host_encode_mutex);
    if (stream->host_encode_failed) {
        set_error(error_out, error_out_size, stream->host_encode_error);
        SDL_UnlockMutex(stream->host_encode_mutex);
        return -1;
    }
    bool ready = stream->host_output_ready;
    bool config_pending = stream->host_output_config_pending;
    uint32_t config_size = stream->host_output_config_size;
    uint32_t frame_size = stream->host_output_frame_size;
    bool keyframe = stream->host_output_keyframe;
    SDL_UnlockMutex(stream->host_encode_mutex);
    if (!ready) return 0;

    if (config_pending) {
        int sent = integral_media_relay_send_media(
            connection, INTEGRAL_MEDIA_MESSAGE_H264_CONFIG, 0,
            stream->video_sequence + 1u, stream->host_output_config,
            config_size, error_out, error_out_size);
        record_send_result(stream, sent, now_us);
        if (sent <= 0) return sent;
        stream->video_sequence++;
        memcpy(stream->h264_config, stream->host_output_config, config_size);
        stream->h264_config_size = config_size;
        SDL_LockMutex(stream->host_encode_mutex);
        stream->host_output_config_pending = false;
        SDL_UnlockMutex(stream->host_encode_mutex);
    }
    int sent = integral_media_relay_send_media(
        connection, INTEGRAL_MEDIA_MESSAGE_H264_FRAME,
        keyframe ? INTEGRAL_MEDIA_FLAG_H264_KEYFRAME : 0,
        stream->video_sequence + 1u, stream->host_encoded,
        frame_size, error_out, error_out_size);
    record_send_result(stream, sent, now_us);
    if (sent <= 0) return sent;
    stream->video_sequence++;
    SDL_LockMutex(stream->host_encode_mutex);
    stream->host_output_ready = false;
    stream->host_output_frame_size = 0;
    SDL_CondSignal(stream->host_encode_condition);
    SDL_UnlockMutex(stream->host_encode_mutex);
    return 1;
}

static int send_host_audio(IntegralN64RuntimeMediaStream *stream,
                           IntegralMediaRelayConnection *connection,
                           uint64_t now_us,
                           char *error_out,
                           size_t error_out_size)
{
    int16_t pcm[INTEGRAL_N64_RUNTIME_MEDIA_MAX_AUDIO_BYTES / 2u];
    uint32_t sample_rate = 0, channels = 0, bytes = 0;
    uint64_t capture_age_us = 0;
    int read = integral_n64_runtime_remote_media_read_audio(&stream->ipc_audio_sequence,
                                                  pcm,
                                                  sizeof(pcm),
                                                  &sample_rate,
                                                  &channels,
                                                  &bytes,
                                                  &capture_age_us);
    if (read <= 0) return read;
    stream->metrics.audio_source_frames++;
    if (channels != 2 || !sample_rate || bytes < 4 || bytes % 4u) {
        set_error(error_out, error_out_size, "N64 Runtime PCM frame invalid"); return -1;
    }
    uint32_t frame_count_32 = bytes / 4u;
    if (frame_count_32 > UINT16_MAX) return -1;
    uint16_t frame_count = (uint16_t)frame_count_32;
    return integral_n64_runtime_media_stream_send_host_pcm(stream,
                                                   connection,
                                                   pcm,
                                                   frame_count,
                                                   sample_rate,
                                                   now_us,
                                                   error_out,
                                                   error_out_size);
}

int integral_n64_runtime_media_stream_send_host_pcm(IntegralN64RuntimeMediaStream *stream,
                                             IntegralMediaRelayConnection *connection,
                                             const int16_t *pcm_stereo,
                                             uint16_t frame_count,
                                             uint32_t sample_rate,
                                             uint64_t now_us,
                                             char *error_out,
                                             size_t error_out_size)
{
    if (!stream || !connection || !pcm_stereo || !frame_count ||
        sample_rate < 8000u || sample_rate > 192000u) {
        set_error(error_out, error_out_size, "GB prototype PCM frame invalid");
        return -1;
    }
    size_t encoded_size = integral_adpcm_stereo_encoded_size(frame_count);
    uint8_t payload[INTEGRAL_MEDIA_MAX_AUDIO_BYTES];
    if (16u + encoded_size > sizeof(payload) ||
        integral_adpcm_encode_stereo(pcm_stereo, frame_count, payload + 16u,
                                sizeof(payload) - 16u) != 0) {
        set_error(error_out, error_out_size, "N64 Runtime ADPCM encode failed"); return -1;
    }
    put_be32(payload, sample_rate); put_be16(payload + 4, 2); put_be16(payload + 6, frame_count);
    put_be64(payload + 8, wall_clock_us());
    int sent = integral_media_relay_send_media(connection,
                                          INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM,
                                          0,
                                          ++stream->audio_sequence,
                                          payload,
                                          (uint32_t)(16u + encoded_size),
                                          error_out,
                                          error_out_size);
    record_send_result(stream, sent, now_us);
    if (sent > 0) stream->metrics.audio_sent_frames++;
    return sent;
}

int integral_n64_runtime_media_stream_pump_host(IntegralN64RuntimeMediaStream *stream,
                                         IntegralMediaRelayConnection *connection,
                                         uint64_t now_us,
                                         char *error_out,
                                         size_t error_out_size)
{
    if (!stream || !connection || !stream->ipc_open) return 0;
    int video = send_host_video(stream, connection, now_us, error_out, error_out_size);
    if (video < 0) return -1;
    int audio = send_host_audio(stream, connection, now_us, error_out, error_out_size);
    sample_relay_pending(stream, now_us);
    return audio < 0 ? -1 : video > 0 || audio > 0;
}

static int ensure_audio_device(IntegralN64RuntimeMediaStream *stream,
                               unsigned sample_rate,
                               char *error_out,
                               size_t error_out_size)
{
    if (stream->audio_device && stream->audio_rate == sample_rate) return 0;
    if (SDL_WasInit(SDL_INIT_AUDIO) == 0 && SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        set_error(error_out, error_out_size, SDL_GetError());
        return -1;
    }
    if (stream->audio_device) SDL_CloseAudioDevice(stream->audio_device);
    SDL_AudioSpec desired;
    SDL_zero(desired);
    desired.freq = (int)sample_rate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 2;
    desired.samples = 1024;
    stream->audio_device = SDL_OpenAudioDevice(NULL, 0, &desired, NULL, 0);
    if (!stream->audio_device) {
        set_error(error_out, error_out_size, SDL_GetError()); return -1;
    }
    stream->audio_rate = sample_rate;
    SDL_PauseAudioDevice(stream->audio_device, 0);
    return 0;
}

static int play_audio_payload(IntegralN64RuntimeMediaStream *stream,
                              const uint8_t *payload,
                              size_t payload_size,
                              char *error_out,
                              size_t error_out_size)
{
    if (payload_size < 24) {
        set_error(error_out, error_out_size, "N64 Runtime ADPCM payload too small");
        return -1;
    }
    uint32_t sample_rate = get_be32(payload);
    uint16_t channels = get_be16(payload + 4);
    uint16_t frame_count = get_be16(payload + 6);
    size_t encoded_size = integral_adpcm_stereo_encoded_size(frame_count);
    if (channels != 2 || 16u + encoded_size != payload_size) {
        set_error(error_out, error_out_size, "N64 Runtime ADPCM payload invalid");
        return -1;
    }
    int16_t *pcm = malloc((size_t)frame_count * 2u * sizeof(*pcm));
    if (!pcm || integral_adpcm_decode_stereo(payload + 16u, encoded_size, frame_count,
                                        pcm, (size_t)frame_count * 2u) != 0 ||
        ensure_audio_device(stream, sample_rate, error_out, error_out_size) != 0) {
        free(pcm); return -1;
    }
    Uint32 pcm_bytes = (Uint32)((size_t)frame_count * 4u);
    sample_audio_state(stream);
    if (integral_audio_queue_would_exceed(SDL_GetQueuedAudioSize(stream->audio_device),
                                     pcm_bytes,
                                     sample_rate,
                                     2,
                                     sizeof(int16_t),
                                     AUDIO_QUEUE_CAP_MS)) {
        SDL_ClearQueuedAudio(stream->audio_device);
        stream->metrics.audio_queue_clears++;
    }
    int result = SDL_QueueAudio(stream->audio_device, pcm, pcm_bytes);
    free(pcm);
    if (result != 0) { set_error(error_out, error_out_size, SDL_GetError()); return -1; }
    return 0;
}

static int sync_remote_video_frame(IntegralN64RuntimeMediaStream *stream,
                                   char *error_out,
                                   size_t error_out_size)
{
    SDL_LockMutex(stream->decode_mutex);
    if (stream->decode_failed) {
        set_error(error_out, error_out_size, stream->decode_error);
        SDL_UnlockMutex(stream->decode_mutex);
        return -1;
    }
    if (!stream->decoded_frame_sequence ||
        stream->decoded_frame_sequence == stream->displayed_frame_sequence) {
        SDL_UnlockMutex(stream->decode_mutex);
        return 0;
    }
    uint16_t width = stream->decoded_width;
    uint16_t height = stream->decoded_height;
    bool fixed_target = stream->video_window_target_width != 0u &&
                        stream->video_window_target_height != 0u;
    if (!stream->video_window) {
        int display_index = 0;
        if (stream->parent_window) {
            int parent_display = SDL_GetWindowDisplayIndex(stream->parent_window);
            if (parent_display >= 0) display_index = parent_display;
        }
        SDL_Rect usable = {0, 0, width, height};
        if (SDL_GetDisplayUsableBounds(display_index, &usable) != 0) {
            usable.w = width;
            usable.h = height;
        }
        stream->video_window_scale_resolved = fixed_target
            ? 1u
            : integral_display_scale_resolve(
                  stream->video_window_scale, usable.w, usable.h, width, height);
        int outer_width = fixed_target
            ? (int)stream->video_window_target_width
            : (int)width * (int)stream->video_window_scale_resolved;
        int outer_height = fixed_target
            ? (int)stream->video_window_target_height
            : (int)height * (int)stream->video_window_scale_resolved;
        if (fixed_target) SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
        Uint32 window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                              SDL_WINDOW_RESIZABLE;
        stream->video_window = SDL_CreateWindow(stream->window_title,
                                                SDL_WINDOWPOS_CENTERED,
                                                SDL_WINDOWPOS_CENTERED,
                                                outer_width,
                                                outer_height,
                                                window_flags);
        if (!stream->video_window) {
            set_error(error_out, error_out_size, SDL_GetError());
            SDL_UnlockMutex(stream->decode_mutex);
            return -1;
        }
        if (fixed_target) {
            SDL_SetWindowMinimumSize(stream->video_window, (int)width, (int)height);
        }
        stream->video_window_id = SDL_GetWindowID(stream->video_window);
        Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
        if (stream->video_vsync_enabled) renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
        int renderer_index = -1;
        if (stream->video_renderer_driver_requested[0]) {
            int renderer_count = SDL_GetNumRenderDrivers();
            for (int index = 0; index < renderer_count; index++) {
                SDL_RendererInfo candidate;
                memset(&candidate, 0, sizeof(candidate));
                if (SDL_GetRenderDriverInfo(index, &candidate) == 0 && candidate.name &&
                    strcmp(candidate.name, stream->video_renderer_driver_requested) == 0) {
                    renderer_index = index;
                    break;
                }
            }
            if (renderer_index < 0 && stream->video_renderer_driver_required) {
                set_error(error_out, error_out_size, "requested SDL renderer unavailable");
                SDL_DestroyWindow(stream->video_window);
                stream->video_window = NULL;
                stream->video_window_id = 0;
                SDL_UnlockMutex(stream->decode_mutex);
                return -1;
            }
        }
        stream->video_renderer = SDL_CreateRenderer(stream->video_window,
                                                    renderer_index,
                                                    renderer_flags);
        if (!stream->video_renderer && renderer_index >= 0 &&
            !stream->video_renderer_driver_required) {
            renderer_index = -1;
            stream->video_renderer = SDL_CreateRenderer(stream->video_window,
                                                        renderer_index,
                                                        renderer_flags);
        }
        if (!stream->video_renderer) {
            stream->video_renderer = SDL_CreateRenderer(stream->video_window,
                                                        renderer_index,
                                                        0);
        }
        if (!stream->video_renderer) {
            set_error(error_out, error_out_size, SDL_GetError());
            SDL_DestroyWindow(stream->video_window);
            stream->video_window = NULL;
            stream->video_window_id = 0;
            SDL_UnlockMutex(stream->decode_mutex);
            return -1;
        }
        if (fixed_target &&
            SDL_RenderSetLogicalSize(stream->video_renderer,
                                     (int)width,
                                     (int)height) != 0) {
            set_error(error_out, error_out_size, SDL_GetError());
            SDL_DestroyRenderer(stream->video_renderer);
            stream->video_renderer = NULL;
            SDL_DestroyWindow(stream->video_window);
            stream->video_window = NULL;
            stream->video_window_id = 0;
            SDL_UnlockMutex(stream->decode_mutex);
            return -1;
        }
        if (fixed_target) {
            (void)SDL_RenderSetIntegerScale(stream->video_renderer, SDL_TRUE);
        }
        SDL_RendererInfo renderer_info;
        memset(&renderer_info, 0, sizeof(renderer_info));
        stream->video_present_vsync =
            SDL_GetRendererInfo(stream->video_renderer, &renderer_info) == 0 &&
            (renderer_info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
        snprintf(stream->video_renderer_driver, sizeof(stream->video_renderer_driver), "%s",
                 renderer_info.name ? renderer_info.name : "unknown");
        int actual_display_index = SDL_GetWindowDisplayIndex(stream->video_window);
        SDL_DisplayMode display_mode;
        memset(&display_mode, 0, sizeof(display_mode));
        if (actual_display_index >= 0 &&
            SDL_GetCurrentDisplayMode(actual_display_index, &display_mode) == 0 &&
            display_mode.refresh_rate > 0) {
            stream->video_refresh_hz = (uint32_t)display_mode.refresh_rate;
        }
    }
    if (!stream->texture || stream->texture_width != width || stream->texture_height != height) {
        if (stream->texture) SDL_DestroyTexture(stream->texture);
        stream->texture = SDL_CreateTexture(stream->video_renderer, SDL_PIXELFORMAT_RGB24,
                                            SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!stream->texture) {
            set_error(error_out, error_out_size, SDL_GetError());
            SDL_UnlockMutex(stream->decode_mutex);
            return -1;
        }
#if SDL_VERSION_ATLEAST(2, 0, 12)
        if (fixed_target) {
            SDL_SetTextureScaleMode(stream->texture, SDL_ScaleModeNearest);
        }
#endif
        stream->texture_width = width; stream->texture_height = height;
        if (!fixed_target) {
            SDL_SetWindowSize(stream->video_window,
                              (int)width * (int)stream->video_window_scale_resolved,
                              (int)height * (int)stream->video_window_scale_resolved);
        }
        if (fixed_target &&
            SDL_RenderSetLogicalSize(stream->video_renderer,
                                     (int)width,
                                     (int)height) != 0) {
            set_error(error_out, error_out_size, SDL_GetError());
            SDL_UnlockMutex(stream->decode_mutex);
            return -1;
        }
    }
    if (SDL_UpdateTexture(stream->texture, NULL, stream->rgb, width * 3u) != 0) {
        set_error(error_out, error_out_size, SDL_GetError());
        SDL_UnlockMutex(stream->decode_mutex);
        return -1;
    }
    stream->displayed_frame_sequence = stream->decoded_frame_sequence;
    stream->texture_dirty = true;
    SDL_UnlockMutex(stream->decode_mutex);
    return 1;
}

int integral_n64_runtime_media_stream_pump_remote(IntegralN64RuntimeMediaStream *stream,
                                           IntegralMediaRelayConnection *connection,
                                           uint64_t now_us,
                                           char *error_out,
                                           size_t error_out_size)
{
    if (!stream || !connection) return -1;
    int changed = 0;
    for (unsigned count = 0; count < 16; count++) {
        uint8_t type = 0; uint16_t flags = 0; uint32_t sequence = 0, size = 0;
        int received = integral_media_relay_poll_media(connection, &type, &flags, &sequence,
                                                  stream->received, VIDEO_CAPACITY, &size,
                                                  error_out, error_out_size);
        (void)flags;
        if (received < 0) return -1;
        if (!received) break;
        if (type == INTEGRAL_MEDIA_MESSAGE_H264_CONFIG ||
            type == INTEGRAL_MEDIA_MESSAGE_H264_FRAME) {
            if (type == INTEGRAL_MEDIA_MESSAGE_H264_CONFIG) {
                integral_audio_jitter_init(&stream->jitter, 3, 60000);
                if (stream->audio_device) SDL_ClearQueuedAudio(stream->audio_device);
            }
            if (enqueue_decode_packet(stream, type, stream->received, size,
                                      error_out, error_out_size) != 0) return -1;
        }
        if (type == INTEGRAL_MEDIA_MESSAGE_H264_FRAME) {
            stream->metrics.received_frames++;
        } else if (type == INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM) {
            stream->metrics.audio_received_frames++;
            if (size > UINT16_MAX || integral_audio_jitter_push(&stream->jitter, sequence,
                                                           stream->received, (uint16_t)size) < 0) {
                set_error(error_out, error_out_size, "N64 Runtime ADPCM jitter buffer rejected packet");
                return -1;
            }
            sample_audio_state(stream);
        }
        else if (type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK ||
                 type == INTEGRAL_MEDIA_MESSAGE_GB_PONG ||
                 type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL ||
                 type == INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE) {
            if (size > sizeof(stream->remote_control_payload)) {
                set_error(error_out, error_out_size, "GB prototype control payload too large");
                return -1;
            }
            stream->remote_control_type = type;
            stream->remote_control_sequence = sequence;
            memcpy(stream->remote_control_payload, stream->received, size);
            stream->remote_control_payload_size = size;
            stream->remote_control_ready = true;
            break;
        }
    }
    for (unsigned count = 0; count < INTEGRAL_AUDIO_JITTER_SLOT_COUNT; count++) {
        uint8_t payload[INTEGRAL_AUDIO_MAX_PAYLOAD]; uint16_t size = 0; uint32_t sequence = 0;
        IntegralAudioJitterResult result = integral_audio_jitter_pop(&stream->jitter, now_us,
                                                           payload, sizeof(payload), &size, &sequence);
        (void)sequence;
        if (result == INTEGRAL_AUDIO_JITTER_WAIT) break;
        if (result == INTEGRAL_AUDIO_JITTER_CONCEAL) {
            stream->metrics.audio_conceals++;
            continue;
        }
        if (play_audio_payload(stream, payload, size, error_out, error_out_size)) return -1;
        sample_audio_state(stream);
    }
    sample_audio_state(stream);
    int video_changed = sync_remote_video_frame(stream, error_out, error_out_size);
    if (video_changed < 0) return -1;
    changed |= video_changed;
    return changed;
}

bool integral_n64_runtime_media_stream_take_remote_control(IntegralN64RuntimeMediaStream *stream,
                                                    uint8_t *message_type_out,
                                                    uint32_t *sequence_out,
                                                    void *payload_out,
                                                    size_t payload_capacity,
                                                    uint32_t *payload_size_out)
{
    if (!stream || !stream->remote_control_ready || !message_type_out || !sequence_out ||
        !payload_out || !payload_size_out ||
        payload_capacity < stream->remote_control_payload_size) return false;
    *message_type_out = stream->remote_control_type;
    *sequence_out = stream->remote_control_sequence;
    *payload_size_out = stream->remote_control_payload_size;
    memcpy(payload_out, stream->remote_control_payload, stream->remote_control_payload_size);
    stream->remote_control_ready = false;
    return true;
}

bool integral_n64_runtime_media_stream_render(IntegralN64RuntimeMediaStream *stream,
                                       SDL_Renderer *renderer,
                                       const SDL_Rect *bounds)
{
    (void)renderer;
    (void)bounds;
    if (!stream || !stream->video_window || !stream->video_renderer || !stream->texture ||
        !stream->texture_width || !stream->texture_height) return false;
    /* Do not block the Remote control/network loop on a duplicate VSync
     * present.  A decoded frame can otherwise arrive just after pump_remote()
     * and wait through an unnecessary refresh before it is synchronized on
     * the next iteration, turning WAN arrival jitter into visible frame loss. */
    if (!stream->texture_dirty && !stream->exit_confirming) return true;
    bool fixed_target = stream->video_window_target_width != 0u &&
                        stream->video_window_target_height != 0u;
    int window_width = fixed_target ? (int)stream->texture_width : 0;
    int window_height = fixed_target ? (int)stream->texture_height : 0;
    if (!fixed_target) {
        SDL_GetRendererOutputSize(stream->video_renderer,
                                  &window_width,
                                  &window_height);
    }
    if (window_width <= 0 || window_height <= 0) return false;
    SDL_Rect destination = {.x = 0, .y = 0, .w = window_width, .h = window_height};
    if (!fixed_target) {
        double source_aspect = (double)stream->texture_width / stream->texture_height;
        double target_aspect = (double)window_width / window_height;
        if (source_aspect > target_aspect) {
            destination.h = (int)(window_width / source_aspect);
            destination.y += (window_height - destination.h) / 2;
        } else {
            destination.w = (int)(window_height * source_aspect);
            destination.x += (window_width - destination.w) / 2;
        }
    }
    SDL_SetRenderDrawColor(stream->video_renderer, 0, 0, 0, 255);
    SDL_RenderClear(stream->video_renderer);
    SDL_RenderCopy(stream->video_renderer, stream->texture, NULL, &destination);
    if (stream->exit_confirming) {
        int text_scale = window_width >= 440 && window_height >= 160
                             ? 3
                             : (window_width >= 280 ? 2 : 1);
        int panel_width = window_width > 428 ? 420 : window_width - 8;
        int panel_height = window_height > 148 ? 140 : window_height - 8;
        SDL_Rect panel = {.x = (window_width - panel_width) / 2,
                          .y = (window_height - panel_height) / 2,
                          .w = panel_width,
                          .h = panel_height};
        SDL_Color title = {238, 238, 220, 255};
        SDL_Color text = {185, 205, 216, 255};
        SDL_Color selected = {86, 220, 150, 255};
        SDL_SetRenderDrawColor(stream->video_renderer, 10, 14, 18, 235);
        SDL_RenderFillRect(stream->video_renderer, &panel);
        SDL_SetRenderDrawColor(stream->video_renderer, 86, 162, 126, 255);
        SDL_RenderDrawRect(stream->video_renderer, &panel);
        integral_sdl_draw_text(stream->video_renderer, panel.x + 12, panel.y + 18,
                               "EXIT GAME?", text_scale, title);
        if (stream->exit_discard_warning) {
            integral_sdl_draw_text(stream->video_renderer, panel.x + 12, panel.y + 58,
                                   "SAVES FOR BOTH PLAYERS", 1, text);
            integral_sdl_draw_text(stream->video_renderer, panel.x + 12, panel.y + 72,
                                   "WILL NOT BE UPDATED.", 1, text);
        }
        integral_sdl_draw_text(stream->video_renderer, panel.x + 12, panel.y + panel.h - 32,
                               stream->exit_confirm_yes ? "> YES" : "  YES", text_scale,
                               stream->exit_confirm_yes ? selected : text);
        integral_sdl_draw_text(stream->video_renderer, panel.x + panel.w / 2, panel.y + panel.h - 32,
                               stream->exit_confirm_yes ? "  NO" : "> NO", text_scale,
                               stream->exit_confirm_yes ? text : selected);
    }
    uint64_t present_started_us = performance_us();
    SDL_RenderPresent(stream->video_renderer);
    uint32_t present_call_us = clamp_u64_u32(performance_us() - present_started_us);
    stream->metrics.present_call_total_us += present_call_us;
    stream->metrics.present_call_samples++;
    if (present_call_us > stream->metrics.present_call_max_us) {
        stream->metrics.present_call_max_us = present_call_us;
    }
    stream->texture_dirty = false;
    uint64_t presented_us = performance_us();
    if (stream->metrics.last_present_us && presented_us >= stream->metrics.last_present_us) {
        uint64_t interval_us_64 = presented_us - stream->metrics.last_present_us;
        uint32_t interval_us = clamp_u64_u32(interval_us_64);
        unsigned bucket = interval_us / PRESENT_HISTOGRAM_BUCKET_US;
        if (bucket >= PRESENT_HISTOGRAM_BUCKETS) bucket = PRESENT_HISTOGRAM_BUCKETS - 1u;
        stream->metrics.present_histogram[bucket]++;
        stream->metrics.present_interval_count++;
        if (interval_us > stream->metrics.present_interval_max_us) {
            stream->metrics.present_interval_max_us = interval_us;
        }
    }
    stream->metrics.last_present_us = presented_us;
    stream->metrics.presented_frames++;
    return true;
}

void integral_n64_runtime_media_stream_set_exit_confirmation(
    IntegralN64RuntimeMediaStream *stream, bool active, bool yes_selected)
{
    if (!stream) return;
    stream->exit_confirming = active;
    stream->exit_confirm_yes = active && yes_selected;
    stream->texture_dirty = true;
}

void integral_n64_runtime_media_stream_set_exit_discard_warning(
    IntegralN64RuntimeMediaStream *stream, bool active)
{
    if (!stream) return;
    stream->exit_discard_warning = active;
    stream->texture_dirty = true;
}

static uint32_t present_percentile_us(const MetricsAccumulator *metrics, unsigned percentile)
{
    if (!metrics->present_interval_count) return 0;
    uint64_t target = ((uint64_t)metrics->present_interval_count * percentile + 99u) / 100u;
    uint64_t seen = 0;
    for (unsigned index = 0; index < PRESENT_HISTOGRAM_BUCKETS; index++) {
        seen += metrics->present_histogram[index];
        if (seen >= target) {
            uint64_t upper_us = (uint64_t)(index + 1u) * PRESENT_HISTOGRAM_BUCKET_US;
            return clamp_u64_u32(upper_us);
        }
    }
    return metrics->present_interval_max_us;
}

static uint64_t producer_counter_delta(uint64_t current, uint64_t baseline)
{
    return current >= baseline ? current - baseline : current;
}

bool integral_n64_runtime_media_stream_take_metrics(IntegralN64RuntimeMediaStream *stream,
                                             uint64_t now_us,
                                             IntegralN64RuntimeMediaMetrics *metrics)
{
    if (!stream || !metrics) return false;
    if (!stream->metrics.window_started_us) {
        stream->metrics.window_started_us = now_us ? now_us : 1u;
        if (stream->ipc_open) {
            (void)integral_n64_runtime_remote_media_read_producer_metrics(
                &stream->producer_metrics_baseline);
        }
        return false;
    }
    if (now_us < stream->metrics.window_started_us ||
        now_us - stream->metrics.window_started_us < METRICS_WINDOW_US) return false;

    sample_relay_pending(stream, now_us);
    sample_audio_state(stream);
    DecodeWorkerMetrics decode_metrics;
    HostEncodeWorkerMetrics host_encode_metrics;
    memset(&decode_metrics, 0, sizeof(decode_metrics));
    memset(&host_encode_metrics, 0, sizeof(host_encode_metrics));
    SDL_LockMutex(stream->host_encode_mutex);
    host_encode_metrics = stream->host_encode_metrics;
    memset(&stream->host_encode_metrics, 0, sizeof(stream->host_encode_metrics));
    SDL_UnlockMutex(stream->host_encode_mutex);
    SDL_LockMutex(stream->decode_mutex);
    decode_metrics = stream->decode_metrics;
    memset(&stream->decode_metrics, 0, sizeof(stream->decode_metrics));
    SDL_UnlockMutex(stream->decode_mutex);
    memset(metrics, 0, sizeof(*metrics));
    uint64_t window_us = now_us - stream->metrics.window_started_us;
    metrics->window_ms = clamp_u64_u32(window_us / 1000u);
    metrics->capture_frames = stream->metrics.capture_frames;
    metrics->capture_overwrites = stream->metrics.capture_overwrites;
    if (stream->ipc_open) {
        IntegralN64RuntimeRemoteMediaProducerMetrics producer;
        if (integral_n64_runtime_remote_media_read_producer_metrics(&producer) == 0) {
            const IntegralN64RuntimeRemoteMediaProducerMetrics *baseline =
                &stream->producer_metrics_baseline;
            uint64_t callback_samples = producer_counter_delta(
                producer.core_callback_interval_samples,
                baseline->core_callback_interval_samples);
            uint64_t callback_total_us = producer_counter_delta(
                producer.core_callback_interval_total_us,
                baseline->core_callback_interval_total_us);
            uint64_t readback_samples = producer_counter_delta(
                producer.readback_samples, baseline->readback_samples);
            uint64_t readback_total_us = producer_counter_delta(
                producer.readback_total_us, baseline->readback_total_us);
            uint64_t ipc_write_samples = producer_counter_delta(
                producer.ipc_write_samples, baseline->ipc_write_samples);
            uint64_t ipc_write_total_us = producer_counter_delta(
                producer.ipc_write_total_us, baseline->ipc_write_total_us);
            metrics->source_core_callbacks = clamp_u64_u32(producer_counter_delta(
                producer.core_callbacks, baseline->core_callbacks));
            metrics->source_capture_due = clamp_u64_u32(producer_counter_delta(
                producer.capture_due, baseline->capture_due));
            metrics->source_capture_success = clamp_u64_u32(producer_counter_delta(
                producer.capture_success, baseline->capture_success));
            metrics->source_capture_failures = clamp_u64_u32(producer_counter_delta(
                producer.capture_failures, baseline->capture_failures));
            if (callback_samples) {
                metrics->source_callback_interval_avg_us = clamp_u64_u32(
                    callback_total_us / callback_samples);
            }
            metrics->source_callback_interval_max_us =
                producer.core_callback_interval_max_us;
            if (readback_samples) {
                metrics->source_readback_avg_us = clamp_u64_u32(
                    readback_total_us / readback_samples);
            }
            metrics->source_readback_max_us = producer.readback_max_us;
            if (ipc_write_samples) {
                metrics->source_ipc_write_avg_us = clamp_u64_u32(
                    ipc_write_total_us / ipc_write_samples);
            }
            metrics->source_ipc_write_max_us = producer.ipc_write_max_us;
            stream->producer_metrics_baseline = producer;
        }
    }
    metrics->encoded_frames = stream->metrics.encoded_frames + host_encode_metrics.encoded_frames;
    metrics->received_frames = stream->metrics.received_frames;
    metrics->decoded_frames = decode_metrics.decoded_frames;
    metrics->presented_frames = stream->metrics.presented_frames;
    if (stream->metrics.capture_frames) {
        metrics->capture_age_avg_us = clamp_u64_u32(
            stream->metrics.capture_age_total_us / stream->metrics.capture_frames);
    }
    metrics->capture_age_max_us = stream->metrics.capture_age_max_us;
    uint32_t encode_samples = stream->metrics.encode_samples + host_encode_metrics.encode_samples;
    uint64_t encode_total_us = stream->metrics.encode_total_us + host_encode_metrics.encode_total_us;
    if (encode_samples) {
        metrics->encode_avg_us = clamp_u64_u32(
            encode_total_us / encode_samples);
    }
    metrics->encode_max_us = stream->metrics.encode_max_us > host_encode_metrics.encode_max_us
                                 ? stream->metrics.encode_max_us
                                 : host_encode_metrics.encode_max_us;
    if (decode_metrics.receive_age_samples) {
        metrics->receive_age_avg_us = clamp_u64_u32(
            decode_metrics.receive_age_total_us / decode_metrics.receive_age_samples);
    }
    metrics->receive_age_max_us = decode_metrics.receive_age_max_us;
    metrics->receive_age_invalid = decode_metrics.receive_age_invalid;
    if (decode_metrics.decode_samples) {
        metrics->decode_avg_us = clamp_u64_u32(
            decode_metrics.decode_total_us / decode_metrics.decode_samples);
    }
    metrics->decode_max_us = decode_metrics.decode_max_us;
    metrics->decode_queue_peak = decode_metrics.queue_peak;
    metrics->display_overwrites = decode_metrics.display_overwrites;
    metrics->present_p50_us = present_percentile_us(&stream->metrics, 50u);
    metrics->present_p95_us = present_percentile_us(&stream->metrics, 95u);
    metrics->present_max_us = stream->metrics.present_interval_max_us;
    if (stream->metrics.present_call_samples) {
        metrics->present_call_avg_us = clamp_u64_u32(
            stream->metrics.present_call_total_us / stream->metrics.present_call_samples);
    }
    metrics->present_call_max_us = stream->metrics.present_call_max_us;
    metrics->video_refresh_hz = stream->video_refresh_hz;
    if (stream->metrics.relay_pending_since_us && now_us >= stream->metrics.relay_pending_since_us) {
        metrics->relay_pending_ms = clamp_u64_u32(
            (now_us - stream->metrics.relay_pending_since_us) / 1000u);
    }
    metrics->relay_pending_max_ms = stream->metrics.relay_pending_max_ms;
    metrics->audio_source_frames = stream->metrics.audio_source_frames;
    metrics->audio_sent_frames = stream->metrics.audio_sent_frames;
    metrics->audio_received_frames = stream->metrics.audio_received_frames;
    metrics->audio_jitter_packets = stream->jitter.packet_count;
    metrics->audio_jitter_peak_packets = stream->metrics.audio_jitter_peak_packets;
    metrics->audio_conceals = stream->metrics.audio_conceals;
    metrics->audio_queue_ms = audio_queue_ms(stream);
    metrics->audio_queue_max_ms = stream->metrics.audio_queue_max_ms;
    metrics->audio_queue_clears = stream->metrics.audio_queue_clears;

    uint64_t last_present_us = stream->metrics.last_present_us;
    uint64_t relay_pending_since_us = stream->metrics.relay_pending_since_us;
    memset(&stream->metrics, 0, sizeof(stream->metrics));
    stream->metrics.window_started_us = now_us ? now_us : 1u;
    stream->metrics.last_present_us = last_present_us;
    stream->metrics.relay_pending_since_us = relay_pending_since_us;
    return true;
}

bool integral_n64_runtime_media_stream_is_video_window(const IntegralN64RuntimeMediaStream *stream,
                                                uint32_t window_id)
{
    return stream && stream->video_window && stream->video_window_id != 0 &&
           stream->video_window_id == window_id;
}

bool integral_n64_runtime_media_stream_is_video_vsync_paced(const IntegralN64RuntimeMediaStream *stream)
{
    return stream && stream->video_window && stream->video_renderer &&
           stream->video_window_id != 0 && stream->video_present_vsync;
}

const char *integral_n64_runtime_media_stream_video_renderer_driver(
    const IntegralN64RuntimeMediaStream *stream)
{
    return stream && stream->video_renderer_driver[0]
               ? stream->video_renderer_driver
               : "unknown";
}

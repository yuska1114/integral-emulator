/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_MEDIA_CODEC_H
#define INTEGRAL_MEDIA_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_ADPCM_STEREO_HEADER_SIZE 8u
#define INTEGRAL_AUDIO_JITTER_SLOT_COUNT 16u
#define INTEGRAL_AUDIO_MAX_PAYLOAD 8192u

size_t integral_adpcm_stereo_encoded_size(uint16_t frame_count);
int integral_adpcm_encode_stereo(const int16_t *pcm,
                            uint16_t frame_count,
                            uint8_t *encoded,
                            size_t encoded_capacity);
int integral_adpcm_decode_stereo(const uint8_t *encoded,
                            size_t encoded_size,
                            uint16_t frame_count,
                            int16_t *pcm,
                            size_t sample_capacity);

typedef enum IntegralAudioJitterResult {
    INTEGRAL_AUDIO_JITTER_WAIT = 0,
    INTEGRAL_AUDIO_JITTER_PACKET = 1,
    INTEGRAL_AUDIO_JITTER_CONCEAL = 2
} IntegralAudioJitterResult;

typedef struct IntegralAudioJitterSlot {
    uint32_t sequence;
    uint16_t payload_size;
    bool occupied;
    uint8_t payload[INTEGRAL_AUDIO_MAX_PAYLOAD];
} IntegralAudioJitterSlot;

typedef struct IntegralAudioJitter {
    IntegralAudioJitterSlot slots[INTEGRAL_AUDIO_JITTER_SLOT_COUNT];
    uint32_t expected_sequence;
    uint64_t gap_started_us;
    uint64_t loss_deadline_us;
    unsigned target_packets;
    unsigned packet_count;
    bool started;
} IntegralAudioJitter;

void integral_audio_jitter_init(IntegralAudioJitter *jitter,
                           unsigned target_packets,
                           uint64_t loss_deadline_us);
int integral_audio_jitter_push(IntegralAudioJitter *jitter,
                          uint32_t sequence,
                          const void *payload,
                          uint16_t payload_size);
IntegralAudioJitterResult integral_audio_jitter_pop(IntegralAudioJitter *jitter,
                                           uint64_t now_us,
                                           uint8_t *payload,
                                           size_t payload_capacity,
                                           uint16_t *payload_size,
                                           uint32_t *sequence);

bool integral_audio_queue_would_exceed(uint32_t queued_bytes,
                                  uint32_t incoming_bytes,
                                  uint32_t sample_rate,
                                  uint32_t channels,
                                  uint32_t bytes_per_sample,
                                  uint32_t cap_ms);

#endif

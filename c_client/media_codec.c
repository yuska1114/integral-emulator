/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_codec.h"

#include <string.h>

static const int step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371,
    408, 449, 494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166,
    1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024,
    3327, 3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845,
    8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500,
    20350, 22385, 24623, 27086, 29794, 32767
};

static const int index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static int clamp_sample(int value)
{
    if (value < -32768) return -32768;
    if (value > 32767) return 32767;
    return value;
}

static int clamp_index(int value)
{
    if (value < 0) return 0;
    if (value > 88) return 88;
    return value;
}

static uint8_t encode_sample(int sample, int *predictor, int *index)
{
    int step = step_table[*index];
    int magnitude = sample - *predictor;
    int reconstructed = step >> 3;
    uint8_t code = 0;
    if (magnitude < 0) {
        code = 8;
        magnitude = -magnitude;
    }
    if (magnitude >= step) {
        code |= 4;
        magnitude -= step;
        reconstructed += step;
    }
    if (magnitude >= (step >> 1)) {
        code |= 2;
        magnitude -= step >> 1;
        reconstructed += step >> 1;
    }
    if (magnitude >= (step >> 2)) {
        code |= 1;
        reconstructed += step >> 2;
    }
    *predictor = clamp_sample(*predictor + ((code & 8) ? -reconstructed : reconstructed));
    *index = clamp_index(*index + index_table[code]);
    return code;
}

static int decode_sample(uint8_t code, int *predictor, int *index)
{
    int step = step_table[*index];
    int difference = step >> 3;
    if (code & 4) difference += step;
    if (code & 2) difference += step >> 1;
    if (code & 1) difference += step >> 2;
    *predictor = clamp_sample(*predictor + ((code & 8) ? -difference : difference));
    *index = clamp_index(*index + index_table[code & 15]);
    return *predictor;
}

static void put_s16_be(uint8_t *destination, int value)
{
    uint16_t bits = (uint16_t)(int16_t)value;
    destination[0] = (uint8_t)(bits >> 8);
    destination[1] = (uint8_t)bits;
}

static int get_s16_be(const uint8_t *source)
{
    uint16_t bits = (uint16_t)((uint16_t)source[0] << 8) | source[1];
    return (int16_t)bits;
}

size_t integral_adpcm_stereo_encoded_size(uint16_t frame_count)
{
    return frame_count ? INTEGRAL_ADPCM_STEREO_HEADER_SIZE + (size_t)frame_count - 1u : 0u;
}

int integral_adpcm_encode_stereo(const int16_t *pcm,
                            uint16_t frame_count,
                            uint8_t *encoded,
                            size_t encoded_capacity)
{
    size_t required = integral_adpcm_stereo_encoded_size(frame_count);
    int left;
    int right;
    int left_index = 0;
    int right_index = 0;
    if (!pcm || !encoded || !required || encoded_capacity < required) return -1;
    left = pcm[0];
    right = pcm[1];
    put_s16_be(encoded, left);
    encoded[2] = 0;
    encoded[3] = 0;
    put_s16_be(encoded + 4, right);
    encoded[6] = 0;
    encoded[7] = 0;
    for (uint16_t frame = 1; frame < frame_count; frame++) {
        uint8_t left_code = encode_sample(pcm[(size_t)frame * 2], &left, &left_index);
        uint8_t right_code = encode_sample(pcm[(size_t)frame * 2 + 1], &right, &right_index);
        encoded[INTEGRAL_ADPCM_STEREO_HEADER_SIZE + frame - 1u] =
            (uint8_t)(left_code | (uint8_t)(right_code << 4));
    }
    return 0;
}

int integral_adpcm_decode_stereo(const uint8_t *encoded,
                            size_t encoded_size,
                            uint16_t frame_count,
                            int16_t *pcm,
                            size_t sample_capacity)
{
    size_t required = integral_adpcm_stereo_encoded_size(frame_count);
    if (!encoded || !pcm || !required || encoded_size != required ||
        sample_capacity < (size_t)frame_count * 2) return -1;
    int left = get_s16_be(encoded);
    int right = get_s16_be(encoded + 4);
    int left_index = encoded[2];
    int right_index = encoded[6];
    if (left_index > 88 || right_index > 88 || encoded[3] || encoded[7]) return -1;
    pcm[0] = (int16_t)left;
    pcm[1] = (int16_t)right;
    for (uint16_t frame = 1; frame < frame_count; frame++) {
        uint8_t codes = encoded[INTEGRAL_ADPCM_STEREO_HEADER_SIZE + frame - 1u];
        pcm[(size_t)frame * 2] = (int16_t)decode_sample(codes & 15, &left, &left_index);
        pcm[(size_t)frame * 2 + 1] = (int16_t)decode_sample(codes >> 4, &right, &right_index);
    }
    return 0;
}

static bool sequence_before(uint32_t left, uint32_t right)
{
    return (int32_t)(left - right) < 0;
}

void integral_audio_jitter_init(IntegralAudioJitter *jitter,
                           unsigned target_packets,
                           uint64_t loss_deadline_us)
{
    if (!jitter) return;
    memset(jitter, 0, sizeof(*jitter));
    jitter->target_packets = target_packets ? target_packets : 1;
    if (jitter->target_packets > INTEGRAL_AUDIO_JITTER_SLOT_COUNT) {
        jitter->target_packets = INTEGRAL_AUDIO_JITTER_SLOT_COUNT;
    }
    jitter->loss_deadline_us = loss_deadline_us;
}

int integral_audio_jitter_push(IntegralAudioJitter *jitter,
                          uint32_t sequence,
                          const void *payload,
                          uint16_t payload_size)
{
    IntegralAudioJitterSlot *free_slot = NULL;
    if (!jitter || !payload || !payload_size || payload_size > INTEGRAL_AUDIO_MAX_PAYLOAD) return -1;
    if (jitter->started && sequence_before(sequence, jitter->expected_sequence)) return 1;
    for (unsigned index = 0; index < INTEGRAL_AUDIO_JITTER_SLOT_COUNT; index++) {
        IntegralAudioJitterSlot *slot = &jitter->slots[index];
        if (slot->occupied && slot->sequence == sequence) return 1;
        if (!slot->occupied && !free_slot) free_slot = slot;
    }
    /* Keep latency bounded when decoding temporarily falls behind. Audio loss is
       recoverable and must not tear down the video/control transport. */
    if (!free_slot) return 1;
    free_slot->sequence = sequence;
    free_slot->payload_size = payload_size;
    memcpy(free_slot->payload, payload, payload_size);
    free_slot->occupied = true;
    jitter->packet_count++;
    return 0;
}

static IntegralAudioJitterSlot *find_sequence(IntegralAudioJitter *jitter, uint32_t sequence)
{
    for (unsigned index = 0; index < INTEGRAL_AUDIO_JITTER_SLOT_COUNT; index++) {
        if (jitter->slots[index].occupied && jitter->slots[index].sequence == sequence) {
            return &jitter->slots[index];
        }
    }
    return NULL;
}

static IntegralAudioJitterSlot *find_earliest(IntegralAudioJitter *jitter)
{
    IntegralAudioJitterSlot *earliest = NULL;
    for (unsigned index = 0; index < INTEGRAL_AUDIO_JITTER_SLOT_COUNT; index++) {
        IntegralAudioJitterSlot *slot = &jitter->slots[index];
        if (slot->occupied && (!earliest || sequence_before(slot->sequence, earliest->sequence))) {
            earliest = slot;
        }
    }
    return earliest;
}

IntegralAudioJitterResult integral_audio_jitter_pop(IntegralAudioJitter *jitter,
                                           uint64_t now_us,
                                           uint8_t *payload,
                                           size_t payload_capacity,
                                           uint16_t *payload_size,
                                           uint32_t *sequence)
{
    if (!jitter || !payload || !payload_size || !sequence) return INTEGRAL_AUDIO_JITTER_WAIT;
    if (!jitter->started) {
        if (jitter->packet_count < jitter->target_packets) return INTEGRAL_AUDIO_JITTER_WAIT;
        IntegralAudioJitterSlot *first = find_earliest(jitter);
        if (!first) return INTEGRAL_AUDIO_JITTER_WAIT;
        jitter->expected_sequence = first->sequence;
        jitter->started = true;
    }
    IntegralAudioJitterSlot *slot = find_sequence(jitter, jitter->expected_sequence);
    if (slot) {
        if (payload_capacity < slot->payload_size) return INTEGRAL_AUDIO_JITTER_WAIT;
        memcpy(payload, slot->payload, slot->payload_size);
        *payload_size = slot->payload_size;
        *sequence = slot->sequence;
        slot->occupied = false;
        jitter->packet_count--;
        jitter->expected_sequence++;
        jitter->gap_started_us = 0;
        return INTEGRAL_AUDIO_JITTER_PACKET;
    }
    if (!jitter->packet_count) {
        jitter->gap_started_us = 0;
        return INTEGRAL_AUDIO_JITTER_WAIT;
    }
    if (!jitter->gap_started_us) {
        jitter->gap_started_us = now_us;
        return INTEGRAL_AUDIO_JITTER_WAIT;
    }
    if (now_us - jitter->gap_started_us < jitter->loss_deadline_us) return INTEGRAL_AUDIO_JITTER_WAIT;
    *payload_size = 0;
    *sequence = jitter->expected_sequence++;
    jitter->gap_started_us = 0;
    return INTEGRAL_AUDIO_JITTER_CONCEAL;
}

bool integral_audio_queue_would_exceed(uint32_t queued_bytes,
                                  uint32_t incoming_bytes,
                                  uint32_t sample_rate,
                                  uint32_t channels,
                                  uint32_t bytes_per_sample,
                                  uint32_t cap_ms)
{
    if (!sample_rate || !channels || !bytes_per_sample || !cap_ms) return false;
    uint64_t projected_bytes = (uint64_t)queued_bytes + incoming_bytes;
    uint64_t bytes_per_second = (uint64_t)sample_rate * channels * bytes_per_sample;
    return projected_bytes * 1000u > bytes_per_second * cap_ms;
}

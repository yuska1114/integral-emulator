/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_codec.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_FRAMES 1024u
#define PI 3.14159265358979323846

int main(void)
{
    int16_t source[TEST_FRAMES * 2];
    int16_t decoded[TEST_FRAMES * 2];
    uint8_t encoded[INTEGRAL_ADPCM_STEREO_HEADER_SIZE + TEST_FRAMES - 1];
    uint64_t absolute_error = 0;
    for (unsigned sample = 0; sample < TEST_FRAMES; sample++) {
        source[sample * 2] = (int16_t)(sin((double)sample * 2.0 * PI * 440.0 / 44100.0) * 24000.0);
        source[sample * 2 + 1] = (int16_t)(cos((double)sample * 2.0 * PI * 660.0 / 44100.0) * 18000.0);
    }
    assert(integral_adpcm_stereo_encoded_size(TEST_FRAMES) == sizeof(encoded));
    assert(integral_adpcm_encode_stereo(source, TEST_FRAMES, encoded, sizeof(encoded)) == 0);
    assert(integral_adpcm_decode_stereo(encoded, sizeof(encoded), TEST_FRAMES, decoded, TEST_FRAMES * 2) == 0);
    for (unsigned sample = 0; sample < TEST_FRAMES * 2; sample++) {
        int difference = (int)source[sample] - (int)decoded[sample];
        absolute_error += (uint64_t)(difference < 0 ? -difference : difference);
    }
    assert(absolute_error / (TEST_FRAMES * 2) <= 2000);

    IntegralAudioJitter jitter;
    uint8_t output[8];
    uint16_t output_size = 0;
    uint32_t sequence = 0;
    uint8_t value = 10;
    integral_audio_jitter_init(&jitter, 2, 30000);
    assert(integral_audio_jitter_push(&jitter, 10, &value, 1) == 0);
    value = 12;
    assert(integral_audio_jitter_push(&jitter, 12, &value, 1) == 0);
    assert(integral_audio_jitter_pop(&jitter, 1000, output, sizeof(output), &output_size, &sequence) == INTEGRAL_AUDIO_JITTER_PACKET);
    assert(sequence == 10 && output[0] == 10);
    assert(integral_audio_jitter_pop(&jitter, 100000, output, sizeof(output), &output_size, &sequence) == INTEGRAL_AUDIO_JITTER_WAIT);
    assert(integral_audio_jitter_pop(&jitter, 130000, output, sizeof(output), &output_size, &sequence) == INTEGRAL_AUDIO_JITTER_CONCEAL);
    assert(sequence == 11);
    assert(integral_audio_jitter_pop(&jitter, 130001, output, sizeof(output), &output_size, &sequence) == INTEGRAL_AUDIO_JITTER_PACKET);
    assert(sequence == 12 && output[0] == 12);

    integral_audio_jitter_init(&jitter, 3, 30000);
    for (unsigned index = 0; index < INTEGRAL_AUDIO_JITTER_SLOT_COUNT; index++) {
        value = (uint8_t)index;
        assert(integral_audio_jitter_push(&jitter, index + 1u, &value, 1) == 0);
    }
    value = 99;
    assert(integral_audio_jitter_push(&jitter,
                                 INTEGRAL_AUDIO_JITTER_SLOT_COUNT + 1u,
                                 &value,
                                 1) == 1);

    assert(!integral_audio_queue_would_exceed(0, 16384, 44100, 2, 2, 100));
    assert(integral_audio_queue_would_exceed(4096, 16384, 44100, 2, 2, 100));
    assert(!integral_audio_queue_would_exceed(4096, 16384, 0, 2, 2, 100));

    printf("media codec test passed: ADPCM=%zu mean-error=%llu\n",
           sizeof(encoded),
           (unsigned long long)(absolute_error / (TEST_FRAMES * 2)));
    return 0;
}

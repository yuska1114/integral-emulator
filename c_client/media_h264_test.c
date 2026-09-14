/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_h264.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_gb_test_pattern(unsigned char *rgb, unsigned width, unsigned height)
{
    static const unsigned char colors[4][3] = {
        {224u, 24u, 24u},
        {24u, 224u, 24u},
        {24u, 24u, 224u},
        {224u, 224u, 224u},
    };
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            size_t offset = ((size_t)y * width + x) * 3u;
            unsigned band = x * 4u / width;
            memcpy(rgb + offset, colors[band], 3u);
        }
    }
}

static bool gb_test_pattern_valid(const unsigned char *rgb,
                                  unsigned width,
                                  unsigned height)
{
    uint64_t sums[4][3] = {{0}};
    uint64_t counts[4] = {0};
    for (unsigned band = 0; band < 4u; band++) {
        unsigned x_start = band * width / 4u + width / 32u;
        unsigned x_end = (band + 1u) * width / 4u - width / 32u;
        for (unsigned y = height / 8u; y < height * 7u / 8u; y++) {
            for (unsigned x = x_start; x < x_end; x++) {
                size_t offset = ((size_t)y * width + x) * 3u;
                for (unsigned channel = 0; channel < 3u; channel++) {
                    sums[band][channel] += rgb[offset + channel];
                }
                counts[band]++;
            }
        }
    }
    unsigned mean[4][3] = {{0}};
    for (unsigned band = 0; band < 4u; band++) {
        if (!counts[band]) return false;
        for (unsigned channel = 0; channel < 3u; channel++) {
            mean[band][channel] = (unsigned)(sums[band][channel] / counts[band]);
        }
    }
    return mean[0][0] > mean[0][1] + 80u && mean[0][0] > mean[0][2] + 80u &&
           mean[1][1] > mean[1][0] + 80u && mean[1][1] > mean[1][2] + 80u &&
           mean[2][2] > mean[2][0] + 80u && mean[2][2] > mean[2][1] + 80u &&
           mean[3][0] > 160u && mean[3][1] > 160u && mean[3][2] > 160u;
}

static int run_round_trip(unsigned width, unsigned height)
{
    unsigned char *rgb = malloc((size_t)width * height * 3u);
    unsigned char *decoded = malloc((size_t)width * height * 3u);
    unsigned char *config = malloc(65536);
    unsigned char *frame = malloc(2u * 1024u * 1024u);
    if (!rgb || !decoded || !config || !frame) return 1;
    unsigned config_size = 0;
    char error[256] = "";
    IntegralH264Encoder *encoder = integral_h264_encoder_create((uint16_t)width,
                                                                 (uint16_t)height,
                                                                 error, sizeof(error));
    if (!encoder) { fprintf(stderr, "encoder create: %s\n", error); return 1; }
    IntegralH264Decoder *decoder = NULL;
    int decoded_frames = 0;
    int verified_frames = 0;
    bool saw_keyframe = false;
    fill_gb_test_pattern(rgb, width, height);
    for (unsigned index = 0; index < 90; index++) {
        unsigned new_config_size = 0, frame_size = 0;
        bool keyframe = false;
        int result = integral_h264_encoder_encode_rgb24(encoder, rgb, false,
                                                    (uint64_t)index * 33333u,
                                                    config, 65536, &new_config_size,
                                                    frame, 2u * 1024u * 1024u, &frame_size,
                                                    &keyframe, error, sizeof(error));
        saw_keyframe = saw_keyframe || keyframe;
        if (result < 0) { fprintf(stderr, "encode: %s\n", error); return 1; }
        if (new_config_size) {
            config_size = new_config_size;
            decoder = integral_h264_decoder_create(config, config_size, error, sizeof(error));
            if (!decoder) { fprintf(stderr, "decoder create: %s\n", error); return 1; }
        }
        if (decoder && frame_size) {
            uint16_t decoded_width = 0, decoded_height = 0;
            int decoded_result = integral_h264_decoder_decode_avcc(decoder, frame, frame_size,
                                                               (uint64_t)index * 33333u,
                                                               decoded, (size_t)width * height * 3u,
                                                               &decoded_width, &decoded_height,
                                                               error, sizeof(error));
            if (decoded_result < 0) { fprintf(stderr, "decode: %s\n", error); return 1; }
            if (decoded_result > 0) {
                if (decoded_width != width || decoded_height != height) return 1;
                decoded_frames++;
                if (gb_test_pattern_valid(decoded, decoded_width, decoded_height)) verified_frames++;
            }
        }
    }
    integral_h264_decoder_destroy(decoder);
    integral_h264_encoder_destroy(encoder);
    if (!config_size || !decoded_frames || !verified_frames || !saw_keyframe) {
        fprintf(stderr,
                "H264 pipeline insufficient config=%u decoded=%d verified=%d keyframe=%d\n",
                config_size,
                decoded_frames,
                verified_frames,
                saw_keyframe ? 1 : 0);
        free(rgb); free(decoded); free(config); free(frame);
        return 1;
    }
    printf("Platform H264 round trip %ux%u: OK config=%u decoded=%d verified=%d\n",
           width, height, config_size, decoded_frames, verified_frames);
    free(rgb); free(decoded); free(config); free(frame);
    return 0;
}

int main(void)
{
#ifdef __linux__
    if (run_round_trip(160u, 144u) != 0) return 1;
    return run_round_trip(640u, 480u);
#elif defined(__APPLE__)
    return run_round_trip(640u, 480u);
#else
    return run_round_trip(160u, 144u);
#endif
}

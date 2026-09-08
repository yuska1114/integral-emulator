/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_h264.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
#if !defined(_WIN32) && !defined(__APPLE__)
    puts("H264 platform codec test skipped outside Windows and macOS.");
    return 0;
#else
#ifdef __APPLE__
    enum { WIDTH = 640, HEIGHT = 480 };
#else
    enum { WIDTH = 64, HEIGHT = 64 };
#endif
    unsigned char *rgb = malloc(WIDTH * HEIGHT * 3u);
    unsigned char *decoded = malloc(WIDTH * HEIGHT * 3u);
    unsigned char *config = malloc(65536);
    unsigned char *frame = malloc(2u * 1024u * 1024u);
    if (!rgb || !decoded || !config || !frame) return 1;
    unsigned config_size = 0;
    char error[256] = "";
    IntegralH264Encoder *encoder = integral_h264_encoder_create(WIDTH, HEIGHT, error, sizeof(error));
    if (!encoder) { fprintf(stderr, "encoder create: %s\n", error); return 1; }
    IntegralH264Decoder *decoder = NULL;
    int decoded_frames = 0;
    bool saw_keyframe = false;
    for (unsigned index = 0; index < 90; index++) {
        for (unsigned y = 0; y < HEIGHT; y++) for (unsigned x = 0; x < WIDTH; x++) {
            size_t offset = ((size_t)y * WIDTH + x) * 3u;
            rgb[offset] = (unsigned char)(x * 4u + index);
            rgb[offset + 1] = (unsigned char)(y * 4u);
            rgb[offset + 2] = (unsigned char)(index * 3u);
        }
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
            uint16_t width = 0, height = 0;
            int decoded_result = integral_h264_decoder_decode_avcc(decoder, frame, frame_size,
                                                               (uint64_t)index * 33333u,
                                                               decoded, WIDTH * HEIGHT * 3u,
                                                               &width, &height,
                                                               error, sizeof(error));
            if (decoded_result < 0) { fprintf(stderr, "decode: %s\n", error); return 1; }
            if (decoded_result > 0) {
                if (width != WIDTH || height != HEIGHT) return 1;
                decoded_frames++;
            }
        }
    }
    integral_h264_decoder_destroy(decoder);
    integral_h264_encoder_destroy(encoder);
    if (!config_size || !decoded_frames || !saw_keyframe) {
        fprintf(stderr,
                "H264 pipeline insufficient config=%u decoded=%d keyframe=%d\n",
                config_size,
                decoded_frames,
                saw_keyframe ? 1 : 0);
        free(rgb); free(decoded); free(config); free(frame);
        return 1;
    }
    printf("Platform H264 round trip: OK config=%u decoded=%d\n",
           config_size, decoded_frames);
    free(rgb); free(decoded); free(config); free(frame);
    return 0;
#endif
}

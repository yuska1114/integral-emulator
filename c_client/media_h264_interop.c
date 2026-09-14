/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_h264.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    N64_WIDTH = 640,
    N64_HEIGHT = 480,
    GB_WIDTH = 160,
    GB_HEIGHT = 144,
    FRAME_COUNT = 90,
};

static int write_u32(FILE *file, uint32_t value)
{
    uint8_t bytes[4] = {
        (uint8_t)(value >> 24),
        (uint8_t)(value >> 16),
        (uint8_t)(value >> 8),
        (uint8_t)value,
    };
    return fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes) ? 0 : -1;
}

static int read_u32(FILE *file, uint32_t *value)
{
    uint8_t bytes[4];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) return -1;
    *value = (uint32_t)bytes[0] << 24 | (uint32_t)bytes[1] << 16 |
             (uint32_t)bytes[2] << 8 | bytes[3];
    return 0;
}

static void fill_frame(uint8_t *rgb,
                       unsigned width,
                       unsigned height,
                       unsigned index)
{
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            size_t offset = ((size_t)y * width + x) * 3u;
            rgb[offset] = (uint8_t)(x + index);
            rgb[offset + 1u] = (uint8_t)(y + index * 2u);
            rgb[offset + 2u] = (uint8_t)(x / 2u + y / 2u + index * 3u);
        }
    }
}

static int write_stream(const char *path, unsigned width, unsigned height)
{
    uint8_t *rgb = malloc((size_t)width * height * 3u);
    uint8_t *config = malloc(65536u);
    uint8_t *frame = malloc(2u * 1024u * 1024u);
    FILE *file = NULL;
    char error[256] = "";
    IntegralH264Encoder *encoder = NULL;
    bool header_written = false;
    unsigned frames_written = 0;
    int exit_code = 1;
    if (!rgb || !config || !frame) goto done;
    file = fopen(path, "wb");
    if (!file) goto done;
    encoder = integral_h264_encoder_create((uint16_t)width, (uint16_t)height,
                                            error, sizeof(error));
    if (!encoder) {
        fprintf(stderr, "encoder create: %s\n", error);
        goto done;
    }
    for (unsigned index = 0; index < FRAME_COUNT; index++) {
        uint32_t config_size = 0, frame_size = 0;
        bool keyframe = false;
        fill_frame(rgb, width, height, index);
        int result = integral_h264_encoder_encode_rgb24(encoder,
                                                    rgb,
                                                    false,
                                                    (uint64_t)index * 33333u,
                                                    config,
                                                    65536u,
                                                    &config_size,
                                                    frame,
                                                    2u * 1024u * 1024u,
                                                    &frame_size,
                                                    &keyframe,
                                                    error,
                                                    sizeof(error));
        (void)keyframe;
        if (result < 0) {
            fprintf(stderr, "encode: %s\n", error);
            goto done;
        }
        if (!header_written && config_size) {
            if (fwrite("GSC26401", 1, 8, file) != 8 || write_u32(file, config_size) ||
                fwrite(config, 1, config_size, file) != config_size) goto done;
            header_written = true;
        }
        if (header_written && frame_size) {
            if (write_u32(file, frame_size) ||
                fwrite(frame, 1, frame_size, file) != frame_size) goto done;
            frames_written++;
        }
    }
    if (!header_written || !frames_written || write_u32(file, 0)) goto done;
    printf("H264 interop stream written: frames=%u path=%s\n", frames_written, path);
    exit_code = 0;
done:
    integral_h264_encoder_destroy(encoder);
    if (file && fclose(file) != 0) exit_code = 1;
    free(rgb);
    free(config);
    free(frame);
    return exit_code;
}

static int read_stream(const char *path,
                       unsigned expected_width,
                       unsigned expected_height)
{
    uint8_t magic[8];
    uint32_t config_size = 0;
    uint8_t *config = NULL;
    uint8_t *frame = NULL;
    uint8_t *rgb = malloc((size_t)1920 * 1080 * 3u);
    FILE *file = fopen(path, "rb");
    IntegralH264Decoder *decoder = NULL;
    char error[256] = "";
    unsigned input_frames = 0, decoded_frames = 0;
    int exit_code = 1;
    if (!file || !rgb || fread(magic, 1, sizeof(magic), file) != sizeof(magic) ||
        memcmp(magic, "GSC26401", sizeof(magic)) != 0 || read_u32(file, &config_size) ||
        !config_size || config_size > 65536u) goto done;
    config = malloc(config_size);
    frame = malloc(2u * 1024u * 1024u);
    if (!config || !frame || fread(config, 1, config_size, file) != config_size) goto done;
    decoder = integral_h264_decoder_create(config, config_size, error, sizeof(error));
    if (!decoder) {
        fprintf(stderr, "decoder create: %s\n", error);
        goto done;
    }
    for (;;) {
        uint32_t frame_size = 0;
        if (read_u32(file, &frame_size)) goto done;
        if (!frame_size) break;
        if (frame_size > 2u * 1024u * 1024u ||
            fread(frame, 1, frame_size, file) != frame_size) goto done;
        uint16_t width = 0, height = 0;
        int result = integral_h264_decoder_decode_avcc(decoder,
                                                   frame,
                                                   frame_size,
                                                   (uint64_t)input_frames * 33333u,
                                                   rgb,
                                                   (size_t)1920 * 1080 * 3u,
                                                   &width,
                                                   &height,
                                                   error,
                                                   sizeof(error));
        if (result < 0) {
            fprintf(stderr, "decode frame %u: %s\n", input_frames, error);
            goto done;
        }
        if (result > 0) {
            if (width != expected_width || height != expected_height) {
                fprintf(stderr,
                        "H264 interop dimensions mismatch: expected=%ux%u actual=%ux%u\n",
                        expected_width,
                        expected_height,
                        width,
                        height);
                goto done;
            }
            decoded_frames++;
        }
        input_frames++;
    }
    if (!input_frames || !decoded_frames) {
        fprintf(stderr, "H264 interop decode produced no picture: inputs=%u decoded=%u\n",
                input_frames,
                decoded_frames);
        goto done;
    }
    printf("H264 interop stream decoded: inputs=%u decoded=%u path=%s\n",
           input_frames,
           decoded_frames,
           path);
    exit_code = 0;
done:
    integral_h264_decoder_destroy(decoder);
    if (file) fclose(file);
    free(rgb);
    free(config);
    free(frame);
    return exit_code;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr,
                "usage: %s --write|--read|--write-gb|--read-gb PATH\n",
                argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "--write") == 0)
        return write_stream(argv[2], N64_WIDTH, N64_HEIGHT);
    if (strcmp(argv[1], "--read") == 0)
        return read_stream(argv[2], N64_WIDTH, N64_HEIGHT);
    if (strcmp(argv[1], "--write-gb") == 0)
        return write_stream(argv[2], GB_WIDTH, GB_HEIGHT);
    if (strcmp(argv[1], "--read-gb") == 0)
        return read_stream(argv[2], GB_WIDTH, GB_HEIGHT);
    return 2;
}

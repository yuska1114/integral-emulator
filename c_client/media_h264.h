/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_MEDIA_H264_H
#define INTEGRAL_MEDIA_H264_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct IntegralH264Encoder IntegralH264Encoder;
typedef struct IntegralH264Decoder IntegralH264Decoder;

IntegralH264Encoder *integral_h264_encoder_create(uint16_t width,
                                        uint16_t height,
                                        char *error_out,
                                        size_t error_out_size);
void integral_h264_encoder_destroy(IntegralH264Encoder *encoder);

/* RGB is packed, bottom-up when bottom_up is true. Output is one AVCC access unit. */
int integral_h264_encoder_encode_rgb24(IntegralH264Encoder *encoder,
                                  const uint8_t *rgb,
                                  bool bottom_up,
                                  uint64_t timestamp_us,
                                  uint8_t *config_out,
                                  size_t config_capacity,
                                  uint32_t *config_size_out,
                                  uint8_t *frame_out,
                                  size_t frame_capacity,
                                  uint32_t *frame_size_out,
                                  bool *keyframe_out,
                                  char *error_out,
                                  size_t error_out_size);

IntegralH264Decoder *integral_h264_decoder_create(const uint8_t *config,
                                        size_t config_size,
                                        char *error_out,
                                        size_t error_out_size);
void integral_h264_decoder_destroy(IntegralH264Decoder *decoder);

/* Decodes one AVCC access unit to packed, top-down RGB24. */
int integral_h264_decoder_decode_avcc(IntegralH264Decoder *decoder,
                                 const uint8_t *frame,
                                 size_t frame_size,
                                 uint64_t timestamp_us,
                                 uint8_t *rgb_out,
                                 size_t rgb_capacity,
                                 uint16_t *width_out,
                                 uint16_t *height_out,
                                 char *error_out,
                                 size_t error_out_size);

#endif

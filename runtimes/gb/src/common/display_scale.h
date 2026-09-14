/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_DISPLAY_SCALE_H
#define INTEGRAL_GB_RUNTIME_DISPLAY_SCALE_H

#include <stddef.h>

enum {
    INTEGRAL_DISPLAY_SCALE_AUTO = 0,
    INTEGRAL_DISPLAY_SCALE_MIN = 1,
    INTEGRAL_DISPLAY_SCALE_MAX = 6,
};

int integral_display_scale_parse(const char *text, unsigned *scale_out);
unsigned integral_display_scale_resolve(unsigned requested_scale,
                                        int available_width,
                                        int available_height,
                                        int content_width,
                                        int content_height);
unsigned integral_display_scale_from_environment(const char *name);
void integral_display_scale_format(unsigned scale, char *out, size_t out_size);

#endif

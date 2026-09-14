/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "display_scale.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int integral_display_scale_parse(const char *text, unsigned *scale_out)
{
    if (!text || !scale_out) {
        return -1;
    }
    if (strcmp(text, "auto") == 0 || strcmp(text, "AUTO") == 0) {
        *scale_out = INTEGRAL_DISPLAY_SCALE_AUTO;
        return 0;
    }
    errno = 0;
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < INTEGRAL_DISPLAY_SCALE_MIN || value > INTEGRAL_DISPLAY_SCALE_MAX) {
        return -1;
    }
    *scale_out = (unsigned)value;
    return 0;
}

unsigned integral_display_scale_resolve(unsigned requested_scale,
                                        int available_width,
                                        int available_height,
                                        int content_width,
                                        int content_height)
{
    if (requested_scale >= INTEGRAL_DISPLAY_SCALE_MIN &&
        requested_scale <= INTEGRAL_DISPLAY_SCALE_MAX) {
        return requested_scale;
    }
    if (available_width <= 0 || available_height <= 0 ||
        content_width <= 0 || content_height <= 0) {
        return INTEGRAL_DISPLAY_SCALE_MIN;
    }
    int width_scale = available_width / content_width;
    int height_scale = available_height / content_height;
    int scale = width_scale < height_scale ? width_scale : height_scale;
    if (scale < INTEGRAL_DISPLAY_SCALE_MIN) {
        scale = INTEGRAL_DISPLAY_SCALE_MIN;
    }
    if (scale > INTEGRAL_DISPLAY_SCALE_MAX) {
        scale = INTEGRAL_DISPLAY_SCALE_MAX;
    }
    return (unsigned)scale;
}

unsigned integral_display_scale_from_environment(const char *name)
{
    unsigned scale = INTEGRAL_DISPLAY_SCALE_AUTO;
    const char *value = name ? getenv(name) : NULL;
    if (value && integral_display_scale_parse(value, &scale) == 0) {
        return scale;
    }
    return INTEGRAL_DISPLAY_SCALE_AUTO;
}

void integral_display_scale_format(unsigned scale, char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return;
    }
    if (scale == INTEGRAL_DISPLAY_SCALE_AUTO) {
        snprintf(out, out_size, "AUTO");
    }
    else {
        snprintf(out, out_size, "%u", scale);
    }
}

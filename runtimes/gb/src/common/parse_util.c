/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "parse_util.h"

#include <stdlib.h>

int integral_gb_runtime_parse_uint_range(const char *text,
                               unsigned min_value,
                               unsigned max_value,
                               unsigned *out)
{
    char *end = NULL;
    unsigned long value = strtoul(text, &end, 10);
    if (*text == '\0' || *end != '\0' ||
        value < (unsigned long)min_value ||
        value > (unsigned long)max_value) {
        return -1;
    }
    *out = (unsigned)value;
    return 0;
}

int integral_gb_runtime_parse_int_range(const char *text,
                              int min_value,
                              int max_value,
                              int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' ||
        value < (long)min_value ||
        value > (long)max_value) {
        return -1;
    }
    *out = (int)value;
    return 0;
}

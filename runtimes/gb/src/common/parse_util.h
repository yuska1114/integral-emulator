/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_COMMON_PARSE_UTIL_H
#define INTEGRAL_GB_RUNTIME_COMMON_PARSE_UTIL_H

int integral_gb_runtime_parse_uint_range(const char *text,
                               unsigned min_value,
                               unsigned max_value,
                               unsigned *out);
int integral_gb_runtime_parse_int_range(const char *text,
                              int min_value,
                              int max_value,
                              int *out);

#endif

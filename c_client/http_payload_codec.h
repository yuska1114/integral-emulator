/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_HTTP_PAYLOAD_CODEC_H
#define INTEGRAL_HTTP_PAYLOAD_CODEC_H
#include <stddef.h>
#include <stdbool.h>

/* Limited existing JSON helpers, not a validating JSON parser.
 * String destination capacities must be >= 1. Truncation is preserved. */
void json_escape(const char *src, char *dest, size_t dest_size);
int extract_json_string_value(const char *json, const char *key, char *out, size_t out_size, bool allow_empty);
int extract_json_string(const char *json, const char *key, char *out, size_t out_size);
int extract_json_bool(const char *json, const char *key, int *out);
const char *skip_json_spaces(const char *p);
const char *copy_json_string_token(const char *p, char *out, size_t out_size);
int extract_json_int(const char *json, const char *key, int *out);
int extract_json_int64(const char *json, const char *key, long long *out);
const char *json_matching_end(const char *start, char open, char close);

/* Internal codec. encode requires out_size >= 1 and keeps existing truncation.
 * decode may write a prefix on failure; out_size is assigned only on success. */
void base64_encode(const unsigned char *data, size_t data_size, char *out, size_t out_size);
int base64_decode(const char *encoded, unsigned char *out, size_t out_capacity, size_t *out_size);
#endif

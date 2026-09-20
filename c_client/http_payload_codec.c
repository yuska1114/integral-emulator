/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_payload_codec.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

void json_escape(const char *src, char *dest, size_t dest_size)
{
    size_t used = 0;
    for (const char *p = src; *p && used + 1 < dest_size; p++) {
        if ((*p == '"' || *p == '\\') && used + 2 < dest_size) {
            dest[used++] = '\\';
            dest[used++] = *p;
        }
        else if ((unsigned char)*p >= 0x20) {
            dest[used++] = *p;
        }
    }
    dest[used] = '\0';
}

int extract_json_string_value(const char *json,
                                     const char *key,
                                     char *out,
                                     size_t out_size,
                                     bool allow_empty)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *search = json;
    while (true) {
        const char *p = strstr(search, pattern);
        if (!p) {
            return -1;
        }
        p = strchr(p + strlen(pattern), ':');
        if (!p) {
            return -1;
        }
        p++;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p != '"') {
            search = p;
            continue;
        }
        p++;

        size_t used = 0;
        while (*p && *p != '"' && used + 1 < out_size) {
            if (*p == '\\' && p[1]) {
                p++;
            }
            out[used++] = *p++;
        }
        out[used] = '\0';
        return used > 0 || allow_empty ? 0 : -1;
    }
}

int extract_json_string(const char *json,
                               const char *key,
                               char *out,
                               size_t out_size)
{
    return extract_json_string_value(json, key, out, out_size, false);
}

int extract_json_bool(const char *json, const char *key, int *out)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (strncmp(p, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

const char *skip_json_spaces(const char *p)
{
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

const char *copy_json_string_token(const char *p, char *out, size_t out_size)
{
    p = skip_json_spaces(p);
    if (*p != '"') {
        return NULL;
    }
    p++;
    size_t used = 0;
    while (*p && *p != '"') {
        char ch = *p++;
        if (ch == '\\' && *p) {
            ch = *p++;
        }
        if (used + 1 < out_size) {
            out[used++] = ch;
        }
    }
    if (*p != '"') {
        return NULL;
    }
    out[used] = '\0';
    return p + 1;
}

int extract_json_int(const char *json, const char *key, int *out)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (sscanf(p, "%d", out) != 1) {
        return -1;
    }
    return 0;
}

int extract_json_int64(const char *json, const char *key, long long *out)
{
    char pattern[96];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) {
        return -1;
    }
    p = strchr(p + strlen(pattern), ':');
    if (!p) {
        return -1;
    }
    p++;
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    if (sscanf(p, "%lld", out) != 1) {
        return -1;
    }
    return 0;
}

const char *json_matching_end(const char *start, char open, char close)
{
    if (!start || *start != open) return NULL;
    unsigned depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (const char *p = start; *p; p++) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (*p == '\\') escaped = true;
            else if (*p == '"') in_string = false;
            continue;
        }
        if (*p == '"') {
            in_string = true;
        }
        else if (*p == open) {
            depth++;
        }
        else if (*p == close && depth > 0 && --depth == 0) {
            return p;
        }
    }
    return NULL;
}

void base64_encode(const unsigned char *data, size_t data_size, char *out, size_t out_size)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t used = 0;
    for (size_t i = 0; i < data_size && used + 4 < out_size; i += 3) {
        unsigned octet_a = data[i];
        unsigned octet_b = i + 1 < data_size ? data[i + 1] : 0;
        unsigned octet_c = i + 2 < data_size ? data[i + 2] : 0;
        unsigned triple = (octet_a << 16) | (octet_b << 8) | octet_c;
        out[used++] = alphabet[(triple >> 18) & 0x3F];
        out[used++] = alphabet[(triple >> 12) & 0x3F];
        out[used++] = i + 1 < data_size ? alphabet[(triple >> 6) & 0x3F] : '=';
        out[used++] = i + 2 < data_size ? alphabet[triple & 0x3F] : '=';
    }
    out[used] = '\0';
}

static int base64_value(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 26;
    }
    if (ch >= '0' && ch <= '9') {
        return ch - '0' + 52;
    }
    if (ch == '+') {
        return 62;
    }
    if (ch == '/') {
        return 63;
    }
    if (ch == '=') {
        return -2;
    }
    return -1;
}

int base64_decode(const char *encoded, unsigned char *out, size_t out_capacity, size_t *out_size)
{
    size_t used = 0;
    int values[4];
    while (*encoded) {
        for (unsigned i = 0; i < 4; i++) {
            while (*encoded && isspace((unsigned char)*encoded)) {
                encoded++;
            }
            if (!*encoded) {
                return -1;
            }
            values[i] = base64_value(*encoded++);
            if (values[i] == -1) {
                return -1;
            }
        }

        if (values[0] < 0 || values[1] < 0) {
            return -1;
        }
        unsigned triple = ((unsigned)values[0] << 18) | ((unsigned)values[1] << 12);
        if (values[2] >= 0) {
            triple |= (unsigned)values[2] << 6;
        }
        if (values[3] >= 0) {
            triple |= (unsigned)values[3];
        }

        if (used >= out_capacity) {
            return -1;
        }
        out[used++] = (unsigned char)((triple >> 16) & 0xff);
        if (values[2] != -2) {
            if (used >= out_capacity) {
                return -1;
            }
            out[used++] = (unsigned char)((triple >> 8) & 0xff);
        }
        if (values[3] != -2) {
            if (used >= out_capacity) {
                return -1;
            }
            out[used++] = (unsigned char)(triple & 0xff);
        }
    }
    *out_size = used;
    return 0;
}

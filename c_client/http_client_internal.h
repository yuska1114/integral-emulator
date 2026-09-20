/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_HTTP_CLIENT_INTERNAL_H
#define INTEGRAL_HTTP_CLIENT_INTERNAL_H
/* Private API-to-HTTP interface; no UI, sockets or TLS types. */
#include <stddef.h>
#include <stdbool.h>
#define INTEGRAL_HTTP_HEADER_MAX (16u * 1024u)

void http_copy_text(char *dest, size_t dest_size, const char *src);
void http_set_error(char *error_out, size_t error_out_size, const char *message);
int api_json_request(const char *method, const char *server_url, const char *path_suffix, const char *token, const char *body, char *response_out, size_t response_out_size, char *error_out, size_t error_out_size);
int api_post_json(const char *server_url, const char *path_suffix, const char *token, const char *body, char *response_out, size_t response_out_size, char *error_out, size_t error_out_size);
int api_get_json(const char *server_url, const char *path_suffix, const char *token, char *response_out, size_t response_out_size, char *error_out, size_t error_out_size);
int api_put_json(const char *server_url, const char *path_suffix, const char *token, const char *body, char *response_out, size_t response_out_size, char *error_out, size_t error_out_size);
void http_secure_wipe(void *data, size_t size);
bool http_safe_path_token(const char *value);
const char *http_body_start(const char *response, size_t response_size, size_t *body_size);
int api_control_json_body_request( const char *method, const char *server_url, const char *path, const char *token, const char *body, unsigned char *json_out, size_t json_out_capacity, size_t *json_out_size, char *error_out, size_t error_out_size);
#endif

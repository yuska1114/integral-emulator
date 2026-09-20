/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "http_client_internal.h"
#include "http_payload_codec.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "http_transport.h"

void http_copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

void http_set_error(char *error_out, size_t error_out_size, const char *message)
{
    http_copy_text(error_out, error_out_size, message);
}

static int http_status_code(const char *response)
{
    int code = 0;
    if (sscanf(response, "HTTP/%*s %d", &code) != 1) {
        return 0;
    }
    return code;
}

int api_json_request(const char *method,
                            const char *server_url,
                            const char *path_suffix,
                            const char *token,
                            const char *body,
                            char *response_out,
                            size_t response_out_size,
                            char *error_out,
                            size_t error_out_size)
{
    error_out[0] = '\0';

    ParsedUrl parsed;
    if (parse_http_url(server_url, &parsed, error_out, error_out_size) != 0) {
        return -1;
    }

    char path[256];
    if (parsed.base_path[0] == '\0' || strcmp(parsed.base_path, "/") == 0) {
        http_copy_text(path, sizeof(path), path_suffix);
    }
    else {
        snprintf(path, sizeof(path), "%s%s", parsed.base_path, path_suffix);
    }

    char auth_header[256] = "";
    if (token && token[0] != '\0') {
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n", token);
    }

    size_t body_len = body ? strlen(body) : 0;
    int header_len = snprintf(NULL,
                              0,
                              "%s %s HTTP/1.1\r\n"
                              "Host: %s:%s\r\n"
                              "Content-Type: application/json\r\n"
                              "%s"
                              "Content-Length: %zu\r\n"
                              "Connection: close\r\n"
                              "\r\n",
                              method,
                              path,
                              parsed.host,
                              parsed.port,
                              auth_header,
                              body_len);
    if (header_len < 0) {
        http_secure_wipe(auth_header, sizeof(auth_header));
        http_set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    size_t request_size = (size_t)header_len + body_len + 1;
    char *request = malloc(request_size);
    if (!request) {
        http_secure_wipe(auth_header, sizeof(auth_header));
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    int written = snprintf(request,
                           request_size,
                           "%s %s HTTP/1.1\r\n"
                           "Host: %s:%s\r\n"
                           "Content-Type: application/json\r\n"
                           "%s"
                           "Content-Length: %zu\r\n"
                           "Connection: close\r\n"
                           "\r\n",
                           method,
                           path,
                           parsed.host,
                           parsed.port,
                           auth_header,
                           body_len);
    if (written < 0 || (size_t)written >= request_size) {
        http_secure_wipe(request, request_size);
        free(request);
        http_secure_wipe(auth_header, sizeof(auth_header));
        http_set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    if (body_len > 0) {
        memcpy(request + written, body, body_len + 1);
    }

    IntegralConnection connection;
    if (connection_open(&parsed, &connection, error_out, error_out_size) != 0) {
        http_secure_wipe(request, request_size);
        free(request);
        http_secure_wipe(auth_header, sizeof(auth_header));
        return -1;
    }
    if (connection_send_all(&connection, request, (size_t)written + body_len) != 0) {
        connection_close(&connection);
        http_secure_wipe(request, request_size);
        free(request);
        http_secure_wipe(auth_header, sizeof(auth_header));
        http_set_error(error_out, error_out_size, "FAILED TO SEND LOGIN REQUEST");
        return -1;
    }
    http_secure_wipe(request, request_size);
    free(request);
    http_secure_wipe(auth_header, sizeof(auth_header));

    if (connection_read_response(&connection, response_out, response_out_size) != 0) {
        connection_close(&connection);
        http_set_error(error_out, error_out_size, "FAILED TO READ RESPONSE");
        return -1;
    }
    connection_close(&connection);

    int status = http_status_code(response_out);
    if (status != 200) {
        char message[160];
        if (extract_json_string(response_out, "message", message, sizeof(message)) == 0) {
            http_set_error(error_out, error_out_size, message);
        }
        else {
            snprintf(error_out, error_out_size, "HTTP %d", status);
        }
        return status >= 400 && status <= 599 ? status : -1;
    }
    return 0;
}

int api_post_json(const char *server_url,
                         const char *path_suffix,
                         const char *token,
                         const char *body,
                         char *response_out,
                         size_t response_out_size,
                         char *error_out,
                         size_t error_out_size)
{
    return api_json_request("POST",
                            server_url,
                            path_suffix,
                            token,
                            body,
                            response_out,
                            response_out_size,
                            error_out,
                            error_out_size);
}

int api_get_json(const char *server_url,
                        const char *path_suffix,
                        const char *token,
                        char *response_out,
                        size_t response_out_size,
                        char *error_out,
                        size_t error_out_size)
{
    return api_json_request("GET",
                            server_url,
                            path_suffix,
                            token,
                            "",
                            response_out,
                            response_out_size,
                            error_out,
                            error_out_size);
}

int api_put_json(const char *server_url,
                        const char *path_suffix,
                        const char *token,
                        const char *body,
                        char *response_out,
                        size_t response_out_size,
                        char *error_out,
                        size_t error_out_size)
{
    return api_json_request("PUT",
                            server_url,
                            path_suffix,
                            token,
                            body,
                            response_out,
                            response_out_size,
                            error_out,
                            error_out_size);
}

void http_secure_wipe(void *data, size_t size)
{
    volatile unsigned char *bytes = data;
    while (bytes && size != 0u) {
        *bytes++ = 0u;
        size--;
    }
}

bool http_safe_path_token(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    if (cursor == NULL || *cursor == '\0') return false;
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') return false;
        cursor++;
    }
    return true;
}

const char *http_body_start(const char *response, size_t response_size,
                                   size_t *body_size)
{
    size_t index;
    if (body_size) *body_size = 0u;
    if (response == NULL || response_size < 4u) return NULL;
    for (index = 0u; index + 4u <= response_size; index++) {
        if (memcmp(response + index, "\r\n\r\n", 4u) == 0) {
            if (body_size) *body_size = response_size - index - 4u;
            return response + index + 4u;
        }
    }
    return NULL;
}

int api_control_json_body_request(
    const char *method,
    const char *server_url,
    const char *path,
    const char *token,
    const char *body,
    unsigned char *json_out,
    size_t json_out_capacity,
    size_t *json_out_size,
    char *error_out,
    size_t error_out_size)
{
    char response[INTEGRAL_API_CONTROL_JSON_MAX + INTEGRAL_HTTP_HEADER_MAX + 1u];
    const char *body_start;
    size_t body_size;
    int result;
    if (json_out_size) *json_out_size = 0u;
    if (json_out && json_out_capacity) memset(json_out, 0, json_out_capacity);
    if (json_out == NULL || json_out_size == NULL ||
        json_out_capacity == 0u || json_out_capacity > INTEGRAL_API_CONTROL_JSON_MAX) {
        http_set_error(error_out, error_out_size, "CONTROL JSON OUTPUT INVALID");
        return -1;
    }
    result = api_json_request(method, server_url, path, token, body,
                              response, sizeof(response),
                              error_out, error_out_size);
    if (result != 0) {
        http_secure_wipe(response, sizeof(response));
        return -1;
    }
    body_start = http_body_start(response, strlen(response), &body_size);
    if (body_start == NULL || body_size == 0u ||
        body_size >= json_out_capacity || body_size > INTEGRAL_API_CONTROL_JSON_MAX) {
        http_secure_wipe(response, sizeof(response));
        http_set_error(error_out, error_out_size, "CONTROL JSON RESPONSE INVALID");
        return -1;
    }
    memcpy(json_out, body_start, body_size);
    json_out[body_size] = '\0';
    *json_out_size = body_size;
    http_secure_wipe(response, sizeof(response));
    return 0;
}

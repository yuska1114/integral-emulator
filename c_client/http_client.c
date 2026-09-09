/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef INTEGRAL_USE_OPENSSL
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#define INTEGRAL_MAX_SAVE_BYTES (128u * 1024u)
#define INTEGRAL_MAX_ROM_BYTES (16u * 1024u * 1024u)
#define INTEGRAL_SAVE_RESPONSE_MAX (220u * 1024u)
#define INTEGRAL_MOBILE_RESPONSE_MAX (45u * 1024u * 1024u)
#define INTEGRAL_HTTP_TIMEOUT_SECONDS 15
#define INTEGRAL_HTTP_HEADER_MAX (16u * 1024u)

typedef struct ParsedUrl {
    char host[128];
    char port[16];
    char base_path[128];
    bool use_tls;
} ParsedUrl;

typedef struct IntegralConnection {
#ifdef _WIN32
    SOCKET sock;
#else
    int sock;
#endif
#ifdef INTEGRAL_USE_OPENSSL
    SSL_CTX *ctx;
    SSL *ssl;
#endif
} IntegralConnection;

#ifdef _WIN32
static int winsock_ready(void)
{
    static bool initialized = false;
    if (initialized) {
        return 0;
    }
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return -1;
    }
    initialized = true;
    return 0;
}

static const char *socket_error_text(void)
{
    static char buffer[64];
    snprintf(buffer, sizeof(buffer), "SOCKET ERROR %d", WSAGetLastError());
    return buffer;
}

static void socket_close(SOCKET sock)
{
    closesocket(sock);
}
#else
static int winsock_ready(void)
{
    return 0;
}

static const char *socket_error_text(void)
{
    return strerror(errno);
}

static void socket_close(int sock)
{
    close(sock);
}
#endif

static void configure_socket_timeouts(int sock)
{
#ifdef _WIN32
    DWORD timeout_ms = INTEGRAL_HTTP_TIMEOUT_SECONDS * 1000;
    setsockopt((SOCKET)sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt((SOCKET)sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval timeout;
    timeout.tv_sec = INTEGRAL_HTTP_TIMEOUT_SECONDS;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

static void copy_text(char *dest, size_t dest_size, const char *src)
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

static void set_error(char *error_out, size_t error_out_size, const char *message)
{
    copy_text(error_out, error_out_size, message);
}

static void base64_encode(const unsigned char *data, size_t data_size, char *out, size_t out_size);
static int base64_decode(const char *encoded, unsigned char *out, size_t out_capacity, size_t *out_size);

static int parse_http_url(const char *url, ParsedUrl *parsed, char *error_out, size_t error_out_size)
{
    memset(parsed, 0, sizeof(*parsed));
    const char http_prefix[] = "http://";
    const char https_prefix[] = "https://";
    const char *cursor = NULL;
    if (strncmp(url, http_prefix, sizeof(http_prefix) - 1) == 0) {
        cursor = url + sizeof(http_prefix) - 1;
        parsed->use_tls = false;
        copy_text(parsed->port, sizeof(parsed->port), "80");
    }
    else if (strncmp(url, https_prefix, sizeof(https_prefix) - 1) == 0) {
#ifndef INTEGRAL_USE_OPENSSL
        set_error(error_out, error_out_size, "HTTPS REQUIRES OPENSSL BUILD");
        return -1;
#else
        cursor = url + sizeof(https_prefix) - 1;
        parsed->use_tls = true;
        copy_text(parsed->port, sizeof(parsed->port), "443");
#endif
    }
    else {
        set_error(error_out, error_out_size, "ONLY HTTP/HTTPS URLS ARE SUPPORTED");
        return -1;
    }

    const char *host_start = cursor;
    while (*cursor && *cursor != ':' && *cursor != '/') {
        cursor++;
    }
    size_t host_len = (size_t)(cursor - host_start);
    if (host_len == 0 || host_len >= sizeof(parsed->host)) {
        set_error(error_out, error_out_size, "INVALID SERVER HOST");
        return -1;
    }
    memcpy(parsed->host, host_start, host_len);
    parsed->host[host_len] = '\0';

    if (*cursor == ':') {
        cursor++;
        const char *port_start = cursor;
        while (*cursor && *cursor != '/') {
            if (!isdigit((unsigned char)*cursor)) {
                set_error(error_out, error_out_size, "INVALID SERVER PORT");
                return -1;
            }
            cursor++;
        }
        size_t port_len = (size_t)(cursor - port_start);
        if (port_len == 0 || port_len >= sizeof(parsed->port)) {
            set_error(error_out, error_out_size, "INVALID SERVER PORT");
            return -1;
        }
        memcpy(parsed->port, port_start, port_len);
        parsed->port[port_len] = '\0';
    }

    if (*cursor == '/') {
        copy_text(parsed->base_path, sizeof(parsed->base_path), cursor);
    }
    else {
        copy_text(parsed->base_path, sizeof(parsed->base_path), "");
    }
    return 0;
}

static int connect_tcp(const char *host, const char *port, char *error_out, size_t error_out_size)
{
    if (winsock_ready() != 0) {
        set_error(error_out, error_out_size, "WINSOCK STARTUP FAILED");
        return -1;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *results = NULL;
    int gai = getaddrinfo(host, port, &hints, &results);
    if (gai != 0) {
        set_error(error_out, error_out_size, gai_strerror(gai));
        return -1;
    }

#ifdef _WIN32
    SOCKET sock = INVALID_SOCKET;
#else
    int sock = -1;
#endif
    for (struct addrinfo *item = results; item; item = item->ai_next) {
        sock = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (
#ifdef _WIN32
            sock == INVALID_SOCKET
#else
            sock < 0
#endif
        ) {
            continue;
        }
        if (connect(sock, item->ai_addr, item->ai_addrlen) == 0) {
            break;
        }
        socket_close(sock);
#ifdef _WIN32
        sock = INVALID_SOCKET;
#else
        sock = -1;
#endif
    }
    freeaddrinfo(results);

    if (
#ifdef _WIN32
        sock == INVALID_SOCKET
#else
        sock < 0
#endif
    ) {
        set_error(error_out, error_out_size, socket_error_text());
        return -1;
    }
    configure_socket_timeouts((int)sock);
#ifdef _WIN32
    return (int)sock;
#else
    return sock;
#endif
}

static int send_all(int sock, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
#ifdef _WIN32
        int n = send((SOCKET)sock, data + sent, (int)(len - sent), 0);
#else
        ssize_t n = send(sock, data + sent, len - sent, 0);
#endif
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

static int read_response(int sock, char *buffer, size_t buffer_size,
                         size_t *response_size)
{
    size_t used = 0;
    while (used + 1 < buffer_size) {
#ifdef _WIN32
        int n = recv((SOCKET)sock, buffer + used, (int)(buffer_size - used - 1), 0);
#else
        ssize_t n = recv(sock, buffer + used, buffer_size - used - 1, 0);
#endif
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
    }
    if (used + 1u == buffer_size) {
        char extra;
#ifdef _WIN32
        int n = recv((SOCKET)sock, &extra, 1, 0);
#else
        ssize_t n = recv(sock, &extra, 1, 0);
#endif
        if (n != 0) return -1;
    }
    buffer[used] = '\0';
    if (response_size) *response_size = used;
    return 0;
}

static void connection_close(IntegralConnection *connection)
{
#ifdef INTEGRAL_USE_OPENSSL
    if (connection->ssl) {
        SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
        connection->ssl = NULL;
    }
    if (connection->ctx) {
        SSL_CTX_free(connection->ctx);
        connection->ctx = NULL;
    }
#endif
    if (
#ifdef _WIN32
        connection->sock != INVALID_SOCKET
#else
        connection->sock >= 0
#endif
    ) {
        socket_close(connection->sock);
#ifdef _WIN32
        connection->sock = INVALID_SOCKET;
#else
        connection->sock = -1;
#endif
    }
}

static int connection_open(const ParsedUrl *parsed, IntegralConnection *connection, char *error_out, size_t error_out_size)
{
    memset(connection, 0, sizeof(*connection));
#ifdef _WIN32
    connection->sock = INVALID_SOCKET;
#else
    connection->sock = -1;
#endif
    int sock = connect_tcp(parsed->host, parsed->port, error_out, error_out_size);
    if (sock < 0) {
        return -1;
    }
    connection->sock = sock;

    if (!parsed->use_tls) {
        return 0;
    }

#ifdef INTEGRAL_USE_OPENSSL
    SSL_CTX *ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        connection_close(connection);
        set_error(error_out, error_out_size, "TLS CONTEXT FAILED");
        return -1;
    }
    connection->ctx = ctx;
    SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
        connection_close(connection);
        set_error(error_out, error_out_size, "TLS CA PATHS FAILED");
        return -1;
    }

    SSL *ssl = SSL_new(ctx);
    if (!ssl) {
        connection_close(connection);
        set_error(error_out, error_out_size, "TLS SESSION FAILED");
        return -1;
    }
    connection->ssl = ssl;
    SSL_set_fd(ssl, sock);
    SSL_set_tlsext_host_name(ssl, parsed->host);
    X509_VERIFY_PARAM *verify_param = SSL_get0_param(ssl);
    X509_VERIFY_PARAM_set_hostflags(verify_param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
    if (X509_VERIFY_PARAM_set1_host(verify_param, parsed->host, 0) != 1) {
        connection_close(connection);
        set_error(error_out, error_out_size, "TLS HOST VERIFY SETUP FAILED");
        return -1;
    }
    if (SSL_connect(ssl) != 1) {
        connection_close(connection);
        set_error(error_out, error_out_size, "TLS HANDSHAKE FAILED");
        return -1;
    }
    if (SSL_get_verify_result(ssl) != X509_V_OK) {
        connection_close(connection);
        set_error(error_out, error_out_size, "TLS CERTIFICATE VERIFY FAILED");
        return -1;
    }
    return 0;
#else
    connection_close(connection);
    set_error(error_out, error_out_size, "HTTPS REQUIRES OPENSSL BUILD");
    return -1;
#endif
}

static int connection_send_all(IntegralConnection *connection, const char *data, size_t len)
{
#ifdef INTEGRAL_USE_OPENSSL
    if (!connection->ssl) {
        return send_all(connection->sock, data, len);
    }
    size_t sent = 0;
    while (sent < len) {
        int n = SSL_write(connection->ssl, data + sent, (int)(len - sent));
        if (n <= 0) {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
#else
    return send_all(connection->sock, data, len);
#endif
}

static int connection_read_response_sized(IntegralConnection *connection,
                                          char *buffer,
                                          size_t buffer_size,
                                          size_t *response_size)
{
#ifdef INTEGRAL_USE_OPENSSL
    if (!connection->ssl) {
        return read_response(connection->sock, buffer, buffer_size,
                             response_size);
    }
    size_t used = 0;
    while (used + 1 < buffer_size) {
        int n = SSL_read(connection->ssl, buffer + used, (int)(buffer_size - used - 1));
        if (n < 0) {
            return -1;
        }
        if (n == 0) {
            break;
        }
        used += (size_t)n;
    }
    if (used + 1u == buffer_size) {
        char extra;
        int n = SSL_read(connection->ssl, &extra, 1);
        if (n != 0) return -1;
    }
    buffer[used] = '\0';
    if (response_size) *response_size = used;
    return 0;
#else
    return read_response(connection->sock, buffer, buffer_size, response_size);
#endif
}

static int connection_read_response(IntegralConnection *connection,
                                    char *buffer,
                                    size_t buffer_size)
{
    return connection_read_response_sized(
        connection, buffer, buffer_size, NULL);
}

static void json_escape(const char *src, char *dest, size_t dest_size)
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

static int http_status_code(const char *response)
{
    int code = 0;
    if (sscanf(response, "HTTP/%*s %d", &code) != 1) {
        return 0;
    }
    return code;
}

static int extract_json_string_value(const char *json,
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

static int extract_json_string(const char *json,
                               const char *key,
                               char *out,
                               size_t out_size)
{
    return extract_json_string_value(json, key, out, out_size, false);
}

static int extract_json_bool(const char *json, const char *key, int *out)
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

static const char *skip_json_spaces(const char *p)
{
    while (*p && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *copy_json_string_token(const char *p, char *out, size_t out_size)
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

static int extract_json_int(const char *json, const char *key, int *out)
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

static int extract_json_int64(const char *json, const char *key, long long *out)
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

static void secure_wipe(void *data, size_t size);

static int api_json_request(const char *method,
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
        copy_text(path, sizeof(path), path_suffix);
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
        secure_wipe(auth_header, sizeof(auth_header));
        set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    size_t request_size = (size_t)header_len + body_len + 1;
    char *request = malloc(request_size);
    if (!request) {
        secure_wipe(auth_header, sizeof(auth_header));
        set_error(error_out, error_out_size, "OUT OF MEMORY");
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
        secure_wipe(request, request_size);
        free(request);
        secure_wipe(auth_header, sizeof(auth_header));
        set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    if (body_len > 0) {
        memcpy(request + written, body, body_len + 1);
    }

    IntegralConnection connection;
    if (connection_open(&parsed, &connection, error_out, error_out_size) != 0) {
        secure_wipe(request, request_size);
        free(request);
        secure_wipe(auth_header, sizeof(auth_header));
        return -1;
    }
    if (connection_send_all(&connection, request, (size_t)written + body_len) != 0) {
        connection_close(&connection);
        secure_wipe(request, request_size);
        free(request);
        secure_wipe(auth_header, sizeof(auth_header));
        set_error(error_out, error_out_size, "FAILED TO SEND LOGIN REQUEST");
        return -1;
    }
    secure_wipe(request, request_size);
    free(request);
    secure_wipe(auth_header, sizeof(auth_header));

    if (connection_read_response(&connection, response_out, response_out_size) != 0) {
        connection_close(&connection);
        set_error(error_out, error_out_size, "FAILED TO READ RESPONSE");
        return -1;
    }
    connection_close(&connection);

    int status = http_status_code(response_out);
    if (status != 200) {
        char message[160];
        if (extract_json_string(response_out, "message", message, sizeof(message)) == 0) {
            set_error(error_out, error_out_size, message);
        }
        else {
            snprintf(error_out, error_out_size, "HTTP %d", status);
        }
        return -1;
    }
    return 0;
}

static int api_post_json(const char *server_url,
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

static int api_get_json(const char *server_url,
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

static int api_put_json(const char *server_url,
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

static void secure_wipe(void *data, size_t size)
{
    volatile unsigned char *bytes = data;
    while (bytes && size != 0u) {
        *bytes++ = 0u;
        size--;
    }
}

static bool safe_path_token(const char *value)
{
    const unsigned char *cursor = (const unsigned char *)value;
    if (cursor == NULL || *cursor == '\0') return false;
    while (*cursor != '\0') {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') return false;
        cursor++;
    }
    return true;
}

static const char *http_body_start(const char *response, size_t response_size,
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

static int api_control_json_body_request(
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
        set_error(error_out, error_out_size, "CONTROL JSON OUTPUT INVALID");
        return -1;
    }
    result = api_json_request(method, server_url, path, token, body,
                              response, sizeof(response),
                              error_out, error_out_size);
    if (result != 0) {
        secure_wipe(response, sizeof(response));
        return -1;
    }
    body_start = http_body_start(response, strlen(response), &body_size);
    if (body_start == NULL || body_size == 0u ||
        body_size >= json_out_capacity || body_size > INTEGRAL_API_CONTROL_JSON_MAX) {
        secure_wipe(response, sizeof(response));
        set_error(error_out, error_out_size, "CONTROL JSON RESPONSE INVALID");
        return -1;
    }
    memcpy(json_out, body_start, body_size);
    json_out[body_size] = '\0';
    *json_out_size = body_size;
    secure_wipe(response, sizeof(response));
    return 0;
}

int integral_api_login(const char *server_url,
                  const char *username,
                  const char *password,
                  const char *server_id,
                  char *token_out,
                  size_t token_out_size,
                  char *authenticated_username_out,
                  size_t authenticated_username_out_size,
                  int *must_change_password_out,
                  int *allow_user_initial_save_import_out,
                  char *error_out,
                  size_t error_out_size)
{
    token_out[0] = '\0';
    if (authenticated_username_out && authenticated_username_out_size > 0) {
        authenticated_username_out[0] = '\0';
    }
    *must_change_password_out = 0;
    if (allow_user_initial_save_import_out) {
        *allow_user_initial_save_import_out = 0;
    }
    char escaped_user[160];
    char escaped_password[160];
    char escaped_server_id[64];
    json_escape(username, escaped_user, sizeof(escaped_user));
    json_escape(password, escaped_password, sizeof(escaped_password));
    json_escape(server_id && server_id[0] ? server_id : "primary", escaped_server_id, sizeof(escaped_server_id));

    char body[448];
    snprintf(body,
             sizeof(body),
             "{\"username\":\"%s\",\"password\":\"%s\",\"server_id\":\"%s\"}",
             escaped_user,
             escaped_password,
             escaped_server_id);

    char response[8192];
    if (api_post_json(server_url, "/auth/login", NULL, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "token", token_out, token_out_size) != 0) {
        set_error(error_out, error_out_size, "LOGIN RESPONSE MISSING TOKEN");
        return -1;
    }
    if (!authenticated_username_out || authenticated_username_out_size == 0 ||
        extract_json_string(response, "username", authenticated_username_out,
                            authenticated_username_out_size) != 0) {
        set_error(error_out, error_out_size, "LOGIN RESPONSE MISSING USERNAME");
        token_out[0] = '\0';
        return -1;
    }
    (void)extract_json_bool(response, "must_change_password", must_change_password_out);
    if (allow_user_initial_save_import_out) {
        (void)extract_json_bool(response,
                                "allow_user_initial_save_import",
                                allow_user_initial_save_import_out);
    }
    return 0;
}

int integral_api_change_password(const char *server_url,
                            const char *token,
                            const char *new_password,
                            char *error_out,
                            size_t error_out_size)
{
    char escaped_password[160];
    json_escape(new_password, escaped_password, sizeof(escaped_password));
    char body[224];
    snprintf(body, sizeof(body), "{\"new_password\":\"%s\"}", escaped_password);
    char response[8192];
    if (api_post_json(server_url, "/auth/change-password", token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    return 0;
}

static void clear_room(IntegralApiRoom *room)
{
    memset(room, 0, sizeof(*room));
}

static const char *json_matching_end(const char *start, char open, char close)
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

static int valid_room_code(const char *code)
{
    if (!code || strlen(code) != 5 || code[0] < '1' || code[0] > '9') return 0;
    for (size_t i = 1; i < 5; i++) {
        if (code[i] < '0' || code[i] > '9') return 0;
    }
    return 1;
}

static int parse_room_object(const char *object, IntegralApiRoom *room)
{
    clear_room(room);
    int room_number = 0;
    if (extract_json_int(object, "room_number", &room_number) != 0 ||
        room_number < 1 || room_number > INTEGRAL_API_ROOMS) return -1;
    room->room_number = (unsigned)room_number;
    (void)extract_json_string(object, "room_code", room->room_code, sizeof(room->room_code));
    (void)extract_json_string(object, "room_type", room->room_type, sizeof(room->room_type));
    (void)extract_json_bool(object, "creator", &room->creator);
    (void)extract_json_string(object, "link_session_id", room->link_session_id, sizeof(room->link_session_id));
    (void)extract_json_string(object, "link_mode", room->link_mode, sizeof(room->link_mode));
    (void)extract_json_bool(object, "game_started", &room->game_started);

    if (room->room_code[0] && !valid_room_code(room->room_code)) return -1;
    if (room->room_type[0]) {
        bool link_room = strcmp(room->room_type, "link_cable") == 0 && room_number <= 64;
        bool n64_room = strcmp(room->room_type, "n64") == 0 && room_number >= 65 && room_number <= 128;
        if (!link_room && !n64_room) return -1;
    }

    const char *users_key = strstr(object, "\"users\"");
    const char *users = users_key ? strchr(users_key, '[') : NULL;
    const char *users_end = users ? json_matching_end(users, '[', ']') : NULL;
    const char *p = users ? users + 1 : NULL;
    unsigned user_index = 0;
    while (p && users_end && p < users_end && user_index < 2) {
        const char *user_start = strchr(p, '{');
        const char *user_end = user_start ? json_matching_end(user_start, '{', '}') : NULL;
        if (!user_start || !user_end || user_end > users_end) break;
        size_t user_size = (size_t)(user_end - user_start + 1);
        char *user_json = malloc(user_size + 1);
        if (!user_json) return -1;
        memcpy(user_json, user_start, user_size);
        user_json[user_size] = '\0';
        char *username = user_index == 0 ? room->user1 : room->user2;
        char *slot = user_index == 0 ? room->slot1 : room->slot2;
        char *n64_slot = user_index == 0 ? room->n64_slot1 : room->n64_slot2;
        char *slot_filename = user_index == 0 ? room->slot_filename1 : room->slot_filename2;
        char *slot_game_type = user_index == 0 ? room->slot_game_type1 : room->slot_game_type2;
        char *slot_header_title = user_index == 0 ? room->slot_header_title1 : room->slot_header_title2;
        (void)extract_json_string(user_json, "username", username, INTEGRAL_API_ROOM_USER_MAX);
        (void)extract_json_string(user_json, "slot", slot, INTEGRAL_API_ROOM_SLOT_MAX);
        (void)extract_json_string(user_json, "n64_slot", n64_slot, INTEGRAL_API_ROOM_SLOT_MAX);
        (void)extract_json_string(user_json, "slot_filename", slot_filename, INTEGRAL_API_ROOM_FILENAME_MAX);
        (void)extract_json_string(user_json, "slot_game_type", slot_game_type, INTEGRAL_API_ROOM_GAME_TYPE_MAX);
        (void)extract_json_string(user_json, "slot_rom_header_title", slot_header_title,
                                  INTEGRAL_API_ROM_HEADER_TITLE_MAX);
        int ready = 0;
        (void)extract_json_bool(user_json, "ready", &ready);
        if (user_index == 0) {
            room->ready1 = ready;
            (void)extract_json_string(user_json, "n64_slot_filename", room->n64_slot_filename1,
                                      sizeof(room->n64_slot_filename1));
            (void)extract_json_string(user_json, "n64_slot_game_type", room->n64_slot_game_type1,
                                      sizeof(room->n64_slot_game_type1));
            (void)extract_json_string(user_json, "n64_slot_rom_header_title",
                                      room->n64_slot_header_title1,
                                      sizeof(room->n64_slot_header_title1));
        }
        else room->ready2 = ready;
        free(user_json);
        user_index++;
        p = user_end + 1;
    }

    const char *chat_key = strstr(object, "\"chat\"");
    const char *chat = chat_key ? strchr(chat_key, '[') : NULL;
    const char *chat_end = chat ? json_matching_end(chat, '[', ']') : NULL;
    p = chat ? chat + 1 : NULL;
    while (p && chat_end && p < chat_end && room->chat_count < INTEGRAL_API_ROOM_CHAT_MAX) {
        const char *item_start = strchr(p, '{');
        const char *item_end = item_start ? json_matching_end(item_start, '{', '}') : NULL;
        if (!item_start || !item_end || item_end > chat_end) break;
        size_t item_size = (size_t)(item_end - item_start + 1);
        char *item_json = malloc(item_size + 1);
        if (!item_json) return -1;
        memcpy(item_json, item_start, item_size);
        item_json[item_size] = '\0';
        char username[INTEGRAL_API_ROOM_USER_MAX] = "USER";
        char message[INTEGRAL_API_ROOM_CHAT_TEXT_MAX] = "";
        (void)extract_json_string(item_json, "username", username, sizeof(username));
        if (extract_json_string(item_json, "message", message, sizeof(message)) == 0) {
            snprintf(room->chat[room->chat_count], sizeof(room->chat[room->chat_count]),
                     "%s: %s", username, message);
            room->chat_count++;
        }
        free(item_json);
        p = item_end + 1;
    }
    return 0;
}

int integral_api_parse_room_matching_response(const char *response,
                                         IntegralApiRoom *room_out,
                                         int *has_room_out)
{
    if (!response || !room_out || !has_room_out) return -1;
    clear_room(room_out);
    *has_room_out = 0;
    const char *room_key = strstr(response, "\"room\"");
    if (!room_key) return -1;
    const char *colon = strchr(room_key, ':');
    if (!colon) return -1;
    const char *value = skip_json_spaces(colon + 1);
    if (strncmp(value, "null", 4) == 0) return 0;
    if (*value != '{') return -1;
    const char *end = json_matching_end(value, '{', '}');
    if (!end) return -1;
    size_t size = (size_t)(end - value + 1);
    char *object = malloc(size + 1);
    if (!object) return -1;
    memcpy(object, value, size);
    object[size] = '\0';
    int result = parse_room_object(object, room_out);
    free(object);
    if (result != 0 || !valid_room_code(room_out->room_code) || !room_out->room_type[0]) return -1;
    *has_room_out = 1;
    return 0;
}

int integral_api_create_room(const char *server_url,
                        const char *token,
                        const char *mode,
                        IntegralApiRoom *room_out,
                        char *error_out,
                        size_t error_out_size)
{
    if (!mode || (strcmp(mode, "link_cable") != 0 && strcmp(mode, "n64") != 0)) {
        set_error(error_out, error_out_size, "INVALID ROOM MODE");
        return -1;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"mode\":\"%s\"}", mode);
    char response[65536];
    if (api_post_json(server_url, "/room-matching/create", token, body, response,
                      sizeof(response), error_out, error_out_size) != 0) return -1;
    int has_room = 0;
    if (integral_api_parse_room_matching_response(response, room_out, &has_room) != 0 || !has_room ||
        strcmp(room_out->room_type, mode) != 0 || !room_out->creator) {
        set_error(error_out, error_out_size, "INVALID CREATE ROOM RESPONSE");
        return -1;
    }
    return 0;
}

int integral_api_join_room_code(const char *server_url,
                           const char *token,
                           const char *room_code,
                           IntegralApiRoom *room_out,
                           char *error_out,
                           size_t error_out_size)
{
    if (!valid_room_code(room_code)) {
        set_error(error_out, error_out_size, "ROOM CODE MUST BE 5 DIGITS");
        return -1;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"room_code\":\"%s\"}", room_code);
    char response[65536];
    if (api_post_json(server_url, "/room-matching/join", token, body, response,
                      sizeof(response), error_out, error_out_size) != 0) return -1;
    int has_room = 0;
    if (integral_api_parse_room_matching_response(response, room_out, &has_room) != 0 || !has_room ||
        strcmp(room_out->room_code, room_code) != 0) {
        set_error(error_out, error_out_size, "INVALID JOIN ROOM RESPONSE");
        return -1;
    }
    return 0;
}

int integral_api_get_current_room(const char *server_url,
                             const char *token,
                             IntegralApiRoom *room_out,
                             int *has_room_out,
                             char *error_out,
                             size_t error_out_size)
{
    char response[65536];
    if (api_get_json(server_url, "/room-matching/current", token, response,
                     sizeof(response), error_out, error_out_size) != 0) return -1;
    if (integral_api_parse_room_matching_response(response, room_out, has_room_out) != 0) {
        set_error(error_out, error_out_size, "INVALID CURRENT ROOM RESPONSE");
        return -1;
    }
    return 0;
}

int integral_api_leave_room(const char *server_url,
                        const char *token,
                        char *error_out,
                        size_t error_out_size)
{
    char response[8192];
    return api_post_json(server_url, "/room-matching/leave", token, "{}", response, sizeof(response), error_out, error_out_size);
}

int integral_api_stop_game(const char *server_url,
                      const char *token,
                      const char *game_session_id,
                      long long fencing_token,
                      char *error_out,
                      size_t error_out_size)
{
    if (!game_session_id || game_session_id[0] == '\0' || fencing_token <= 0) {
        set_error(error_out, error_out_size, "GAME SESSION FENCE REQUIRED");
        return -1;
    }
    char body[320];
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    char response[8192];
    return api_post_json(server_url, "/game/stop", token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_stop_local_game(const char *server_url,
                            const char *token,
                            const char *game_session_id,
                            long long fencing_token,
                            char *error_out,
                            size_t error_out_size)
{
    if (!game_session_id || game_session_id[0] == '\0' || fencing_token <= 0) {
        set_error(error_out, error_out_size, "GAME SESSION FENCE REQUIRED");
        return -1;
    }
    char body[320];
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    char response[8192];
    return api_post_json(server_url, "/game/stop", token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_heartbeat_game(const char *server_url,
                           const char *token,
                           const char *game_session_id,
                           long long fencing_token,
                           char *error_out,
                           size_t error_out_size)
{
    if (!game_session_id || game_session_id[0] == '\0' || fencing_token <= 0) {
        set_error(error_out, error_out_size, "GAME SESSION FENCE REQUIRED");
        return -1;
    }
    char body[320];
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    char response[8192];
    return api_post_json(server_url, "/game/heartbeat", token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_get_link_game_fence(const char *server_url,
                                const char *token,
                                const char *expected_link_session_id,
                                char *game_session_id_out,
                                size_t game_session_id_out_size,
                                long long *fencing_token_out,
                                char *error_out,
                                size_t error_out_size)
{
    if (!server_url || !token || !expected_link_session_id ||
        expected_link_session_id[0] == '\0' || !game_session_id_out ||
        game_session_id_out_size == 0u || !fencing_token_out) {
        set_error(error_out, error_out_size, "GAME SESSION FENCE REQUEST INVALID");
        return -1;
    }
    game_session_id_out[0] = '\0';
    *fencing_token_out = 0;
    char response[8192];
    if (api_get_json(server_url, "/game/status", token, response,
                     sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    int active = 0;
    int owner = 0;
    char link_session_id[128] = {0};
    if (extract_json_bool(response, "active", &active) != 0 || !active ||
        extract_json_bool(response, "is_owner_auth_session", &owner) != 0 || !owner ||
        extract_json_string(response, "link_session_id", link_session_id,
                            sizeof(link_session_id)) != 0 ||
        strcmp(link_session_id, expected_link_session_id) != 0 ||
        extract_json_string(response, "game_session_id", game_session_id_out,
                            game_session_id_out_size) != 0 ||
        extract_json_int64(response, "fencing_token", fencing_token_out) != 0 ||
        *fencing_token_out <= 0) {
        game_session_id_out[0] = '\0';
        *fencing_token_out = 0;
        set_error(error_out, error_out_size, "GAME SESSION FENCE RESPONSE INVALID");
        return -1;
    }
    return 0;
}

int integral_api_start_local_game(const char *server_url,
                             const char *token,
                             const char *save_id1,
                             const char *save_id2,
                             char *game_session_id_out,
                             size_t game_session_id_out_size,
                             long long *fencing_token_out,
                             char *error_out,
                             size_t error_out_size)
{
    const char *save_ids[2];
    unsigned save_count = 0;
    save_ids[save_count++] = save_id1;
    if (save_id2 && save_id2[0] != '\0') {
        save_ids[save_count++] = save_id2;
    }
    return integral_api_start_game_with_saves(server_url,
                                         token,
                                         INTEGRAL_EXECUTION_MODE_LOCAL_CLIENT,
                                         save_ids,
                                         save_count,
                                         game_session_id_out,
                                         game_session_id_out_size,
                                         fencing_token_out,
                                         error_out,
                                         error_out_size);
}

int integral_api_list_mobile_scenarios(
    const char *server_url,
    const char *token,
    const char *save_id,
    const char *rom_id,
    IntegralApiMobileScenario *scenarios,
    unsigned *scenario_count,
    char *error_out,
    size_t error_out_size)
{
    if (!server_url || !token || !save_id || !save_id[0] || !rom_id || !rom_id[0] ||
        !scenarios || !scenario_count) {
        set_error(error_out, error_out_size, "MOBILE SCENARIO REQUEST INVALID");
        return -1;
    }
    *scenario_count = 0;
    char body[256];
    snprintf(body, sizeof(body), "{\"save_id\":\"%s\",\"rom_id\":\"%s\"}", save_id, rom_id);
    char response[INTEGRAL_API_CONTROL_JSON_MAX];
    if (api_post_json(server_url, "/mobile-scenarios", token, body, response,
                      sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    const char *cursor = strstr(response, "\"scenarios\"");
    cursor = cursor ? strchr(cursor, '[') : NULL;
    if (!cursor) {
        set_error(error_out, error_out_size, "MOBILE SCENARIO RESPONSE INVALID");
        return -1;
    }
    cursor++;
    while (*cursor && *cursor != ']') {
        const char *object_start = strchr(cursor, '{');
        if (!object_start) break;
        const char *array_end = strchr(cursor, ']');
        if (array_end && object_start > array_end) break;
        const char *object_end = strchr(object_start, '}');
        if (!object_end || (size_t)(object_end - object_start) >= 512u ||
            *scenario_count >= INTEGRAL_API_MOBILE_SCENARIOS_MAX) {
            set_error(error_out, error_out_size, "MOBILE SCENARIO RESPONSE INVALID");
            return -1;
        }
        char object[512];
        size_t object_size = (size_t)(object_end - object_start + 1);
        memcpy(object, object_start, object_size);
        object[object_size] = '\0';
        IntegralApiMobileScenario *item = &scenarios[*scenario_count];
        memset(item, 0, sizeof(*item));
        if (extract_json_string(object, "scenario_id", item->scenario_id, sizeof(item->scenario_id)) != 0 ||
            extract_json_string(object, "display_name", item->display_name, sizeof(item->display_name)) != 0 ||
            extract_json_string(object, "release_id", item->release_id, sizeof(item->release_id)) != 0 ||
            extract_json_bool(object, "default", &item->is_default) != 0 ||
            !safe_path_token(item->scenario_id)) {
            set_error(error_out, error_out_size, "MOBILE SCENARIO RESPONSE INVALID");
            return -1;
        }
        (*scenario_count)++;
        cursor = object_end + 1;
    }
    if (*scenario_count == 0u) {
        set_error(error_out, error_out_size, "NO MOBILE SCENARIO AVAILABLE");
        return -1;
    }
    return 0;
}

int integral_api_start_mobile_session_contract(
    const char *server_url,
    const char *token,
    const char *save_id,
    const char *rom_id,
    const char *create_request_id,
    const char *scenario_id,
    char *mobile_session_id_out,
    size_t mobile_session_id_out_size,
    char *game_session_id_out,
    size_t game_session_id_out_size,
    long long *fencing_token_out,
    IntegralMobileRuntimeContract *contract_out,
    char *error_out,
    size_t error_out_size)
{
    if (!save_id || !save_id[0] || !rom_id || !rom_id[0] ||
        !create_request_id || !create_request_id[0] || !scenario_id || !scenario_id[0] ||
        !mobile_session_id_out || !game_session_id_out || !fencing_token_out ||
        !contract_out) {
        set_error(error_out, error_out_size, "MOBILE SESSION REQUEST INVALID");
        return -1;
    }
    char body[512];
    snprintf(body, sizeof(body),
             "{\"save_id\":\"%s\",\"rom_id\":\"%s\",\"request_id\":\"%s\",\"scenario_id\":\"%s\"}",
             save_id, rom_id, create_request_id, scenario_id);
    char *response = malloc(INTEGRAL_MOBILE_RESPONSE_MAX);
    if (!response) {
        set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    integral_mobile_runtime_contract_init(contract_out);
    int rc = api_post_json(server_url, "/mobile-sessions", token, body,
                           response, INTEGRAL_MOBILE_RESPONSE_MAX,
                           error_out, error_out_size);
    if (rc != 0 && strstr(response, "mobile_create_auth_session_conflict") != NULL) {
        set_error(error_out, error_out_size,
                  "MOBILE SESSION IS BOUND TO ANOTHER LOGIN; RETRY AFTER EXPIRY");
        integral_mobile_runtime_contract_free(contract_out);
        free(response);
        return INTEGRAL_API_MOBILE_CREATE_AUTH_SESSION_CONFLICT;
    }
    if (rc != 0 && strstr(response, "mobile_create_aborted") != NULL) {
        set_error(error_out, error_out_size,
                  "PREVIOUS MOBILE CREATE WAS ABORTED; RETRY START");
        integral_mobile_runtime_contract_free(contract_out);
        free(response);
        return INTEGRAL_API_MOBILE_CREATE_ABORTED;
    }
    const char *contract_json = strstr(response, "\r\n\r\n");
    contract_json = contract_json ? contract_json + 4 : response;
    char mobile_status[32] = {0};
    if (rc == 0 && strstr(contract_json, "\"runtime_contract\"") == NULL &&
        extract_json_string(response, "status", mobile_status, sizeof(mobile_status)) == 0 &&
        (strcmp(mobile_status, "COMPLETED") == 0 ||
         strcmp(mobile_status, "CANCELLED") == 0 ||
         strcmp(mobile_status, "EXPIRED") == 0 ||
         strcmp(mobile_status, "FAILED") == 0)) {
        set_error(error_out, error_out_size, "PREVIOUS MOBILE CREATE IS TERMINAL; RETRY START");
        free(response);
        return INTEGRAL_API_MOBILE_CREATE_ABORTED;
    }
    if (rc == 0 &&
        (extract_json_string(response, "id", mobile_session_id_out, mobile_session_id_out_size) != 0 ||
         extract_json_string(response, "game_session_id", game_session_id_out, game_session_id_out_size) != 0 ||
         extract_json_int64(response, "fencing_token", fencing_token_out) != 0 ||
         integral_mobile_runtime_contract_parse(contract_json, contract_out, error_out, error_out_size) != 0)) {
        integral_mobile_runtime_contract_free(contract_out);
        if (!error_out || !error_out[0]) {
            set_error(error_out, error_out_size, "MOBILE SESSION RESPONSE INVALID");
        }
        rc = -1;
    }
    free(response);
    return rc;
}

int integral_api_heartbeat_mobile_session(const char *server_url,
                                     const char *token,
                                     const char *mobile_session_id,
                                     const char *game_session_id,
                                     long long fencing_token,
                                     char *error_out,
                                     size_t error_out_size)
{
    if (!mobile_session_id || !mobile_session_id[0] || !game_session_id ||
        !game_session_id[0] || fencing_token <= 0) {
        set_error(error_out, error_out_size, "MOBILE SESSION FENCE REQUIRED");
        return -1;
    }
    char path[192];
    char body[320];
    char response[8192];
    snprintf(path, sizeof(path), "/mobile-sessions/%s/heartbeat", mobile_session_id);
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_complete_mobile_session(const char *server_url,
                                    const char *token,
                                    const char *mobile_session_id,
                                    const char *game_session_id,
                                    long long fencing_token,
                                    char *error_out,
                                    size_t error_out_size)
{
    if (!mobile_session_id || !mobile_session_id[0] || !game_session_id ||
        !game_session_id[0] || fencing_token <= 0) {
        set_error(error_out, error_out_size, "MOBILE COMPLETE REQUEST INVALID");
        return -1;
    }
    char body[320];
    snprintf(body, sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id, fencing_token);
    char path[192];
    char response[8192];
    snprintf(path, sizeof(path), "/mobile-sessions/%s/complete", mobile_session_id);
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_cancel_mobile_session(const char *server_url,
                                  const char *token,
                                  const char *mobile_session_id,
                                  const char *game_session_id,
                                  long long fencing_token,
                                  const char *reason,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (!mobile_session_id || !mobile_session_id[0] || !game_session_id ||
        !game_session_id[0] || fencing_token <= 0) {
        set_error(error_out, error_out_size, "MOBILE SESSION FENCE REQUIRED");
        return -1;
    }
    char path[192];
    char body[512];
    char response[8192];
    snprintf(path, sizeof(path), "/mobile-sessions/%s/cancel", mobile_session_id);
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld,\"reason\":\"%s\"}",
             game_session_id,
             fencing_token,
             reason && reason[0] ? reason : "client canceled");
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_start_game_with_saves(const char *server_url,
                                  const char *token,
                                  const char *execution_mode,
                                  const char *const *save_ids,
                                  unsigned save_count,
                                  char *game_session_id_out,
                                  size_t game_session_id_out_size,
                                  long long *fencing_token_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (game_session_id_out && game_session_id_out_size > 0) {
        game_session_id_out[0] = '\0';
    }
    if (fencing_token_out) {
        *fencing_token_out = 0;
    }
    if (!execution_mode || execution_mode[0] == '\0' || !save_ids || save_count == 0) {
        set_error(error_out, error_out_size, "SAVE ID REQUIRED");
        return -1;
    }
    char body[768];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"execution_mode\":\"%s\",\"save_ids\":[",
                     execution_mode);
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    size_t used = (size_t)n;
    for (unsigned i = 0; i < save_count; i++) {
        if (!save_ids[i] || save_ids[i][0] == '\0') {
            set_error(error_out, error_out_size, "SAVE ID REQUIRED");
            return -1;
        }
        n = snprintf(body + used,
                     sizeof(body) - used,
                     "%s\"%s\"",
                     i == 0 ? "" : ",",
                     save_ids[i]);
        if (n <= 0 || (size_t)n >= sizeof(body) - used) {
            set_error(error_out, error_out_size, "REQUEST TOO LARGE");
            return -1;
        }
        used += (size_t)n;
    }
    n = snprintf(body + used, sizeof(body) - used, "]}");
    if (n <= 0 || (size_t)n >= sizeof(body) - used) {
        set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    char response[8192];
    int rc = api_post_json(server_url, "/game/start", token, body, response, sizeof(response), error_out, error_out_size);
    if (rc != 0) {
        return rc;
    }
    if (game_session_id_out && game_session_id_out_size > 0) {
        if (extract_json_string(response, "game_session_id", game_session_id_out, game_session_id_out_size) != 0) {
            set_error(error_out, error_out_size, "GAME SESSION ID MISSING");
            return -1;
        }
    }
    if (!fencing_token_out || extract_json_int64(response, "fencing_token", fencing_token_out) != 0 || *fencing_token_out <= 0) {
        set_error(error_out, error_out_size, "GAME SESSION FENCE MISSING");
        return -1;
    }
    return 0;
}

int integral_api_room_heartbeat(const char *server_url,
                            const char *token,
                            char *error_out,
                            size_t error_out_size)
{
    return integral_api_room_heartbeat_status(
        server_url, token, NULL, error_out, error_out_size
    );
}

int integral_api_room_heartbeat_status(const char *server_url,
                            const char *token,
                            IntegralApiHeartbeatStatus *status_out,
                            char *error_out,
                            size_t error_out_size)
{
    char response[8192];
    if (status_out) {
        memset(status_out, 0, sizeof(*status_out));
    }
    int rc = api_post_json(server_url, "/rooms/heartbeat", token, "{}", response, sizeof(response), error_out, error_out_size);
    if (rc != 0 || !status_out) {
        return rc;
    }
    (void)extract_json_string(response, "kind", status_out->lifecycle_kind, sizeof(status_out->lifecycle_kind));
    (void)extract_json_string(response, "session_id", status_out->lifecycle_session_id, sizeof(status_out->lifecycle_session_id));
    (void)extract_json_string(response, "status", status_out->lifecycle_status, sizeof(status_out->lifecycle_status));
    (void)extract_json_string(response, "termination_reason", status_out->termination_reason, sizeof(status_out->termination_reason));
    (void)extract_json_string(response, "expires_at", status_out->expires_at, sizeof(status_out->expires_at));
    (void)extract_json_int64(response, "unix_time", &status_out->server_unix_time);
    return 0;
}

int integral_api_get_server_time(const char *server_url,
                            long long *unix_time_out,
                            char *error_out,
                            size_t error_out_size)
{
    if (unix_time_out) {
        *unix_time_out = 0;
    }
    char response[2048];
    if (api_get_json(server_url, "/time", NULL, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (!unix_time_out || extract_json_int64(response, "unix_time", unix_time_out) != 0 || *unix_time_out <= 0) {
        set_error(error_out, error_out_size, "TIME RESPONSE MISSING UNIX TIME");
        return -1;
    }
    return 0;
}

static int parse_rom_slots_response(const char *response,
                                    IntegralApiRomSlot *slots,
                                    size_t slot_count,
                                    char *error_out,
                                    size_t error_out_size)
{
    for (size_t i = 0; i < slot_count; i++) {
        memset(&slots[i], 0, sizeof(slots[i]));
        slots[i].slot = (unsigned)i + 1;
    }

    const char *p = strstr(response, "\"slots\"");
    if (!p) {
        set_error(error_out, error_out_size, "ROM SLOTS RESPONSE MISSING SLOTS");
        return -1;
    }
    p = strchr(p, '[');
    if (!p) {
        set_error(error_out, error_out_size, "ROM SLOTS RESPONSE INVALID");
        return -1;
    }

    while ((p = strchr(p, '{')) != NULL) {
        const char *object_end = strchr(p, '}');
        if (!object_end) {
            break;
        }
        int slot_number = 0;
        const char *slot_key = strstr(p, "\"slot\"");
        if (!slot_key || slot_key > object_end) {
            p = object_end + 1;
            continue;
        }
        const char *slot_colon = strchr(slot_key, ':');
        if (!slot_colon || slot_colon > object_end || sscanf(slot_colon + 1, "%d", &slot_number) != 1 ||
            slot_number < 1 || (size_t)slot_number > slot_count) {
            p = object_end + 1;
            continue;
        }

        IntegralApiRomSlot *slot = &slots[slot_number - 1];
        slot->slot = (unsigned)slot_number;
        const char *key = NULL;
        const char *colon = NULL;
        if ((key = strstr(p, "\"rom_id\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->rom_id, sizeof(slot->rom_id));
        }
        if ((key = strstr(p, "\"save_id\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->save_id, sizeof(slot->save_id));
        }
        if ((key = strstr(p, "\"filename\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->filename, sizeof(slot->filename));
        }
        if ((key = strstr(p, "\"game_type\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->game_type, sizeof(slot->game_type));
        }
        if ((key = strstr(p, "\"sha256\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->sha256, sizeof(slot->sha256));
        }
        if ((key = strstr(p, "\"platform\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->platform, sizeof(slot->platform));
        }
        if ((key = strstr(p, "\"rom_header_title\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->rom_header_title, sizeof(slot->rom_header_title));
        }
        p = object_end + 1;
    }
    return 0;
}

int integral_api_get_rom_slots(const char *server_url,
                          const char *token,
                          IntegralApiRomSlot *slots,
                          size_t slot_count,
                          char *error_out,
                          size_t error_out_size)
{
    char response[32768];
    if (api_get_json(server_url, "/rom-slots", token, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    return parse_rom_slots_response(response, slots, slot_count, error_out, error_out_size);
}

int integral_api_update_room_state(const char *server_url,
                                    const char *token,
                                    unsigned room_number,
                                    const char *slot,
                                    int ready,
                                    const char *link_mode,
                                    char *error_out,
                                    size_t error_out_size)
{
    char path[80];
    char escaped_slot[64];
    char escaped_mode[64];
    json_escape(slot ? slot : "", escaped_slot, sizeof(escaped_slot));
    snprintf(path, sizeof(path), "/rooms/%u/state", room_number);
    char body[240];
    if (link_mode && link_mode[0] != '\0') {
        json_escape(link_mode, escaped_mode, sizeof(escaped_mode));
        snprintf(body,
                 sizeof(body),
                 "{\"slot\":\"%s\",\"ready\":%s,\"link_mode\":\"%s\"}",
                 escaped_slot,
                 ready ? "true" : "false",
                 escaped_mode);
    }
    else {
        snprintf(body,
                 sizeof(body),
                 "{\"slot\":\"%s\",\"ready\":%s}",
                 escaped_slot,
                 ready ? "true" : "false");
    }
    char response[8192];
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_update_n64_room_state(const char *server_url,
                                        const char *token,
                                        unsigned room_number,
                                        const char *slot,
                                        const char *n64_slot,
                                        int ready,
                                        char *error_out,
                                        size_t error_out_size)
{
    char path[80];
    char escaped_slot[64];
    char escaped_n64_slot[64];
    json_escape(slot ? slot : "", escaped_slot, sizeof(escaped_slot));
    json_escape(n64_slot ? n64_slot : "", escaped_n64_slot, sizeof(escaped_n64_slot));
    snprintf(path, sizeof(path), "/rooms/%u/state", room_number);
    char body[240];
    snprintf(body,
             sizeof(body),
             "{\"slot\":\"%s\",\"n64_slot\":\"%s\",\"ready\":%s}",
             escaped_slot,
             escaped_n64_slot,
             ready ? "true" : "false");
    char response[8192];
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_send_room_chat(const char *server_url,
                                 const char *token,
                                 unsigned room_number,
                                 const char *message,
                                 char *error_out,
                                 size_t error_out_size)
{
    char path[80];
    char escaped_message[384];
    json_escape(message ? message : "", escaped_message, sizeof(escaped_message));
    snprintf(path, sizeof(path), "/rooms/%u/chat", room_number);
    char body[480];
    snprintf(body, sizeof(body), "{\"message\":\"%s\"}", escaped_message);
    char response[8192];
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_start_room(const char *server_url,
                             const char *token,
                             unsigned room_number,
                             const char *link_mode,
                             char *session_id_out,
                             size_t session_id_out_size,
                             char *error_out,
                             size_t error_out_size)
{
    if (session_id_out_size > 0) {
        session_id_out[0] = '\0';
    }
    char path[80];
    char escaped_mode[64];
    snprintf(path, sizeof(path), "/rooms/%u/start", room_number);
    json_escape(link_mode ? link_mode : "trade", escaped_mode, sizeof(escaped_mode));
    char body[96];
    snprintf(body, sizeof(body), "{\"link_mode\":\"%s\"}", escaped_mode);
    char response[8192];
    if (api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "id", session_id_out, session_id_out_size) != 0) {
        set_error(error_out, error_out_size, "ROOM START RESPONSE MISSING ID");
        return -1;
    }
    return 0;
}

int integral_api_start_n64_room(const char *server_url,
                                 const char *token,
                                 unsigned room_number,
                                 char *session_id_out,
                                 size_t session_id_out_size,
                                 char *relay_host_out,
                                 size_t relay_host_out_size,
                                 unsigned *relay_port_out,
                                 char *relay_transport_out,
                                 size_t relay_transport_out_size,
                                 char *role_out,
                                 size_t role_out_size,
                                 char *scope_out,
                                 size_t scope_out_size,
                                 char *ticket_out,
                                 size_t ticket_out_size,
                                 char *error_out,
                                 size_t error_out_size)
{
    if (!relay_transport_out || relay_transport_out_size == 0u) {
        set_error(error_out, error_out_size, "N64 MEDIA START PARAMETERS INVALID");
        return -1;
    }
    if (session_id_out_size > 0) session_id_out[0] = '\0';
    if (relay_host_out_size > 0) relay_host_out[0] = '\0';
    if (relay_transport_out_size > 0) relay_transport_out[0] = '\0';
    if (role_out_size > 0) role_out[0] = '\0';
    if (scope_out_size > 0) scope_out[0] = '\0';
    if (ticket_out_size > 0) ticket_out[0] = '\0';
    if (relay_port_out) *relay_port_out = 0;
    char path[80];
    snprintf(path, sizeof(path), "/rooms/%u/start", room_number);
    char response[8192];
    if (api_post_json(server_url, path, token, "{}", response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    int relay_port = 0;
    if (extract_json_string(response, "id", session_id_out, session_id_out_size) != 0 ||
        extract_json_string(response, "relay_host", relay_host_out, relay_host_out_size) != 0 ||
        extract_json_int(response, "relay_port", &relay_port) != 0 || relay_port <= 0 || relay_port > 65535 ||
        extract_json_string(response, "relay_transport", relay_transport_out, relay_transport_out_size) != 0 ||
        (strcmp(relay_transport_out, "tls") != 0 && strcmp(relay_transport_out, "plain") != 0) ||
        extract_json_string(response, "role", role_out, role_out_size) != 0 ||
        extract_json_string(response, "scope", scope_out, scope_out_size) != 0 ||
        extract_json_string(response, "ticket", ticket_out, ticket_out_size) != 0) {
        set_error(error_out, error_out_size, "N64 MEDIA START RESPONSE INVALID");
        return -1;
    }
    if (relay_port_out) {
        *relay_port_out = (unsigned)relay_port;
    }
    return 0;
}

 int integral_api_gb_runtime_fixed_host_get_manifest(const char *server_url,
                                    const char *token,
                                    const char *session_id,
                                    char *role_out,
                                    size_t role_out_size,
                                    char *manifest_digest_out,
                                    size_t manifest_digest_out_size,
                                    char *host_game_type_out,
                                    size_t host_game_type_out_size,
                                    char *remote_game_type_out,
                                    size_t remote_game_type_out_size,
                                    char *host_platform_out,
                                    size_t host_platform_out_size,
                                    char *remote_platform_out,
                                    size_t remote_platform_out_size,
                                    char *host_header_title_out,
                                    size_t host_header_title_out_size,
                                    char *remote_header_title_out,
                                    size_t remote_header_title_out_size,
                                    char *runtime_build_id_out,
                                    size_t runtime_build_id_out_size,
                                    char *state_out,
                                    size_t state_out_size,
                                    unsigned *pause_remaining_seconds_out,
                                    char *error_out,
                                    size_t error_out_size)
{
    char path[192];
    unsigned char json[INTEGRAL_API_CONTROL_JSON_MAX + 1u];
    size_t json_size = 0u;
    int result = -1;
    if (role_out && role_out_size) role_out[0] = '\0';
    if (manifest_digest_out && manifest_digest_out_size)
        manifest_digest_out[0] = '\0';
    if (host_game_type_out && host_game_type_out_size)
        host_game_type_out[0] = '\0';
    if (remote_game_type_out && remote_game_type_out_size)
        remote_game_type_out[0] = '\0';
    if (host_platform_out && host_platform_out_size) host_platform_out[0] = '\0';
    if (remote_platform_out && remote_platform_out_size) remote_platform_out[0] = '\0';
    if (host_header_title_out && host_header_title_out_size) host_header_title_out[0] = '\0';
    if (remote_header_title_out && remote_header_title_out_size) remote_header_title_out[0] = '\0';
    if (runtime_build_id_out && runtime_build_id_out_size)
        runtime_build_id_out[0] = '\0';
    if (state_out && state_out_size) state_out[0] = '\0';
    if (pause_remaining_seconds_out) *pause_remaining_seconds_out = 0u;
    if (!safe_path_token(session_id) || role_out == NULL || role_out_size == 0u ||
        manifest_digest_out == NULL || manifest_digest_out_size == 0u ||
        host_game_type_out == NULL || host_game_type_out_size == 0u ||
        remote_game_type_out == NULL || remote_game_type_out_size == 0u ||
        host_platform_out == NULL || host_platform_out_size == 0u ||
        remote_platform_out == NULL || remote_platform_out_size == 0u ||
        host_header_title_out == NULL || host_header_title_out_size == 0u ||
        remote_header_title_out == NULL || remote_header_title_out_size == 0u ||
        runtime_build_id_out == NULL || runtime_build_id_out_size == 0u ||
        state_out == NULL || state_out_size == 0u ||
        pause_remaining_seconds_out == NULL) {
        set_error(error_out, error_out_size, "FIXED HOST MANIFEST INVALID");
        return -1;
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/manifest", session_id);
    if (api_control_json_body_request(
            "GET", server_url, path, token, "", json, sizeof(json) - 1u, &json_size,
            error_out, error_out_size) == 0) {
        json[json_size] = '\0';
        if (extract_json_string((char *)json, "role", role_out, role_out_size) == 0 &&
            extract_json_string((char *)json, "manifest_digest", manifest_digest_out,
                                manifest_digest_out_size) == 0 &&
            extract_json_string((char *)json, "host_game_type", host_game_type_out,
                                host_game_type_out_size) == 0 &&
            extract_json_string((char *)json, "remote_game_type", remote_game_type_out,
                                remote_game_type_out_size) == 0 &&
            extract_json_string((char *)json, "host_platform", host_platform_out,
                                host_platform_out_size) == 0 &&
            extract_json_string((char *)json, "remote_platform", remote_platform_out,
                                remote_platform_out_size) == 0 &&
            extract_json_string((char *)json, "host_rom_header_title", host_header_title_out,
                                host_header_title_out_size) == 0 &&
            extract_json_string((char *)json, "remote_rom_header_title", remote_header_title_out,
                                remote_header_title_out_size) == 0 &&
            extract_json_string((char *)json, "runtime_build_id", runtime_build_id_out,
                                runtime_build_id_out_size) == 0 &&
            extract_json_string((char *)json, "state", state_out, state_out_size) == 0) {
            int remaining = 0;
            if (extract_json_int((char *)json, "pause_remaining_seconds", &remaining) == 0 &&
                remaining >= 0) {
                *pause_remaining_seconds_out = (unsigned)remaining;
                result = 0;
            }
            else {
                set_error(error_out, error_out_size,
                          "FIXED HOST PAUSE DEADLINE INVALID");
            }
        }
        else {
            set_error(error_out, error_out_size,
                      "FIXED HOST MANIFEST RESPONSE INVALID");
        }
    }
    secure_wipe(json, sizeof(json));
    return result;
}

int integral_api_gb_runtime_fixed_host_submit_preflight(const char *server_url,
                                        const char *token,
                                        const char *session_id,
                                        const char *manifest_digest,
                                        const char *runtime_build_id,
                                        const char *game_type_a,
                                        const char *platform_a,
                                        const char *header_title_a,
                                        const char *game_type_b,
                                        const char *platform_b,
                                        const char *header_title_b,
                                        char *state_out,
                                        size_t state_out_size,
                                        char *error_out,
                                        size_t error_out_size)
{
    char path[192];
    char digest[96];
    char build[192];
    char game_type_a_json[64];
    char game_type_b_json[64];
    char platform_a_json[16];
    char platform_b_json[16];
    char header_a_json[64];
    char header_b_json[64];
    char body[1024];
    char response[INTEGRAL_API_CONTROL_JSON_MAX + INTEGRAL_HTTP_HEADER_MAX + 1u];
    int result = -1;
    if (state_out && state_out_size) state_out[0] = '\0';
    if (!safe_path_token(session_id) || manifest_digest == NULL ||
        runtime_build_id == NULL || game_type_a == NULL || game_type_b == NULL ||
        platform_a == NULL || platform_b == NULL ||
        header_title_a == NULL || header_title_b == NULL ||
        state_out == NULL || state_out_size == 0u) {
        set_error(error_out, error_out_size, "FIXED HOST PREFLIGHT INVALID");
        return -1;
    }
    json_escape(manifest_digest, digest, sizeof(digest));
    json_escape(runtime_build_id, build, sizeof(build));
    json_escape(game_type_a, game_type_a_json, sizeof(game_type_a_json));
    json_escape(game_type_b, game_type_b_json, sizeof(game_type_b_json));
    json_escape(platform_a, platform_a_json, sizeof(platform_a_json));
    json_escape(platform_b, platform_b_json, sizeof(platform_b_json));
    json_escape(header_title_a, header_a_json, sizeof(header_a_json));
    json_escape(header_title_b, header_b_json, sizeof(header_b_json));
    if (game_type_a[0] == '\0' && game_type_b[0] == '\0') {
        snprintf(body, sizeof(body),
                 "{\"manifest_digest\":\"%s\",\"protocol_id\":\"gb_runtime_fixed_host_v1\","
                 "\"runtime_build_id\":\"%s\",\"available_roms\":[]}",
                 digest, build);
    }
    else if (strcmp(game_type_a, game_type_b) == 0 &&
             strcmp(platform_a, platform_b) == 0 &&
             strcmp(header_title_a, header_title_b) == 0) {
        snprintf(body, sizeof(body),
                 "{\"manifest_digest\":\"%s\",\"protocol_id\":\"gb_runtime_fixed_host_v1\","
                 "\"runtime_build_id\":\"%s\",\"available_roms\":[{\"game_type\":\"%s\",\"platform\":\"%s\",\"rom_header_title\":\"%s\"}]}",
                 digest, build, game_type_a_json, platform_a_json, header_a_json);
    }
    else {
        snprintf(body, sizeof(body),
                 "{\"manifest_digest\":\"%s\",\"protocol_id\":\"gb_runtime_fixed_host_v1\","
                 "\"runtime_build_id\":\"%s\",\"available_roms\":[{\"game_type\":\"%s\",\"platform\":\"%s\",\"rom_header_title\":\"%s\"},{\"game_type\":\"%s\",\"platform\":\"%s\",\"rom_header_title\":\"%s\"}]}",
                 digest, build, game_type_a_json, platform_a_json, header_a_json,
                 game_type_b_json, platform_b_json, header_b_json);
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/preflight", session_id);
    if (api_post_json(server_url, path, token, body, response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "state", state_out, state_out_size) == 0) {
        result = 0;
    }
    else if (error_out && error_out_size && error_out[0] == '\0') {
        set_error(error_out, error_out_size,
                  "FIXED HOST PREFLIGHT RESPONSE INVALID");
    }
    secure_wipe(body, sizeof(body));
    secure_wipe(response, sizeof(response));
    return result;
}

int integral_api_gb_runtime_fixed_host_issue_relay_ticket(const char *server_url,
                                          const char *token,
                                          const char *session_id,
                                          char *relay_host_out,
                                          size_t relay_host_out_size,
                                          unsigned *relay_port_out,
                                          char *relay_transport_out,
                                          size_t relay_transport_out_size,
                                          char *role_out,
                                          size_t role_out_size,
                                          char *scope_out,
                                          size_t scope_out_size,
                                          char *ticket_out,
                                          size_t ticket_out_size,
                                          char *save_policy_out,
                                          size_t save_policy_out_size,
                                          char *error_out,
                                          size_t error_out_size)
{
    char path[192];
    char response[INTEGRAL_API_CONTROL_JSON_MAX + INTEGRAL_HTTP_HEADER_MAX + 1u];
    int relay_port = 0;
    int result = -1;
    if (relay_transport_out && relay_transport_out_size) relay_transport_out[0] = '\0';
    if (!safe_path_token(session_id) || relay_host_out == NULL ||
        relay_port_out == NULL || role_out == NULL || scope_out == NULL ||
        relay_transport_out == NULL || relay_transport_out_size == 0u ||
        ticket_out == NULL || save_policy_out == NULL || save_policy_out_size == 0u) {
        set_error(error_out, error_out_size, "FIXED HOST RELAY REQUEST INVALID");
        return -1;
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/relay-ticket", session_id);
    if (api_post_json(server_url, path, token, "{}", response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "relay_host", relay_host_out,
                            relay_host_out_size) == 0 &&
        extract_json_int(response, "relay_port", &relay_port) == 0 &&
        relay_port > 0 && relay_port <= 65535 &&
        extract_json_string(response, "relay_transport", relay_transport_out,
                            relay_transport_out_size) == 0 &&
        (strcmp(relay_transport_out, "tls") == 0 ||
         strcmp(relay_transport_out, "plain") == 0) &&
        extract_json_string(response, "role", role_out, role_out_size) == 0 &&
        extract_json_string(response, "scope", scope_out, scope_out_size) == 0 &&
        extract_json_string(response, "ticket", ticket_out, ticket_out_size) == 0 &&
        extract_json_string(response, "save_policy", save_policy_out,
                            save_policy_out_size) == 0) {
        *relay_port_out = (unsigned)relay_port;
        result = 0;
    }
    else if (error_out && error_out_size && error_out[0] == '\0') {
        set_error(error_out, error_out_size, "FIXED HOST RELAY RESPONSE INVALID");
    }
    secure_wipe(response, sizeof(response));
    return result;
}

int integral_api_gb_runtime_fixed_host_download_snapshots(const char *server_url,
                                          const char *token,
                                          const char *session_id,
                                          unsigned char *host_save_out,
                                          size_t host_save_capacity,
                                          size_t *host_save_size_out,
                                          unsigned char *remote_save_out,
                                          size_t remote_save_capacity,
                                          size_t *remote_save_size_out,
                                          char *error_out,
                                          size_t error_out_size)
{
    const size_t json_capacity = 6u * 1024u * 1024u;
    const size_t response_capacity = json_capacity + INTEGRAL_HTTP_HEADER_MAX + 1u;
    char path[192];
    unsigned char *json = NULL;
    char *host_encoded = NULL, *remote_encoded = NULL;
    const char *host_section, *remote_section, *json_body;
    char save_policy[32];
    size_t json_size = 0u;
    int result = -1;
    if (host_save_size_out) *host_save_size_out = 0u;
    if (remote_save_size_out) *remote_save_size_out = 0u;
    if (!safe_path_token(session_id) || host_save_out == NULL ||
        host_save_size_out == NULL || remote_save_out == NULL ||
        remote_save_size_out == NULL) {
        set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT REQUEST INVALID");
        return -1;
    }
    json = malloc(response_capacity);
    host_encoded = malloc(json_capacity / 2u);
    remote_encoded = malloc(json_capacity / 2u);
    if (!json || !host_encoded || !remote_encoded) {
        set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT MEMORY FAILED");
        goto done;
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/runtime-snapshots", session_id);
    if (api_json_request("GET", server_url, path, token, "", (char *)json,
                         response_capacity, error_out, error_out_size) != 0)
        goto done;
    json_body = http_body_start((char *)json, strlen((char *)json), &json_size);
    if (json_body == NULL || json_size == 0u || json_size > json_capacity) {
        set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT RESPONSE INVALID");
        goto done;
    }
    if (extract_json_string(json_body, "save_policy", save_policy,
                            sizeof(save_policy)) != 0 ||
        (strcmp(save_policy, "discard") != 0 &&
         strcmp(save_policy, "commit_pair") != 0)) {
        set_error(error_out, error_out_size, "FIXED HOST SAVE POLICY INVALID");
        goto done;
    }
    host_section = strstr(json_body, "\"host\"");
    remote_section = strstr(json_body, "\"remote\"");
    if (!host_section || !remote_section || host_section >= remote_section ||
        extract_json_string(host_section, "save_data", host_encoded,
                            json_capacity / 2u) != 0 ||
        extract_json_string(remote_section, "save_data", remote_encoded,
                            json_capacity / 2u) != 0 ||
        base64_decode(host_encoded, host_save_out, host_save_capacity,
                      host_save_size_out) != 0 ||
        base64_decode(remote_encoded, remote_save_out, remote_save_capacity,
                      remote_save_size_out) != 0) {
        set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT RESPONSE INVALID");
        goto done;
    }
    result = 0;
done:
    if (json) { secure_wipe(json, response_capacity); free(json); }
    if (host_encoded) { secure_wipe(host_encoded, json_capacity / 2u); free(host_encoded); }
    if (remote_encoded) { secure_wipe(remote_encoded, json_capacity / 2u); free(remote_encoded); }
    if (result != 0) {
        if (host_save_out) secure_wipe(host_save_out, host_save_capacity);
        if (remote_save_out) secure_wipe(remote_save_out, remote_save_capacity);
    }
    return result;
}

static void gb_runtime_fixed_host_digest_hex(const uint8_t digest[32], char output[65])
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t index = 0u; index < 32u; index++) {
        output[index * 2u] = HEX[digest[index] >> 4u];
        output[index * 2u + 1u] = HEX[digest[index] & 15u];
    }
    output[64] = '\0';
}

int integral_api_gb_runtime_fixed_host_submit_host_finish(
    const char *server_url, const char *token, const char *session_id,
    const char *game_session_id, long long fencing_token,
    uint64_t final_frame, const uint8_t terminal_digest[32],
    const uint8_t *host_candidate, size_t host_candidate_size,
    const uint8_t *remote_candidate, size_t remote_candidate_size,
    char *state_out, size_t state_out_size,
    char *error_out, size_t error_out_size)
{
    char path[192], digest[65], game_session[160];
    char *host_encoded = NULL, *remote_encoded = NULL, *body = NULL;
    char response[INTEGRAL_HTTP_HEADER_MAX + 4096u];
    size_t host_encoded_size, remote_encoded_size, body_size;
    int result = -1;
    if (state_out && state_out_size) state_out[0] = '\0';
    if (!safe_path_token(session_id) || !game_session_id || !terminal_digest ||
        !host_candidate || !remote_candidate || !host_candidate_size ||
        !remote_candidate_size || fencing_token <= 0 || final_frame == 0u ||
        !state_out || !state_out_size) {
        set_error(error_out, error_out_size, "FIXED HOST FINISH INVALID");
        return -1;
    }
    host_encoded_size = ((host_candidate_size + 2u) / 3u) * 4u + 1u;
    remote_encoded_size = ((remote_candidate_size + 2u) / 3u) * 4u + 1u;
    body_size = host_encoded_size + remote_encoded_size + 512u;
    host_encoded = malloc(host_encoded_size);
    remote_encoded = malloc(remote_encoded_size);
    body = malloc(body_size);
    if (!host_encoded || !remote_encoded || !body) {
        set_error(error_out, error_out_size, "FIXED HOST FINISH MEMORY FAILED");
        goto done;
    }
    base64_encode(host_candidate, host_candidate_size, host_encoded, host_encoded_size);
    base64_encode(remote_candidate, remote_candidate_size, remote_encoded, remote_encoded_size);
    gb_runtime_fixed_host_digest_hex(terminal_digest, digest);
    json_escape(game_session_id, game_session, sizeof(game_session));
    snprintf(body, body_size,
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld,"
             "\"final_frame\":%llu,\"terminal_digest\":\"%s\","
             "\"host_candidate\":\"%s\",\"remote_candidate\":\"%s\"}",
             game_session, fencing_token, (unsigned long long)final_frame, digest,
             host_encoded, remote_encoded);
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/host-finish", session_id);
    if (api_post_json(server_url, path, token, body, response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "state", state_out, state_out_size) == 0) result = 0;
    else if (error_out && error_out_size && !error_out[0])
        set_error(error_out, error_out_size, "FIXED HOST FINISH RESPONSE INVALID");
done:
    secure_wipe(response, sizeof(response)); secure_wipe(digest, sizeof(digest));
    if (body) { secure_wipe(body, body_size); free(body); }
    if (host_encoded) { secure_wipe(host_encoded, host_encoded_size); free(host_encoded); }
    if (remote_encoded) { secure_wipe(remote_encoded, remote_encoded_size); free(remote_encoded); }
    return result;
}

int integral_api_gb_runtime_fixed_host_submit_terminal_receipt(
    const char *server_url, const char *token, const char *session_id,
    uint64_t final_frame, const uint8_t terminal_digest[32],
    char *state_out, size_t state_out_size,
    char *error_out, size_t error_out_size)
{
    char path[192], digest[65], body[256], response[INTEGRAL_HTTP_HEADER_MAX + 4096u];
    int result = -1;
    if (state_out && state_out_size) state_out[0] = '\0';
    if (!safe_path_token(session_id) || !terminal_digest || final_frame == 0u ||
        !state_out || !state_out_size) {
        set_error(error_out, error_out_size, "FIXED HOST RECEIPT INVALID");
        return -1;
    }
    gb_runtime_fixed_host_digest_hex(terminal_digest, digest);
    snprintf(body, sizeof(body), "{\"final_frame\":%llu,\"terminal_digest\":\"%s\"}",
             (unsigned long long)final_frame, digest);
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/terminal-receipt", session_id);
    if (api_post_json(server_url, path, token, body, response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "state", state_out, state_out_size) == 0) result = 0;
    else if (error_out && error_out_size && !error_out[0])
        set_error(error_out, error_out_size, "FIXED HOST RECEIPT RESPONSE INVALID");
    secure_wipe(response, sizeof(response)); secure_wipe(body, sizeof(body));
    secure_wipe(digest, sizeof(digest)); return result;
}

int integral_api_get_link_session_info(const char *server_url,
                                  const char *token,
                                  const char *session_id,
                                  char *status_out,
                                  size_t status_out_size,
                                  int *room_number_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (status_out_size > 0) {
        status_out[0] = '\0';
    }
    if (room_number_out) {
        *room_number_out = 0;
    }
    char path[192];
    snprintf(path, sizeof(path), "/link-sessions/%s", session_id);
    char response[8192];
    if (api_get_json(server_url, path, token, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "status", status_out, status_out_size) != 0) {
        set_error(error_out, error_out_size, "LINK SESSION RESPONSE MISSING STATUS");
        return -1;
    }
    if (room_number_out) {
        (void)extract_json_int(response, "room_number", room_number_out);
    }
    return 0;
}

int integral_api_get_link_session_protocol(const char *server_url,
                                      const char *token,
                                      const char *session_id,
                                      char *protocol_id_out,
                                      size_t protocol_id_out_size,
                                      char *error_out,
                                      size_t error_out_size)
{
    char path[192];
    char response[8192];
    if (protocol_id_out && protocol_id_out_size) protocol_id_out[0] = '\0';
    if (!safe_path_token(session_id) || protocol_id_out == NULL ||
        protocol_id_out_size == 0u) {
        set_error(error_out, error_out_size, "LINK SESSION PROTOCOL INVALID");
        return -1;
    }
    snprintf(path, sizeof(path), "/link-sessions/%s", session_id);
    if (api_get_json(server_url, path, token, response, sizeof(response),
                     error_out, error_out_size) != 0 ||
        extract_json_string(response, "protocol_id", protocol_id_out,
                            protocol_id_out_size) != 0) {
        if (error_out && error_out_size && error_out[0] == '\0') {
            set_error(error_out, error_out_size,
                      "LINK SESSION RESPONSE MISSING PROTOCOL");
        }
        return -1;
    }
    return 0;
}

int integral_api_register_rom(const char *server_url,
                         const char *token,
                         const char *sha256,
                         const char *sha1,
                         const char *title,
                         const char *platform,
                         const char *region,
                         const char *rom_header_title,
                         char *rom_id_out,
                         size_t rom_id_out_size,
                         char *error_out,
                         size_t error_out_size)
{
    rom_id_out[0] = '\0';
    char escaped_title[160];
    char escaped_header_title[64];
    json_escape(title, escaped_title, sizeof(escaped_title));
    json_escape(rom_header_title, escaped_header_title, sizeof(escaped_header_title));
    char body[512];
    snprintf(body,
             sizeof(body),
             "{\"sha256\":\"%s\",\"sha1\":\"%s\",\"title\":\"%s\",\"platform\":\"%s\",\"region\":\"%s\",\"rom_header_title\":\"%s\"}",
             sha256,
             sha1,
             escaped_title,
             platform,
             region,
             escaped_header_title);
    char response[8192];
    if (api_post_json(server_url, "/roms", token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "id", rom_id_out, rom_id_out_size) != 0) {
        set_error(error_out, error_out_size, "ROM RESPONSE MISSING ID");
        return -1;
    }
    return 0;
}

static void base64_encode(const unsigned char *data, size_t data_size, char *out, size_t out_size)
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

static int base64_decode(const char *encoded, unsigned char *out, size_t out_capacity, size_t *out_size)
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

static int download_save_path(const char *server_url,
                              const char *token,
                              const char *path,
                              unsigned char *save_data_out,
                              size_t save_data_capacity,
                              size_t *save_data_size_out,
                              int *revision_out,
                              char *error_out,
                              size_t error_out_size)
{
    *save_data_size_out = 0;
    *revision_out = 0;

    char *response = malloc(INTEGRAL_SAVE_RESPONSE_MAX);
    if (!response) {
        set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    int rc = api_get_json(server_url, path, token, response, INTEGRAL_SAVE_RESPONSE_MAX, error_out, error_out_size);
    if (rc != 0) {
        free(response);
        return -1;
    }

    if (extract_json_int(response, "revision", revision_out) != 0) {
        free(response);
        set_error(error_out, error_out_size, "SAVE RESPONSE MISSING REVISION");
        return -1;
    }

    char *encoded = malloc(INTEGRAL_SAVE_RESPONSE_MAX);
    if (!encoded) {
        free(response);
        set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    if (extract_json_string(response, "save_data", encoded, INTEGRAL_SAVE_RESPONSE_MAX) != 0 ||
        base64_decode(encoded, save_data_out, save_data_capacity, save_data_size_out) != 0) {
        free(encoded);
        free(response);
        set_error(error_out, error_out_size, "SAVE DATA DECODE FAILED");
        return -1;
    }
    free(encoded);
    free(response);
    return 0;
}

int integral_api_download_save(const char *server_url,
                          const char *token,
                          const char *save_id,
                          unsigned char *save_data_out,
                          size_t save_data_capacity,
                          size_t *save_data_size_out,
                          int *revision_out,
                          char *error_out,
                          size_t error_out_size)
{
    char path[160];
    snprintf(path, sizeof(path), "/saves/%s", save_id);
    return download_save_path(server_url,
                              token,
                              path,
                              save_data_out,
                              save_data_capacity,
                              save_data_size_out,
                              revision_out,
                              error_out,
                              error_out_size);
}

int integral_api_download_n64_runtime_save(const char *server_url,
                                      const char *token,
                                      const char *media_session_id,
                                      const char *kind,
                                      unsigned char *save_data_out,
                                      size_t save_data_capacity,
                                      size_t *save_data_size_out,
                                      int *revision_out,
                                      char *error_out,
                                      size_t error_out_size)
{
    if (!media_session_id || !media_session_id[0] ||
        (!kind || (strcmp(kind, "n64") != 0 && strcmp(kind, "host-gb") != 0 &&
                   strcmp(kind, "remote-gb") != 0))) {
        set_error(error_out, error_out_size, "N64 RUNTIME SAVE REQUEST INVALID");
        return -1;
    }
    for (const unsigned char *cursor = (const unsigned char *)media_session_id;
         *cursor;
         cursor++) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') {
            set_error(error_out, error_out_size, "N64 MEDIA SESSION ID INVALID");
            return -1;
        }
    }
    char path[256];
    snprintf(path,
             sizeof(path),
             "/n64-runtime-media-sessions/%s/runtime-saves/%s",
             media_session_id,
             kind);
    return download_save_path(server_url,
                              token,
                              path,
                              save_data_out,
                              save_data_capacity,
                              save_data_size_out,
                              revision_out,
                              error_out,
                              error_out_size);
}

int integral_api_upload_save(const char *server_url,
                        const char *token,
                        const char *save_id,
                        int expected_revision,
                        const unsigned char *save_data,
                        size_t save_data_size,
                        int *revision_out,
                        char *error_out,
                        size_t error_out_size)
{
    return integral_api_upload_save_fenced(server_url,
                                      token,
                                      save_id,
                                      expected_revision,
                                      save_data,
                                      save_data_size,
                                      NULL,
                                      0,
                                      NULL,
                                      revision_out,
                                      error_out,
                                      error_out_size);
}

int integral_api_upload_save_with_request_id(const char *server_url,
                                        const char *token,
                                        const char *save_id,
                                        int expected_revision,
                                        const unsigned char *save_data,
                                        size_t save_data_size,
                                        const char *request_id,
                                        int *revision_out,
                                        char *error_out,
                                        size_t error_out_size)
{
    return integral_api_upload_save_fenced(server_url,
                                      token,
                                      save_id,
                                      expected_revision,
                                      save_data,
                                      save_data_size,
                                      NULL,
                                      0,
                                      request_id,
                                      revision_out,
                                      error_out,
                                      error_out_size);
}

int integral_api_upload_save_fenced(const char *server_url,
                               const char *token,
                               const char *save_id,
                               int expected_revision,
                               const unsigned char *save_data,
                               size_t save_data_size,
                               const char *game_session_id,
                               long long fencing_token,
                               const char *request_id,
                               int *revision_out,
                               char *error_out,
                               size_t error_out_size)
{
    *revision_out = 0;
    if (save_data_size > INTEGRAL_MAX_SAVE_BYTES) {
        set_error(error_out, error_out_size, "SAVE DATA TOO LARGE");
        return -1;
    }
    size_t encoded_size = ((save_data_size + 2) / 3) * 4 + 1;
    char *encoded = malloc(encoded_size);
    if (!encoded) {
        set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    base64_encode(save_data, save_data_size, encoded, encoded_size);

    size_t body_size = encoded_size + 512;
    char *body = malloc(body_size);
    if (!body) {
        free(encoded);
        set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    if (game_session_id && game_session_id[0] != '\0' && fencing_token > 0) {
        snprintf(body,
                 body_size,
                 "{\"expected_revision\":%d,\"save_data\":\"%s\",\"game_session_id\":\"%s\",\"fencing_token\":%lld,\"request_id\":\"%s\"}",
                 expected_revision,
                 encoded,
                 game_session_id,
                 fencing_token,
                 request_id ? request_id : "");
    }
    else {
        snprintf(body,
                 body_size,
                 "{\"expected_revision\":%d,\"save_data\":\"%s\",\"request_id\":\"%s\"}",
                 expected_revision,
                 encoded,
                 request_id ? request_id : "");
    }
    free(encoded);

    char path[160];
    snprintf(path, sizeof(path), "/saves/%s", save_id);
    char response[8192];
    int rc = api_put_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
    free(body);
    if (rc != 0) {
        return -1;
    }
    if (extract_json_int(response, "revision", revision_out) != 0) {
        set_error(error_out, error_out_size, "SAVE UPLOAD RESPONSE MISSING REVISION");
        return -1;
    }
    return 0;
}

int integral_api_apply_rom_slot(const char *server_url,
                           const char *token,
                           unsigned slot,
                           const char *filename,
                           const char *sha256,
                           const char *sha1,
                           const char *platform,
                           const char *region,
                           const char *rom_header_title,
                           const unsigned char *initial_save_data,
                           size_t initial_save_data_size,
                           int confirm_delete_saves,
                           char *rom_id_out,
                           size_t rom_id_out_size,
                           char *save_id_out,
                           size_t save_id_out_size,
                           int *requires_confirmation_out,
                           char *error_out,
                           size_t error_out_size)
{
    rom_id_out[0] = '\0';
    save_id_out[0] = '\0';
    *requires_confirmation_out = 0;
    char escaped_filename[256];
    json_escape(filename, escaped_filename, sizeof(escaped_filename));
    char escaped_header_title[64];
    json_escape(rom_header_title ? rom_header_title : "", escaped_header_title, sizeof(escaped_header_title));
    char *initial_save_encoded = NULL;
    size_t initial_save_encoded_size = 0;
    if (initial_save_data && initial_save_data_size > 0) {
        if (initial_save_data_size > INTEGRAL_MAX_SAVE_BYTES) {
            set_error(error_out, error_out_size, "INITIAL SAVE DATA TOO LARGE");
            return -1;
        }
        initial_save_encoded_size = ((initial_save_data_size + 2) / 3) * 4 + 1;
        initial_save_encoded = malloc(initial_save_encoded_size);
        if (!initial_save_encoded) {
            set_error(error_out, error_out_size, "OUT OF MEMORY");
            return -1;
        }
        base64_encode(initial_save_data, initial_save_data_size, initial_save_encoded, initial_save_encoded_size);
    }
    size_t body_size = 768 + (initial_save_encoded ? initial_save_encoded_size + 32 : 0);
    char *body = malloc(body_size);
    if (!body) {
        free(initial_save_encoded);
        set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    if (initial_save_encoded) {
        snprintf(body,
                 body_size,
                 "{\"confirm_delete_saves\":%s,\"slots\":[{\"slot\":%u,\"filename\":\"%s\",\"sha256\":\"%s\",\"sha1\":\"%s\",\"platform\":\"%s\",\"region\":\"%s\",\"rom_header_title\":\"%s\",\"initial_save_data\":\"%s\"}]}",
                 confirm_delete_saves ? "true" : "false",
                 slot,
                 escaped_filename,
                 sha256,
                 sha1,
                 platform,
                 region,
                 escaped_header_title,
                 initial_save_encoded);
    }
    else {
        snprintf(body,
                 body_size,
                 "{\"confirm_delete_saves\":%s,\"slots\":[{\"slot\":%u,\"filename\":\"%s\",\"sha256\":\"%s\",\"sha1\":\"%s\",\"platform\":\"%s\",\"region\":\"%s\",\"rom_header_title\":\"%s\"}]}",
                 confirm_delete_saves ? "true" : "false",
                 slot,
                 escaped_filename,
                 sha256,
                 sha1,
                 platform,
                 region,
                 escaped_header_title);
    }
    free(initial_save_encoded);
    char response[8192];
    if (api_post_json(server_url, "/rom-slots/apply", token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        free(body);
        return -1;
    }
    free(body);
    (void)extract_json_bool(response, "requires_confirmation", requires_confirmation_out);
    if (*requires_confirmation_out) {
        return 0;
    }
    IntegralApiRomSlot response_slots[8];
    if (slot == 0u || slot > sizeof(response_slots) / sizeof(response_slots[0]) ||
        parse_rom_slots_response(response, response_slots,
                                 sizeof(response_slots) / sizeof(response_slots[0]),
                                 error_out, error_out_size) != 0 ||
        response_slots[slot - 1u].rom_id[0] == '\0' ||
        response_slots[slot - 1u].save_id[0] == '\0') {
        set_error(error_out, error_out_size, "ROM SLOT APPLY RESPONSE MISSING IDS");
        return -1;
    }
    copy_text(rom_id_out, rom_id_out_size, response_slots[slot - 1u].rom_id);
    copy_text(save_id_out, save_id_out_size, response_slots[slot - 1u].save_id);
    return 0;
}

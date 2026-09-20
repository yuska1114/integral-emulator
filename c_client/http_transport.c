/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_transport.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define INTEGRAL_HTTP_TIMEOUT_SECONDS 15

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


int parse_http_url(const char *url, ParsedUrl *parsed, char *error_out, size_t error_out_size)
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

void connection_close(IntegralConnection *connection)
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

int connection_open(const ParsedUrl *parsed, IntegralConnection *connection, char *error_out, size_t error_out_size)
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

int connection_send_all(IntegralConnection *connection, const char *data, size_t len)
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

int connection_read_response(IntegralConnection *connection,
                                    char *buffer,
                                    size_t buffer_size)
{
    return connection_read_response_sized(
        connection, buffer, buffer_size, NULL);
}

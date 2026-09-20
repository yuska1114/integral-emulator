/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_HTTP_TRANSPORT_H
#define INTEGRAL_HTTP_TRANSPORT_H
/* Private to HTTP communication code; API modules must not include this. */
#include <stddef.h>
#include <stdbool.h>
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

int parse_http_url(const char *url, ParsedUrl *parsed, char *error_out, size_t error_out_size);
void connection_close(IntegralConnection *connection);
int connection_open(const ParsedUrl *parsed, IntegralConnection *connection, char *error_out, size_t error_out_size);
int connection_send_all(IntegralConnection *connection, const char *data, size_t len);
int connection_read_response(IntegralConnection *connection, char *buffer, size_t buffer_size);
#endif

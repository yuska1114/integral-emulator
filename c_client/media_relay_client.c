/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_relay_client.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#ifdef INTEGRAL_USE_OPENSSL
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#endif

#define INTEGRAL_MEDIA_TIMEOUT_SECONDS 10
#define INTEGRAL_MEDIA_LINE_MAX 512
#define INTEGRAL_MEDIA_MAGIC "N64RUNTIME1"
#define INTEGRAL_MEDIA_CONTROL_MAGIC "N64R"
#define INTEGRAL_MEDIA_CONTROL_VERSION 2u
#define INTEGRAL_MEDIA_CONTROL_INPUT 1u
#define INTEGRAL_MEDIA_CONTROL_HEADER_SIZE 16u
#define INTEGRAL_MEDIA_CONTROL_INPUT_SIZE 8u
#define INTEGRAL_MEDIA_CONTROLLER_MASK 0x3ffffu
#define INTEGRAL_MEDIA_RECEIVE_CAPACITY (INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + INTEGRAL_MEDIA_MAX_H264_FRAME_BYTES)

#ifdef _WIN32
typedef SOCKET IntegralMediaSocket;
#define INTEGRAL_MEDIA_INVALID_SOCKET INVALID_SOCKET
#else
typedef int IntegralMediaSocket;
#define INTEGRAL_MEDIA_INVALID_SOCKET (-1)
#endif

struct IntegralMediaRelayConnection {
    IntegralMediaSocket sock;
#ifdef INTEGRAL_USE_OPENSSL
    SSL_CTX *ctx;
    SSL *ssl;
#endif
    bool use_tls;
    char session_id[96];
    char role[8];
    unsigned char *receive_buffer;
    size_t receive_capacity;
    size_t receive_used;
    unsigned char *send_buffer;
    size_t send_size;
    size_t send_offset;
    bool paired;
    bool controller_sequence_seen;
    uint32_t last_controller_sequence;
};

#ifdef INTEGRAL_USE_OPENSSL
static uint32_t read_be32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24u) | ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) | (uint32_t)data[3];
}

static uint16_t read_be16(const unsigned char *data)
{
    return (uint16_t)((uint16_t)data[0] << 8u) | data[1];
}

static uint64_t read_be64(const unsigned char *data)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; index++) value = (value << 8u) | data[index];
    return value;
}

static void write_be32(unsigned char *data, uint32_t value)
{
    data[0] = (unsigned char)(value >> 24u);
    data[1] = (unsigned char)(value >> 16u);
    data[2] = (unsigned char)(value >> 8u);
    data[3] = (unsigned char)value;
}

static void write_be16(unsigned char *data, uint16_t value)
{
    data[0] = (unsigned char)(value >> 8u);
    data[1] = (unsigned char)value;
}

static void write_be64(unsigned char *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) {
        data[index] = (unsigned char)value;
        value >>= 8u;
    }
}
#endif

static void copy_error(char *out, size_t out_size, const char *message)
{
    if (!out || out_size == 0) return;
    snprintf(out, out_size, "%s", message ? message : "MEDIA RELAY ERROR");
}

#ifdef _WIN32
#ifdef INTEGRAL_USE_OPENSSL
static int ensure_sockets(void)
{
    static bool ready = false;
    if (ready) return 0;
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return -1;
    ready = true;
    return 0;
}
#endif

static void close_socket(IntegralMediaSocket sock)
{
    if (sock != INTEGRAL_MEDIA_INVALID_SOCKET) closesocket(sock);
}
#else
#ifdef INTEGRAL_USE_OPENSSL
static int ensure_sockets(void)
{
    return 0;
}
#endif

static void close_socket(IntegralMediaSocket sock)
{
    if (sock != INTEGRAL_MEDIA_INVALID_SOCKET) close(sock);
}
#endif

static bool field_is_safe(const char *value)
{
    if (!value || !value[0]) return false;
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p <= 0x20 || *p >= 0x7f) return false;
    }
    return true;
}

#ifdef INTEGRAL_USE_OPENSSL
static void set_socket_timeouts(IntegralMediaSocket sock, unsigned seconds)
{
#ifdef _WIN32
    DWORD timeout_ms = seconds * 1000u;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeout_ms, sizeof(timeout_ms));
#else
    struct timeval timeout = {.tv_sec = seconds, .tv_usec = 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#endif
}

static IntegralMediaSocket connect_tcp(const char *host, unsigned port, unsigned seconds)
{
    if (ensure_sockets() != 0) return INTEGRAL_MEDIA_INVALID_SOCKET;
    char port_text[16];
    snprintf(port_text, sizeof(port_text), "%u", port);
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *results = NULL;
    if (getaddrinfo(host, port_text, &hints, &results) != 0) return INTEGRAL_MEDIA_INVALID_SOCKET;
    IntegralMediaSocket sock = INTEGRAL_MEDIA_INVALID_SOCKET;
    for (struct addrinfo *item = results; item; item = item->ai_next) {
        sock = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (sock == INTEGRAL_MEDIA_INVALID_SOCKET) continue;
#ifdef _WIN32
        u_long nonblocking = 1;
        ioctlsocket(sock, FIONBIO, &nonblocking);
#else
        int flags = fcntl(sock, F_GETFL, 0);
        fcntl(sock, F_SETFL, flags | O_NONBLOCK);
#endif
        int connected = connect(sock, item->ai_addr, (int)item->ai_addrlen);
        if (connected != 0) {
            fd_set writes; FD_ZERO(&writes); FD_SET(sock, &writes);
            struct timeval timeout = {.tv_sec = seconds, .tv_usec = 0};
            int socket_error = 0;
#ifdef _WIN32
            int size = sizeof(socket_error);
#else
            socklen_t size = sizeof(socket_error);
#endif
            connected = select((int)sock + 1, NULL, &writes, NULL, &timeout) > 0 &&
                getsockopt(sock, SOL_SOCKET, SO_ERROR, (void *)&socket_error, &size) == 0 &&
                socket_error == 0 ? 0 : -1;
        }
#ifdef _WIN32
        nonblocking = 0; ioctlsocket(sock, FIONBIO, &nonblocking);
#else
        fcntl(sock, F_SETFL, flags);
#endif
        if (connected == 0) break;
        close_socket(sock);
        sock = INTEGRAL_MEDIA_INVALID_SOCKET;
    }
    freeaddrinfo(results);
    if (sock != INTEGRAL_MEDIA_INVALID_SOCKET) set_socket_timeouts(sock, seconds);
    return sock;
}
#endif

#ifdef INTEGRAL_USE_OPENSSL
static bool socket_would_block(void)
{
#ifdef _WIN32
    int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

static int transport_write(IntegralMediaRelayConnection *connection,
                           const void *data,
                           int size,
                           bool *would_block)
{
    *would_block = false;
    if (connection->use_tls) {
        int count = SSL_write(connection->ssl, data, size);
        if (count > 0) return count;
        int error = SSL_get_error(connection->ssl, count);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            *would_block = true;
            return 0;
        }
        return -1;
    }
#ifdef _WIN32
    int count = send(connection->sock, (const char *)data, size, 0);
    if (count == SOCKET_ERROR) {
#else
    int count = (int)send(connection->sock, data, (size_t)size, 0);
    if (count < 0) {
#endif
        if (socket_would_block()) {
            *would_block = true;
            return 0;
        }
        return -1;
    }
    return count > 0 ? count : -1;
}

static int transport_read(IntegralMediaRelayConnection *connection,
                          void *data,
                          int size,
                          bool *would_block)
{
    *would_block = false;
    if (connection->use_tls) {
        int count = SSL_read(connection->ssl, data, size);
        if (count > 0) return count;
        int error = SSL_get_error(connection->ssl, count);
        if (error == SSL_ERROR_WANT_READ || error == SSL_ERROR_WANT_WRITE) {
            *would_block = true;
            return 0;
        }
        return -1;
    }
#ifdef _WIN32
    int count = recv(connection->sock, (char *)data, size, 0);
    if (count == SOCKET_ERROR) {
#else
    int count = (int)recv(connection->sock, data, (size_t)size, 0);
    if (count < 0) {
#endif
        if (socket_would_block()) {
            *would_block = true;
            return 0;
        }
        return -1;
    }
    return count > 0 ? count : -1;
}

static int transport_send_all(IntegralMediaRelayConnection *connection,
                              const char *data,
                              size_t size)
{
    size_t sent = 0;
    while (sent < size) {
        bool would_block = false;
        int count = transport_write(connection, data + sent,
                                    (int)(size - sent), &would_block);
        if (count <= 0) return -1;
        sent += (size_t)count;
    }
    return 0;
}

static int transport_read_line(IntegralMediaRelayConnection *connection,
                               char *out,
                               size_t out_size)
{
    size_t used = 0;
    while (used + 1 < out_size) {
        char ch;
        bool would_block = false;
        int count = transport_read(connection, &ch, 1, &would_block);
        if (count != 1) return -1;
        if (ch == '\n') {
            out[used] = '\0';
            return 0;
        }
        if (ch != '\r') out[used++] = ch;
    }
    return -1;
}

static int set_nonblocking(IntegralMediaSocket sock)
{
#ifdef _WIN32
    u_long enabled = 1;
    return ioctlsocket(sock, FIONBIO, &enabled) == 0 ? 0 : -1;
#else
    int flags = fcntl(sock, F_GETFL, 0);
    return flags >= 0 && fcntl(sock, F_SETFL, flags | O_NONBLOCK) == 0 ? 0 : -1;
#endif
}

static int flush_send_buffer(IntegralMediaRelayConnection *connection,
                             char *error_out,
                             size_t error_out_size)
{
    while (connection->send_offset < connection->send_size) {
        size_t remaining = connection->send_size - connection->send_offset;
        int request = remaining > (size_t)INT_MAX ? INT_MAX : (int)remaining;
        bool would_block = false;
        int count = transport_write(connection,
                                    connection->send_buffer + connection->send_offset,
                                    request, &would_block);
        if (count > 0) {
            connection->send_offset += (size_t)count;
            continue;
        }
        if (would_block) return 0;
        copy_error(error_out, error_out_size, "MEDIA FRAME SEND FAILED");
        return -1;
    }
    free(connection->send_buffer);
    connection->send_buffer = NULL;
    connection->send_size = 0;
    connection->send_offset = 0;
    return 1;
}
#endif

void integral_media_relay_close(IntegralMediaRelayConnection *connection)
{
    if (!connection) return;
#ifdef INTEGRAL_USE_OPENSSL
    if (connection->ssl) {
        SSL_shutdown(connection->ssl);
        SSL_free(connection->ssl);
    }
    if (connection->ctx) SSL_CTX_free(connection->ctx);
#endif
    close_socket(connection->sock);
    free(connection->receive_buffer);
    free(connection->send_buffer);
    memset(connection, 0, sizeof(*connection));
    free(connection);
}

int integral_media_relay_connect(const char *host,
                            unsigned port,
                            const char *transport,
                            const char *session_id,
                            const char *role,
                            const char *scope,
                            const char *ticket,
                            const char *ca_file,
                            IntegralMediaRelayConnection **connection_out,
                            char *error_out,
                            size_t error_out_size)
{
    return integral_media_relay_connect_timeout(host, port, transport, session_id, role,
        scope, ticket, ca_file, connection_out, error_out, error_out_size,
        INTEGRAL_MEDIA_TIMEOUT_SECONDS);
}

int integral_media_relay_connect_timeout(const char *host, unsigned port,
    const char *transport, const char *session_id, const char *role, const char *scope,
    const char *ticket, const char *ca_file, IntegralMediaRelayConnection **connection_out,
    char *error_out, size_t error_out_size, unsigned timeout_seconds)
{
    if (connection_out) *connection_out = NULL;
    if (!connection_out || timeout_seconds < 1 || timeout_seconds > 10 ||
        !field_is_safe(host) || port == 0 || port > 65535 ||
        (!transport || (strcmp(transport, "tls") != 0 && strcmp(transport, "plain") != 0)) ||
        !field_is_safe(session_id) || !field_is_safe(role) || !field_is_safe(scope) ||
        !field_is_safe(ticket)) {
        copy_error(error_out, error_out_size, "MEDIA RELAY PARAMETERS INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    (void)ca_file;
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    IntegralMediaRelayConnection *connection = calloc(1, sizeof(*connection));
    if (!connection) {
        copy_error(error_out, error_out_size, "MEDIA RELAY MEMORY FAILED");
        return -1;
    }
    connection->sock = INTEGRAL_MEDIA_INVALID_SOCKET;
    connection->use_tls = strcmp(transport, "tls") == 0;
    connection->receive_capacity = INTEGRAL_MEDIA_RECEIVE_CAPACITY;
    connection->receive_buffer = malloc(connection->receive_capacity);
    if (!connection->receive_buffer) {
        copy_error(error_out, error_out_size, "MEDIA RELAY MEMORY FAILED");
        integral_media_relay_close(connection);
        return -1;
    }
    connection->sock = connect_tcp(host, port, timeout_seconds);
    if (connection->sock == INTEGRAL_MEDIA_INVALID_SOCKET) {
        copy_error(error_out, error_out_size, "MEDIA RELAY TCP FAILED");
        integral_media_relay_close(connection);
        return -1;
    }
    if (connection->use_tls) {
        connection->ctx = SSL_CTX_new(TLS_client_method());
        if (!connection->ctx || SSL_CTX_set_min_proto_version(connection->ctx, TLS1_3_VERSION) != 1) {
            copy_error(error_out, error_out_size, "MEDIA RELAY TLS SETUP FAILED");
            integral_media_relay_close(connection);
            return -1;
        }
        int trust_loaded = ca_file && ca_file[0]
                               ? SSL_CTX_load_verify_locations(connection->ctx, ca_file, NULL)
                               : SSL_CTX_set_default_verify_paths(connection->ctx);
        if (trust_loaded != 1) {
            copy_error(error_out, error_out_size, "MEDIA RELAY CA LOAD FAILED");
            integral_media_relay_close(connection);
            return -1;
        }
        SSL_CTX_set_verify(connection->ctx, SSL_VERIFY_PEER, NULL);
        connection->ssl = SSL_new(connection->ctx);
        if (!connection->ssl || SSL_set_fd(connection->ssl, (int)connection->sock) != 1 ||
            SSL_set_tlsext_host_name(connection->ssl, host) != 1) {
            copy_error(error_out, error_out_size, "MEDIA RELAY TLS SESSION FAILED");
            integral_media_relay_close(connection);
            return -1;
        }
        X509_VERIFY_PARAM *verify = SSL_get0_param(connection->ssl);
        X509_VERIFY_PARAM_set_hostflags(verify, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (X509_VERIFY_PARAM_set1_host(verify, host, 0) != 1 || SSL_connect(connection->ssl) != 1 ||
            SSL_get_verify_result(connection->ssl) != X509_V_OK) {
            copy_error(error_out, error_out_size, "MEDIA RELAY CERTIFICATE REJECTED");
            integral_media_relay_close(connection);
            return -1;
        }
    }
    char handshake[INTEGRAL_MEDIA_LINE_MAX];
    int handshake_size = snprintf(handshake,
                                  sizeof(handshake),
                                  "%s %s %s %s %s\n",
                                  INTEGRAL_MEDIA_MAGIC,
                                  session_id,
                                  role,
                                  scope,
                                  ticket);
    if (handshake_size <= 0 || (size_t)handshake_size >= sizeof(handshake) ||
        transport_send_all(connection, handshake, (size_t)handshake_size) != 0) {
        copy_error(error_out, error_out_size, "MEDIA RELAY AUTH SEND FAILED");
        integral_media_relay_close(connection);
        return -1;
    }
    memset(handshake, 0, sizeof(handshake));
    char response[INTEGRAL_MEDIA_LINE_MAX];
    char expected[INTEGRAL_MEDIA_LINE_MAX];
    snprintf(expected, sizeof(expected), "%s AUTHENTICATED %s %s", INTEGRAL_MEDIA_MAGIC, session_id, role);
    if (transport_read_line(connection, response, sizeof(response)) != 0 || strcmp(response, expected) != 0) {
        copy_error(error_out, error_out_size, "MEDIA RELAY AUTH REJECTED");
        integral_media_relay_close(connection);
        return -1;
    }
    if (set_nonblocking(connection->sock) != 0) {
        copy_error(error_out, error_out_size, "MEDIA RELAY NONBLOCK FAILED");
        integral_media_relay_close(connection);
        return -1;
    }
    snprintf(connection->session_id, sizeof(connection->session_id), "%s", session_id);
    snprintf(connection->role, sizeof(connection->role), "%s", role);
    *connection_out = connection;
    copy_error(error_out, error_out_size, "");
    return 0;
#endif
}

int integral_media_relay_poll(IntegralMediaRelayConnection *connection,
                         char *error_out,
                         size_t error_out_size)
{
    if (!connection) {
        copy_error(error_out, error_out_size, "MEDIA RELAY NOT CONNECTED");
        return -1;
    }
    if (connection->paired) return 1;
#ifndef INTEGRAL_USE_OPENSSL
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    while (connection->receive_used + 1 < connection->receive_capacity) {
        bool would_block = false;
        int count = transport_read(
            connection,
            connection->receive_buffer + connection->receive_used,
            (int)(connection->receive_capacity - connection->receive_used - 1),
            &would_block);
        if (count > 0) {
            connection->receive_used += (size_t)count;
            connection->receive_buffer[connection->receive_used] = '\0';
            unsigned char *newline = memchr(connection->receive_buffer, '\n', connection->receive_used);
            if (!newline) continue;
            *newline = '\0';
            if (newline > connection->receive_buffer && newline[-1] == '\r') newline[-1] = '\0';
            char expected[INTEGRAL_MEDIA_LINE_MAX];
            snprintf(expected, sizeof(expected), "%s PAIRED %s", INTEGRAL_MEDIA_MAGIC, connection->session_id);
            if (strcmp((const char *)connection->receive_buffer, expected) != 0) {
                copy_error(error_out, error_out_size, "MEDIA RELAY STATUS INVALID");
                return -1;
            }
            size_t consumed = (size_t)(newline - connection->receive_buffer) + 1u;
            size_t remaining = connection->receive_used - consumed;
            memmove(connection->receive_buffer, connection->receive_buffer + consumed, remaining);
            connection->receive_used = remaining;
            connection->paired = true;
            return 1;
        }
        if (would_block) return 0;
        copy_error(error_out, error_out_size, "MEDIA RELAY CONNECTION CLOSED");
        return -1;
    }
    copy_error(error_out, error_out_size, "MEDIA RELAY STATUS TOO LARGE");
    return -1;
#endif
}

int integral_media_relay_send_controller(IntegralMediaRelayConnection *connection,
                                    uint32_t sequence,
                                    uint64_t buttons,
                                    char *error_out,
                                    size_t error_out_size)
{
    if (!connection || !connection->paired || strcmp(connection->role, "remote") != 0 ||
        (buttons & ~((uint64_t)INTEGRAL_MEDIA_CONTROLLER_MASK)) != 0u) {
        copy_error(error_out, error_out_size, "MEDIA CONTROLLER STATE INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    (void)sequence;
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    unsigned char frame[INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + INTEGRAL_MEDIA_CONTROL_INPUT_SIZE] = {0};
    memcpy(frame, INTEGRAL_MEDIA_CONTROL_MAGIC, 4u);
    frame[4] = INTEGRAL_MEDIA_CONTROL_VERSION;
    frame[5] = INTEGRAL_MEDIA_CONTROL_INPUT;
    write_be32(frame + 8u, sequence);
    write_be32(frame + 12u, INTEGRAL_MEDIA_CONTROL_INPUT_SIZE);
    write_be64(frame + INTEGRAL_MEDIA_CONTROL_HEADER_SIZE, buttons);
    bool would_block = false;
    int count = transport_write(connection, frame, (int)sizeof(frame), &would_block);
    if (count == (int)sizeof(frame)) return 1;
    if (would_block) return 0;
    copy_error(error_out, error_out_size, "MEDIA CONTROLLER SEND FAILED");
    return -1;
#endif
}

int integral_media_relay_poll_controller(IntegralMediaRelayConnection *connection,
                                    uint32_t *sequence_out,
                                    uint64_t *buttons_out,
                                    char *error_out,
                                    size_t error_out_size)
{
    if (!connection || !connection->paired || strcmp(connection->role, "host") != 0 ||
        !sequence_out || !buttons_out) {
        copy_error(error_out, error_out_size, "MEDIA CONTROLLER POLL INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    while (connection->receive_used < connection->receive_capacity) {
        if (connection->receive_used >= INTEGRAL_MEDIA_CONTROL_HEADER_SIZE) {
            const unsigned char *frame = (const unsigned char *)connection->receive_buffer;
            uint32_t payload_size = read_be32(frame + 12u);
            if (memcmp(frame, INTEGRAL_MEDIA_CONTROL_MAGIC, 4u) != 0 ||
                frame[4] != INTEGRAL_MEDIA_CONTROL_VERSION || frame[5] != INTEGRAL_MEDIA_CONTROL_INPUT ||
                frame[6] != 0u || frame[7] != 0u || payload_size != INTEGRAL_MEDIA_CONTROL_INPUT_SIZE) {
                copy_error(error_out, error_out_size, "MEDIA CONTROLLER FRAME INVALID");
                return -1;
            }
            size_t frame_size = INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + payload_size;
            if (connection->receive_used >= frame_size) {
                uint64_t buttons = read_be64(frame + INTEGRAL_MEDIA_CONTROL_HEADER_SIZE);
                if ((buttons & ~((uint64_t)INTEGRAL_MEDIA_CONTROLLER_MASK)) != 0u) {
                    uint32_t sequence = read_be32(frame + 8u);
                    size_t remaining = connection->receive_used - frame_size;
                    memmove(connection->receive_buffer,
                            connection->receive_buffer + frame_size,
                            remaining);
                    connection->receive_used = remaining;
                    if (error_out && error_out_size > 0) {
                        snprintf(error_out,
                                 error_out_size,
                                 "MEDIA CONTROLLER FRAME DROPPED sequence=%u buttons=0x%016llx",
                                 sequence,
                                 (unsigned long long)buttons);
                    }
                    return 2;
                }
                uint32_t sequence = read_be32(frame + 8u);
                size_t remaining = connection->receive_used - frame_size;
                memmove(connection->receive_buffer, connection->receive_buffer + frame_size, remaining);
                connection->receive_used = remaining;
                if (connection->controller_sequence_seen &&
                    (int32_t)(sequence - connection->last_controller_sequence) <= 0) {
                    continue;
                }
                connection->controller_sequence_seen = true;
                connection->last_controller_sequence = sequence;
                *sequence_out = sequence;
                *buttons_out = buttons;
                return 1;
            }
        }
        bool would_block = false;
        int count = transport_read(
            connection,
            connection->receive_buffer + connection->receive_used,
            (int)(connection->receive_capacity - connection->receive_used),
            &would_block);
        if (count > 0) {
            connection->receive_used += (size_t)count;
            continue;
        }
        if (would_block) return 0;
        copy_error(error_out, error_out_size, "MEDIA CONTROLLER CONNECTION CLOSED");
        return -1;
    }
    copy_error(error_out, error_out_size, "MEDIA CONTROLLER BUFFER FULL");
    return -1;
#endif
}

static uint32_t control_payload_size_for_role(const IntegralMediaRelayConnection *connection,
                                              uint8_t message_type)
{
    if (!connection) return 0;
    if (strcmp(connection->role, "remote") == 0) {
        if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT) return INTEGRAL_MEDIA_GB_INPUT_BYTES;
        if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_PING) return INTEGRAL_MEDIA_GB_PING_BYTES;
        if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL_ACK) return INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
        return 0;
    }
    if (strcmp(connection->role, "host") == 0) {
        if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK) return INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES;
        if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_PONG) return INTEGRAL_MEDIA_GB_PING_BYTES;
        if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL) return INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
    }
    return 0;
}

int integral_media_relay_send_control(IntegralMediaRelayConnection *connection,
                                      uint8_t message_type,
                                      uint32_t sequence,
                                      const void *payload,
                                      uint32_t payload_size,
                                      char *error_out,
                                      size_t error_out_size)
{
    uint32_t expected = control_payload_size_for_role(connection, message_type);
    if (!connection || !connection->paired || !payload || !expected || payload_size != expected) {
        copy_error(error_out, error_out_size, "MEDIA CONTROL SEND INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    (void)sequence;
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    if (connection->send_buffer) {
        int flushed = flush_send_buffer(connection, error_out, error_out_size);
        if (flushed <= 0) return flushed;
    }
    size_t frame_size = INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + payload_size;
    connection->send_buffer = malloc(frame_size);
    if (!connection->send_buffer) {
        copy_error(error_out, error_out_size, "MEDIA CONTROL MEMORY FAILED");
        return -1;
    }
    memcpy(connection->send_buffer, INTEGRAL_MEDIA_CONTROL_MAGIC, 4u);
    connection->send_buffer[4] = INTEGRAL_MEDIA_CONTROL_VERSION;
    connection->send_buffer[5] = message_type;
    write_be16(connection->send_buffer + 6u, 0u);
    write_be32(connection->send_buffer + 8u, sequence);
    write_be32(connection->send_buffer + 12u, payload_size);
    memcpy(connection->send_buffer + INTEGRAL_MEDIA_CONTROL_HEADER_SIZE, payload, payload_size);
    connection->send_size = frame_size;
    connection->send_offset = 0;
    int flushed = flush_send_buffer(connection, error_out, error_out_size);
    /* A newly queued control is owned by the connection even when the socket
       would block. Distinguish that from the earlier-frame pending return. */
    return flushed == 0 ? 1 : flushed;
#endif
}

int integral_media_relay_poll_control(IntegralMediaRelayConnection *connection,
                                      uint8_t *message_type_out,
                                      uint32_t *sequence_out,
                                      void *payload_out,
                                      size_t payload_capacity,
                                      uint32_t *payload_size_out,
                                      char *error_out,
                                      size_t error_out_size)
{
    if (!connection || !connection->paired || !message_type_out || !sequence_out ||
        !payload_out || !payload_size_out) {
        copy_error(error_out, error_out_size, "MEDIA CONTROL POLL INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    (void)payload_capacity;
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    while (connection->receive_used < connection->receive_capacity) {
        if (connection->receive_used >= INTEGRAL_MEDIA_CONTROL_HEADER_SIZE) {
            const unsigned char *frame = connection->receive_buffer;
            uint8_t message_type = frame[5];
            uint32_t payload_size = read_be32(frame + 12u);
            uint32_t expected = 0;
            if (strcmp(connection->role, "host") == 0) {
                if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT) expected = INTEGRAL_MEDIA_GB_INPUT_BYTES;
                else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_PING) expected = INTEGRAL_MEDIA_GB_PING_BYTES;
                else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL_ACK) expected = INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
                else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE) expected = INTEGRAL_MEDIA_GB_SESSION_STATE_BYTES;
            }
            else {
                if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK) expected = INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES;
                else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_PONG) expected = INTEGRAL_MEDIA_GB_PING_BYTES;
                else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL) expected = INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
                else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE) expected = INTEGRAL_MEDIA_GB_SESSION_STATE_BYTES;
            }
            if (memcmp(frame, INTEGRAL_MEDIA_CONTROL_MAGIC, 4u) != 0 ||
                frame[4] != INTEGRAL_MEDIA_CONTROL_VERSION || read_be16(frame + 6u) != 0u ||
                !expected || payload_size != expected) {
                copy_error(error_out, error_out_size, "MEDIA CONTROL FRAME INVALID");
                return -1;
            }
            size_t frame_size = INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + payload_size;
            if (connection->receive_used >= frame_size) {
                if (payload_capacity < payload_size) {
                    copy_error(error_out, error_out_size, "MEDIA CONTROL OUTPUT TOO SMALL");
                    return -1;
                }
                memcpy(payload_out, frame + INTEGRAL_MEDIA_CONTROL_HEADER_SIZE, payload_size);
                *message_type_out = message_type;
                *sequence_out = read_be32(frame + 8u);
                *payload_size_out = payload_size;
                size_t remaining = connection->receive_used - frame_size;
                memmove(connection->receive_buffer, connection->receive_buffer + frame_size, remaining);
                connection->receive_used = remaining;
                return 1;
            }
        }
        bool would_block = false;
        int count = transport_read(
            connection,
            connection->receive_buffer + connection->receive_used,
            (int)(connection->receive_capacity - connection->receive_used),
            &would_block);
        if (count > 0) {
            connection->receive_used += (size_t)count;
            continue;
        }
        if (would_block) return 0;
        copy_error(error_out, error_out_size, "MEDIA CONTROL CONNECTION CLOSED");
        return -1;
    }
    copy_error(error_out, error_out_size, "MEDIA CONTROL BUFFER FULL");
    return -1;
#endif
}

int integral_media_relay_send_media(IntegralMediaRelayConnection *connection,
                               uint8_t message_type,
                               uint16_t flags,
                               uint32_t sequence,
                               const void *payload,
                               uint32_t payload_size,
                               char *error_out,
                               size_t error_out_size)
{
    if (!connection || !connection->paired || strcmp(connection->role, "host") != 0 ||
        !payload || !payload_size) {
        copy_error(error_out, error_out_size, "MEDIA FRAME SEND INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    (void)message_type;
    (void)flags;
    (void)sequence;
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    uint32_t maximum = 0;
    if (message_type == INTEGRAL_MEDIA_MESSAGE_H264_CONFIG && flags == 0) {
        maximum = INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES;
    }
    else if (message_type == INTEGRAL_MEDIA_MESSAGE_H264_FRAME &&
             (flags & ~INTEGRAL_MEDIA_FLAG_H264_KEYFRAME) == 0) {
        maximum = INTEGRAL_MEDIA_MAX_H264_FRAME_BYTES;
    }
    else if (message_type == INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM && flags == 0) {
        maximum = INTEGRAL_MEDIA_MAX_AUDIO_BYTES;
    }
    else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK && flags == 0) {
        maximum = INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES;
    }
    else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_PONG && flags == 0) {
        maximum = INTEGRAL_MEDIA_GB_PING_BYTES;
    }
    else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL && flags == 0) {
        maximum = INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
    }
    if (!maximum || payload_size > maximum) {
        copy_error(error_out, error_out_size, "MEDIA FRAME TYPE OR SIZE INVALID");
        return -1;
    }
    if (connection->send_buffer) {
        int flushed = flush_send_buffer(connection, error_out, error_out_size);
        if (flushed <= 0) return flushed;
    }
    size_t frame_size = INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + (size_t)payload_size;
    connection->send_buffer = malloc(frame_size);
    if (!connection->send_buffer) {
        copy_error(error_out, error_out_size, "MEDIA FRAME MEMORY FAILED");
        return -1;
    }
    memcpy(connection->send_buffer, INTEGRAL_MEDIA_CONTROL_MAGIC, 4);
    connection->send_buffer[4] = INTEGRAL_MEDIA_CONTROL_VERSION;
    connection->send_buffer[5] = message_type;
    write_be16(connection->send_buffer + 6, flags);
    write_be32(connection->send_buffer + 8, sequence);
    write_be32(connection->send_buffer + 12, payload_size);
    memcpy(connection->send_buffer + INTEGRAL_MEDIA_CONTROL_HEADER_SIZE, payload, payload_size);
    connection->send_size = frame_size;
    connection->send_offset = 0;
    return flush_send_buffer(connection, error_out, error_out_size);
#endif
}

int integral_media_relay_poll_media(IntegralMediaRelayConnection *connection,
                               uint8_t *message_type_out,
                               uint16_t *flags_out,
                               uint32_t *sequence_out,
                               void *payload_out,
                               size_t payload_capacity,
                               uint32_t *payload_size_out,
                               char *error_out,
                               size_t error_out_size)
{
    if (!connection || !connection->paired || strcmp(connection->role, "remote") != 0 ||
        !message_type_out || !flags_out || !sequence_out || !payload_out || !payload_size_out) {
        copy_error(error_out, error_out_size, "MEDIA FRAME POLL INVALID");
        return -1;
    }
#ifndef INTEGRAL_USE_OPENSSL
    (void)payload_capacity;
    copy_error(error_out, error_out_size, "MEDIA RELAY REQUIRES OPENSSL");
    return -1;
#else
    while (connection->receive_used < connection->receive_capacity) {
        if (connection->receive_used >= INTEGRAL_MEDIA_CONTROL_HEADER_SIZE) {
            const unsigned char *frame = connection->receive_buffer;
            uint8_t message_type = frame[5];
            uint16_t flags = read_be16(frame + 6);
            uint32_t payload_size = read_be32(frame + 12);
            uint32_t maximum = 0;
            if (message_type == INTEGRAL_MEDIA_MESSAGE_H264_CONFIG && flags == 0) {
                maximum = INTEGRAL_MEDIA_MAX_H264_CONFIG_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_H264_FRAME &&
                     (flags & ~INTEGRAL_MEDIA_FLAG_H264_KEYFRAME) == 0) {
                maximum = INTEGRAL_MEDIA_MAX_H264_FRAME_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_AUDIO_ADPCM && flags == 0) {
                maximum = INTEGRAL_MEDIA_MAX_AUDIO_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK && flags == 0) {
                maximum = INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_PONG && flags == 0) {
                maximum = INTEGRAL_MEDIA_GB_PING_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL && flags == 0) {
                maximum = INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL_ACK && flags == 0) {
                maximum = INTEGRAL_MEDIA_GB_TERMINAL_BYTES;
            }
            else if (message_type == INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE && flags == 0) {
                maximum = INTEGRAL_MEDIA_GB_SESSION_STATE_BYTES;
            }
            if (memcmp(frame, INTEGRAL_MEDIA_CONTROL_MAGIC, 4) != 0 ||
                frame[4] != INTEGRAL_MEDIA_CONTROL_VERSION || !maximum || payload_size > maximum) {
                copy_error(error_out, error_out_size, "MEDIA FRAME INVALID");
                return -1;
            }
            size_t frame_size = INTEGRAL_MEDIA_CONTROL_HEADER_SIZE + (size_t)payload_size;
            if (connection->receive_used >= frame_size) {
                if (payload_capacity < payload_size) {
                    copy_error(error_out, error_out_size, "MEDIA FRAME OUTPUT TOO SMALL");
                    return -1;
                }
                memcpy(payload_out, frame + INTEGRAL_MEDIA_CONTROL_HEADER_SIZE, payload_size);
                *message_type_out = message_type;
                *flags_out = flags;
                *sequence_out = read_be32(frame + 8);
                *payload_size_out = payload_size;
                size_t remaining = connection->receive_used - frame_size;
                memmove(connection->receive_buffer, connection->receive_buffer + frame_size, remaining);
                connection->receive_used = remaining;
                return 1;
            }
        }
        bool would_block = false;
        int count = transport_read(
            connection,
            connection->receive_buffer + connection->receive_used,
            (int)(connection->receive_capacity - connection->receive_used),
            &would_block);
        if (count > 0) {
            connection->receive_used += (size_t)count;
            continue;
        }
        if (would_block) return 0;
        copy_error(error_out, error_out_size, "MEDIA FRAME CONNECTION CLOSED");
        return -1;
    }
    copy_error(error_out, error_out_size, "MEDIA FRAME BUFFER FULL");
    return -1;
#endif
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_COMMON_NET_COMPAT_H
#define INTEGRAL_GB_RUNTIME_COMMON_NET_COMPAT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#define strcasecmp _stricmp

#ifndef _SSIZE_T_DEFINED
typedef intptr_t ssize_t;
#define _SSIZE_T_DEFINED
#endif
#ifndef socklen_t
typedef int socklen_t;
#endif

typedef uintptr_t integral_gb_runtime_socket_t;
#define INTEGRAL_GB_RUNTIME_INVALID_SOCKET ((integral_gb_runtime_socket_t)~(uintptr_t)0)
#define INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(sock) ((sock) != INTEGRAL_GB_RUNTIME_INVALID_SOCKET)
#define INTEGRAL_GB_RUNTIME_SELECT_NFDS(sock) 0
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

typedef int integral_gb_runtime_socket_t;
#define INTEGRAL_GB_RUNTIME_INVALID_SOCKET (-1)
#define INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(sock) ((sock) >= 0)
#define INTEGRAL_GB_RUNTIME_SELECT_NFDS(sock) ((sock) + 1)
#endif

int integral_gb_runtime_net_init(void);
void integral_gb_runtime_net_shutdown(void);
uint64_t integral_gb_runtime_now_us(void);
void integral_gb_runtime_sleep_ms(unsigned ms);
int integral_gb_runtime_socket_close(integral_gb_runtime_socket_t sock);
int integral_gb_runtime_socket_set_nonblocking(integral_gb_runtime_socket_t sock);
ssize_t integral_gb_runtime_socket_read(integral_gb_runtime_socket_t sock, void *dest, size_t size);
ssize_t integral_gb_runtime_socket_write(integral_gb_runtime_socket_t sock, const void *src, size_t size);
int integral_gb_runtime_socket_last_error(void);
bool integral_gb_runtime_socket_error_would_block(int error_code);
bool integral_gb_runtime_socket_error_in_progress(int error_code);
bool integral_gb_runtime_socket_error_interrupted(int error_code);
bool integral_gb_runtime_socket_error_connection_lost(int error_code);
const char *integral_gb_runtime_socket_error_string(int error_code);
bool integral_gb_runtime_detect_local_ipv4(char *out, size_t out_size);
bool integral_gb_runtime_localtime(time_t source, struct tm *dest);
integral_gb_runtime_socket_t integral_gb_runtime_tcp_connect(const char *host, unsigned port);
integral_gb_runtime_socket_t integral_gb_runtime_tcp_listen_ipv4(unsigned port, int backlog, bool nonblocking);
integral_gb_runtime_socket_t integral_gb_runtime_tcp_accept(integral_gb_runtime_socket_t listener);

#endif

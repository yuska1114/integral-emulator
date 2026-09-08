/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "net_compat.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <netdb.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>

static unsigned winsock_init_count;

static bool ipv4_is_usable_lan(uint32_t addr_network_order)
{
    uint32_t addr = ntohl(addr_network_order);
    unsigned first = (unsigned)((addr >> 24) & 0xFFu);
    unsigned second = (unsigned)((addr >> 16) & 0xFFu);
    if (first == 0u || first == 127u || first == 169u || first >= 224u) {
        return false;
    }
    if (first == 255u) {
        return false;
    }
    (void)second;
    return true;
}

static bool ipv4_is_private_lan(uint32_t addr_network_order)
{
    uint32_t addr = ntohl(addr_network_order);
    unsigned first = (unsigned)((addr >> 24) & 0xFFu);
    unsigned second = (unsigned)((addr >> 16) & 0xFFu);
    return first == 10u ||
           (first == 172u && second >= 16u && second <= 31u) ||
           (first == 192u && second == 168u);
}

int integral_gb_runtime_net_init(void)
{
    if (winsock_init_count > 0) {
        winsock_init_count++;
        return 0;
    }
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return -1;
    }
    winsock_init_count = 1;
    return 0;
}

void integral_gb_runtime_net_shutdown(void)
{
    if (winsock_init_count == 0) {
        return;
    }
    winsock_init_count--;
    if (winsock_init_count == 0) {
        WSACleanup();
    }
}

uint64_t integral_gb_runtime_now_us(void)
{
    return GetTickCount64() * 1000u;
}

void integral_gb_runtime_sleep_ms(unsigned ms)
{
    Sleep(ms);
}

int integral_gb_runtime_socket_close(integral_gb_runtime_socket_t sock)
{
    return closesocket((SOCKET)sock);
}

int integral_gb_runtime_socket_set_nonblocking(integral_gb_runtime_socket_t sock)
{
    u_long mode = 1;
    return ioctlsocket((SOCKET)sock, FIONBIO, &mode);
}

static int integral_gb_runtime_socket_set_blocking(integral_gb_runtime_socket_t sock)
{
    u_long mode = 0;
    return ioctlsocket((SOCKET)sock, FIONBIO, &mode);
}

ssize_t integral_gb_runtime_socket_read(integral_gb_runtime_socket_t sock, void *dest, size_t size)
{
    int chunk = size > INT_MAX ? INT_MAX : (int)size;
    int n = recv((SOCKET)sock, (char *)dest, chunk, 0);
    return n < 0 ? -1 : (ssize_t)n;
}

ssize_t integral_gb_runtime_socket_write(integral_gb_runtime_socket_t sock, const void *src, size_t size)
{
    int chunk = size > INT_MAX ? INT_MAX : (int)size;
    int n = send((SOCKET)sock, (const char *)src, chunk, 0);
    return n < 0 ? -1 : (ssize_t)n;
}

int integral_gb_runtime_socket_last_error(void)
{
    return WSAGetLastError();
}

bool integral_gb_runtime_socket_error_would_block(int error_code)
{
    return error_code == WSAEWOULDBLOCK;
}

bool integral_gb_runtime_socket_error_in_progress(int error_code)
{
    return error_code == WSAEWOULDBLOCK || error_code == WSAEINPROGRESS || error_code == WSAEINVAL;
}

bool integral_gb_runtime_socket_error_interrupted(int error_code)
{
    return error_code == WSAEINTR;
}

bool integral_gb_runtime_socket_error_connection_lost(int error_code)
{
    return error_code == WSAECONNRESET || error_code == WSAESHUTDOWN || error_code == WSAECONNABORTED;
}

const char *integral_gb_runtime_socket_error_string(int error_code)
{
    static char buffer[64];
    snprintf(buffer, sizeof(buffer), "WinSock error %d", error_code);
    return buffer;
}

bool integral_gb_runtime_detect_local_ipv4(char *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = '\0';
    if (integral_gb_runtime_net_init() != 0) {
        return false;
    }

    ULONG buffer_size = 15000;
    IP_ADAPTER_ADDRESSES *addresses = malloc(buffer_size);
    if (!addresses) {
        return false;
    }
    ULONG result = GetAdaptersAddresses(AF_INET,
                                        GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                        NULL,
                                        addresses,
                                        &buffer_size);
    if (result == ERROR_BUFFER_OVERFLOW) {
        IP_ADAPTER_ADDRESSES *larger = realloc(addresses, buffer_size);
        if (!larger) {
            free(addresses);
            return false;
        }
        addresses = larger;
        result = GetAdaptersAddresses(AF_INET,
                                      GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                      NULL,
                                      addresses,
                                      &buffer_size);
    }
    if (result != NO_ERROR) {
        free(addresses);
        return false;
    }

    char fallback[INET_ADDRSTRLEN];
    fallback[0] = '\0';
    for (IP_ADAPTER_ADDRESSES *adapter = addresses; adapter; adapter = adapter->Next) {
        if (adapter->OperStatus != IfOperStatusUp ||
            adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            adapter->IfType == IF_TYPE_TUNNEL) {
            continue;
        }
        for (IP_ADAPTER_UNICAST_ADDRESS *addr = adapter->FirstUnicastAddress; addr; addr = addr->Next) {
            if (!addr->Address.lpSockaddr || addr->Address.lpSockaddr->sa_family != AF_INET) {
                continue;
            }
            const struct sockaddr_in *sin = (const struct sockaddr_in *)addr->Address.lpSockaddr;
            if (!ipv4_is_usable_lan(sin->sin_addr.s_addr)) {
                continue;
            }
            char text[INET_ADDRSTRLEN];
            if (!inet_ntop(AF_INET, &sin->sin_addr, text, sizeof(text))) {
                continue;
            }
            size_t len = strlen(text);
            if (len >= out_size) {
                continue;
            }
            if (ipv4_is_private_lan(sin->sin_addr.s_addr)) {
                memcpy(out, text, len + 1);
                free(addresses);
                return true;
            }
            if (fallback[0] == '\0') {
                memcpy(fallback, text, len + 1);
            }
        }
    }
    free(addresses);
    if (fallback[0] != '\0') {
        size_t len = strlen(fallback);
        if (len < out_size) {
            memcpy(out, fallback, len + 1);
            return true;
        }
    }
    return false;
}

bool integral_gb_runtime_localtime(time_t source, struct tm *dest)
{
    return localtime_s(dest, &source) == 0;
}
#else
int integral_gb_runtime_net_init(void)
{
    return 0;
}

void integral_gb_runtime_net_shutdown(void)
{
}

uint64_t integral_gb_runtime_now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000u + (uint64_t)tv.tv_usec;
}

void integral_gb_runtime_sleep_ms(unsigned ms)
{
    usleep((useconds_t)ms * 1000u);
}

int integral_gb_runtime_socket_close(integral_gb_runtime_socket_t sock)
{
    return close(sock);
}

int integral_gb_runtime_socket_set_nonblocking(integral_gb_runtime_socket_t sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

static int integral_gb_runtime_socket_set_blocking(integral_gb_runtime_socket_t sock)
{
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(sock, F_SETFL, flags & ~O_NONBLOCK);
}

ssize_t integral_gb_runtime_socket_read(integral_gb_runtime_socket_t sock, void *dest, size_t size)
{
    return read(sock, dest, size);
}

ssize_t integral_gb_runtime_socket_write(integral_gb_runtime_socket_t sock, const void *src, size_t size)
{
    return write(sock, src, size);
}

int integral_gb_runtime_socket_last_error(void)
{
    return errno;
}

bool integral_gb_runtime_socket_error_would_block(int error_code)
{
    return error_code == EAGAIN || error_code == EWOULDBLOCK;
}

bool integral_gb_runtime_socket_error_in_progress(int error_code)
{
    return error_code == EINPROGRESS;
}

bool integral_gb_runtime_socket_error_interrupted(int error_code)
{
    return error_code == EINTR;
}

bool integral_gb_runtime_socket_error_connection_lost(int error_code)
{
    return error_code == EPIPE || error_code == ECONNRESET;
}

const char *integral_gb_runtime_socket_error_string(int error_code)
{
    return strerror(error_code);
}

bool integral_gb_runtime_detect_local_ipv4(char *out, size_t out_size)
{
    struct ifaddrs *ifaddr = NULL;
    if (!out || out_size == 0 || getifaddrs(&ifaddr) != 0) {
        return false;
    }

    bool found = false;
    for (struct ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
        if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 || (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        const struct sockaddr_in *addr = (const struct sockaddr_in *)ifa->ifa_addr;
        uint32_t ip = ntohl(addr->sin_addr.s_addr);
        unsigned first = (unsigned)((ip >> 24) & 0xFFu);
        if (first == 0u || first == 127u || first == 169u || first >= 224u) {
            continue;
        }
        char text[INET_ADDRSTRLEN];
        if (!inet_ntop(AF_INET, &addr->sin_addr, text, sizeof(text))) {
            continue;
        }
        size_t len = strlen(text);
        if (len >= out_size) {
            continue;
        }
        memcpy(out, text, len + 1);
        found = true;
        break;
    }

    freeifaddrs(ifaddr);
    return found;
}

bool integral_gb_runtime_localtime(time_t source, struct tm *dest)
{
    return localtime_r(&source, dest) != NULL;
}
#endif

integral_gb_runtime_socket_t integral_gb_runtime_tcp_connect(const char *host, unsigned port)
{
    char service[16];
    int length = snprintf(service, sizeof(service), "%u", port);
    if (length < 0 || (size_t)length >= sizeof(service)) {
        return INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo *results = NULL;
    if (getaddrinfo(host, service, &hints, &results) != 0) {
        return INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }

    integral_gb_runtime_socket_t fd = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    for (struct addrinfo *it = results; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
            continue;
        }
        if (connect(fd, it->ai_addr, (socklen_t)it->ai_addrlen) == 0) {
            break;
        }
        integral_gb_runtime_socket_close(fd);
        fd = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        int yes = 1;
        (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
    }
    freeaddrinfo(results);
    return fd;
}

integral_gb_runtime_socket_t integral_gb_runtime_tcp_listen_ipv4(unsigned port, int backlog, bool nonblocking)
{
    integral_gb_runtime_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        return fd;
    }
    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(fd, backlog) != 0 ||
        (nonblocking && integral_gb_runtime_socket_set_nonblocking(fd) != 0)) {
        integral_gb_runtime_socket_close(fd);
        return INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }
    return fd;
}

integral_gb_runtime_socket_t integral_gb_runtime_tcp_accept(integral_gb_runtime_socket_t listener)
{
    integral_gb_runtime_socket_t fd = accept(listener, NULL, NULL);
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd) && integral_gb_runtime_socket_set_blocking(fd) != 0) {
        integral_gb_runtime_socket_close(fd);
        return INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }
    return fd;
}

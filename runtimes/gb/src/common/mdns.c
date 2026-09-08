/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mdns.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "net_compat.h"

#define INTEGRAL_GB_RUNTIME_MDNS_ADDR "224.0.0.251"
#define INTEGRAL_GB_RUNTIME_MDNS_PORT 5353u
#define INTEGRAL_GB_RUNTIME_MDNS_TARGET "gb_runtime.local"
#define INTEGRAL_GB_RUNTIME_MDNS_TTL 120u

static uint16_t read_u16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0] << 8) | src[1]);
}

static void write_u16(uint8_t *dest, uint16_t value)
{
    dest[0] = (uint8_t)(value >> 8);
    dest[1] = (uint8_t)value;
}

static void write_u32(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

static bool append_bytes(uint8_t *packet, size_t packet_size, size_t *offset, const void *src, size_t size)
{
    if (*offset > packet_size || size > packet_size - *offset) {
        return false;
    }
    memcpy(packet + *offset, src, size);
    *offset += size;
    return true;
}

static bool append_u16(uint8_t *packet, size_t packet_size, size_t *offset, uint16_t value)
{
    uint8_t bytes[2];
    write_u16(bytes, value);
    return append_bytes(packet, packet_size, offset, bytes, sizeof(bytes));
}

static bool append_u32(uint8_t *packet, size_t packet_size, size_t *offset, uint32_t value)
{
    uint8_t bytes[4];
    write_u32(bytes, value);
    return append_bytes(packet, packet_size, offset, bytes, sizeof(bytes));
}

static bool append_dns_name(uint8_t *packet, size_t packet_size, size_t *offset, const char *name)
{
    const char *label = name;
    while (*label) {
        const char *dot = strchr(label, '.');
        size_t label_len = dot ? (size_t)(dot - label) : strlen(label);
        if (label_len == 0 || label_len > 63 || *offset >= packet_size) {
            return false;
        }
        packet[(*offset)++] = (uint8_t)label_len;
        if (!append_bytes(packet, packet_size, offset, label, label_len)) {
            return false;
        }
        if (!dot) {
            break;
        }
        label = dot + 1;
    }
    if (*offset >= packet_size) {
        return false;
    }
    packet[(*offset)++] = 0;
    return true;
}

static bool begin_record(uint8_t *packet,
                         size_t packet_size,
                         size_t *offset,
                         const char *name,
                         uint16_t type,
                         size_t *rdlength_offset,
                         size_t *rdata_offset)
{
    if (!append_dns_name(packet, packet_size, offset, name) ||
        !append_u16(packet, packet_size, offset, type) ||
        !append_u16(packet, packet_size, offset, 1u) ||
        !append_u32(packet, packet_size, offset, INTEGRAL_GB_RUNTIME_MDNS_TTL)) {
        return false;
    }
    *rdlength_offset = *offset;
    if (!append_u16(packet, packet_size, offset, 0u)) {
        return false;
    }
    *rdata_offset = *offset;
    return true;
}

static bool finish_record(uint8_t *packet, size_t rdlength_offset, size_t rdata_offset, size_t offset)
{
    size_t rdata_len = offset - rdata_offset;
    if (rdata_len > UINT16_MAX) {
        return false;
    }
    write_u16(packet + rdlength_offset, (uint16_t)rdata_len);
    return true;
}

static bool detect_local_ipv4_addr(struct in_addr *out)
{
    char text[INET_ADDRSTRLEN];
    if (!integral_gb_runtime_detect_local_ipv4(text, sizeof(text))) {
        return false;
    }
    return inet_pton(AF_INET, text, out) == 1;
}

static integral_gb_runtime_socket_t create_udp_socket(void)
{
    if (integral_gb_runtime_net_init() != 0) {
        return INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }
    integral_gb_runtime_socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        integral_gb_runtime_net_shutdown();
        return INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }

    int yes = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));
#ifdef SO_REUSEPORT
    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, (const char *)&yes, sizeof(yes));
#endif
    return fd;
}

int integral_gb_runtime_mdns_advertise_named_once(const char *service,
                                         const char *instance,
                                         const char *target,
                                         unsigned port)
{
    struct in_addr local_addr;
    if (!detect_local_ipv4_addr(&local_addr)) {
        return -1;
    }

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));
    write_u16(packet + 2, 0x8400u);
    write_u16(packet + 6, 3u);
    size_t offset = 12;

    size_t rdlength_offset = 0;
    size_t rdata_offset = 0;
    if (!begin_record(packet, sizeof(packet), &offset, service, 12u, &rdlength_offset, &rdata_offset) ||
        !append_dns_name(packet, sizeof(packet), &offset, instance) ||
        !finish_record(packet, rdlength_offset, rdata_offset, offset)) {
        return -1;
    }

    if (!begin_record(packet, sizeof(packet), &offset, instance, 33u, &rdlength_offset, &rdata_offset) ||
        !append_u16(packet, sizeof(packet), &offset, 0u) ||
        !append_u16(packet, sizeof(packet), &offset, 0u) ||
        !append_u16(packet, sizeof(packet), &offset, (uint16_t)port) ||
        !append_dns_name(packet, sizeof(packet), &offset, target) ||
        !finish_record(packet, rdlength_offset, rdata_offset, offset)) {
        return -1;
    }

    if (!begin_record(packet, sizeof(packet), &offset, target, 1u, &rdlength_offset, &rdata_offset) ||
        !append_bytes(packet, sizeof(packet), &offset, &local_addr, sizeof(local_addr)) ||
        !finish_record(packet, rdlength_offset, rdata_offset, offset)) {
        return -1;
    }

    integral_gb_runtime_socket_t fd = create_udp_socket();
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        return -1;
    }

    unsigned char ttl = 255;
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_TTL, (const char *)&ttl, sizeof(ttl));
    (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (const char *)&local_addr, sizeof(local_addr));
    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)INTEGRAL_GB_RUNTIME_MDNS_PORT);
    inet_pton(AF_INET, INTEGRAL_GB_RUNTIME_MDNS_ADDR, &dest.sin_addr);
    int sent = sendto(fd, (const char *)packet, (int)offset, 0, (const struct sockaddr *)&dest, sizeof(dest));
    integral_gb_runtime_socket_close(fd);
    integral_gb_runtime_net_shutdown();
    return sent == (ssize_t)offset ? 0 : -1;
}

int integral_gb_runtime_mdns_advertise_once(unsigned port)
{
    return integral_gb_runtime_mdns_advertise_named_once(INTEGRAL_GB_RUNTIME_MDNS_SERVICE,
                                                INTEGRAL_GB_RUNTIME_MDNS_INSTANCE,
                                                INTEGRAL_GB_RUNTIME_MDNS_TARGET,
                                                port);
}

static bool decode_dns_name(const uint8_t *packet,
                            size_t packet_size,
                            size_t *offset,
                            char *out,
                            size_t out_size)
{
    size_t pos = *offset;
    size_t out_len = 0;
    unsigned jumps = 0;
    bool jumped = false;

    if (out_size == 0) {
        return false;
    }

    while (pos < packet_size) {
        uint8_t len = packet[pos++];
        if (len == 0) {
            if (!jumped) {
                *offset = pos;
            }
            out[out_len] = '\0';
            return true;
        }
        if ((len & 0xC0u) == 0xC0u) {
            if (pos >= packet_size || jumps++ > 16) {
                return false;
            }
            uint16_t pointer = (uint16_t)(((uint16_t)(len & 0x3Fu) << 8) | packet[pos++]);
            if (pointer >= packet_size) {
                return false;
            }
            if (!jumped) {
                *offset = pos;
            }
            pos = pointer;
            jumped = true;
            continue;
        }
        if ((len & 0xC0u) != 0 || pos + len > packet_size) {
            return false;
        }
        if (out_len > 0) {
            if (out_len + 1 >= out_size) {
                return false;
            }
            out[out_len++] = '.';
        }
        if (out_len + len >= out_size) {
            return false;
        }
        memcpy(out + out_len, packet + pos, len);
        out_len += len;
        pos += len;
    }
    return false;
}

static bool skip_question(const uint8_t *packet, size_t packet_size, size_t *offset)
{
    char name[256];
    if (!decode_dns_name(packet, packet_size, offset, name, sizeof(name)) ||
        *offset > packet_size ||
        packet_size - *offset < 4) {
        return false;
    }
    *offset += 4;
    return true;
}

static bool parse_response(const uint8_t *packet,
                           size_t packet_size,
                           const char *service,
                           char *host_out,
                           size_t host_out_size,
                           unsigned *port_out)
{
    if (packet_size < 12) {
        return false;
    }
    unsigned qdcount = read_u16(packet + 4);
    unsigned ancount = read_u16(packet + 6);
    unsigned nscount = read_u16(packet + 8);
    unsigned arcount = read_u16(packet + 10);
    size_t offset = 12;
    for (unsigned i = 0; i < qdcount; i++) {
        if (!skip_question(packet, packet_size, &offset)) {
            return false;
        }
    }

    bool found_host = false;
    bool found_port = false;
    unsigned record_count = ancount + nscount + arcount;
    for (unsigned i = 0; i < record_count; i++) {
        char name[256];
        if (!decode_dns_name(packet, packet_size, &offset, name, sizeof(name)) ||
            offset > packet_size ||
            packet_size - offset < 10) {
            return false;
        }
        uint16_t type = read_u16(packet + offset);
        uint16_t rdlength = read_u16(packet + offset + 8);
        offset += 10;
        if (offset > packet_size || rdlength > packet_size - offset) {
            return false;
        }
        size_t rdata_offset = offset;

        size_t name_len = strlen(name);
        size_t service_len = strlen(service);
        if (type == 33u && name_len > service_len &&
            strcasecmp(name + name_len - service_len, service) == 0 && rdlength >= 7) {
            *port_out = read_u16(packet + rdata_offset + 4);
            found_port = true;
        }
        else if (type == 1u && rdlength == 4) {
            char addr_text[INET_ADDRSTRLEN];
            if (inet_ntop(AF_INET, packet + rdata_offset, addr_text, sizeof(addr_text))) {
                size_t len = strlen(addr_text);
                if (len < host_out_size) {
                    memcpy(host_out, addr_text, len + 1);
                    found_host = true;
                }
            }
        }

        offset = rdata_offset + rdlength;
        if (found_host && found_port) {
            return true;
        }
    }
    return false;
}

static bool send_discovery_query(integral_gb_runtime_socket_t fd,
                                 const struct in_addr *local_addr,
                                 const char *service)
{
    uint8_t packet[256];
    memset(packet, 0, sizeof(packet));
    write_u16(packet + 4, 1u);
    size_t offset = 12;
    if (!append_dns_name(packet, sizeof(packet), &offset, service) ||
        !append_u16(packet, sizeof(packet), &offset, 12u) ||
        !append_u16(packet, sizeof(packet), &offset, 1u)) {
        return false;
    }

    struct sockaddr_in dest;
    memset(&dest, 0, sizeof(dest));
    dest.sin_family = AF_INET;
    dest.sin_port = htons((uint16_t)INTEGRAL_GB_RUNTIME_MDNS_PORT);
    inet_pton(AF_INET, INTEGRAL_GB_RUNTIME_MDNS_ADDR, &dest.sin_addr);
    if (local_addr) {
        (void)setsockopt(fd, IPPROTO_IP, IP_MULTICAST_IF, (const char *)local_addr, sizeof(*local_addr));
    }
    int sent = sendto(fd, (const char *)packet, (int)offset, 0, (const struct sockaddr *)&dest, sizeof(dest));
    return sent == (int)offset;
}

int integral_gb_runtime_mdns_discover_named(const char *service,
                                  char *host_out,
                                  size_t host_out_size,
                                  unsigned *port_out,
                                  unsigned timeout_ms)
{
    if (!host_out || host_out_size == 0 || !port_out) {
        return -1;
    }
    host_out[0] = '\0';
    *port_out = 0;

    integral_gb_runtime_socket_t fd = create_udp_socket();
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        return -1;
    }

    struct sockaddr_in bind_addr;
    memset(&bind_addr, 0, sizeof(bind_addr));
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons((uint16_t)INTEGRAL_GB_RUNTIME_MDNS_PORT);
    if (bind(fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) != 0) {
        integral_gb_runtime_socket_close(fd);
        integral_gb_runtime_net_shutdown();
        return -1;
    }

    struct in_addr local_addr;
    bool has_local_addr = detect_local_ipv4_addr(&local_addr);
    struct ip_mreq mreq;
    memset(&mreq, 0, sizeof(mreq));
    inet_pton(AF_INET, INTEGRAL_GB_RUNTIME_MDNS_ADDR, &mreq.imr_multiaddr);
    mreq.imr_interface.s_addr = has_local_addr ? local_addr.s_addr : htonl(INADDR_ANY);
    (void)setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, (const char *)&mreq, sizeof(mreq));
    (void)send_discovery_query(fd, has_local_addr ? &local_addr : NULL, service);

    uint64_t start_us = integral_gb_runtime_now_us();
    for (;;) {
        unsigned elapsed = (unsigned)((integral_gb_runtime_now_us() - start_us) / 1000u);
        if (elapsed >= timeout_ms) {
            break;
        }
        unsigned remaining = timeout_ms - elapsed;
        struct timeval timeout;
        timeout.tv_sec = remaining / 1000u;
        timeout.tv_usec = (long)((remaining % 1000u) * 1000u);
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(fd, &read_set);
        int ready = select(INTEGRAL_GB_RUNTIME_SELECT_NFDS(fd), &read_set, NULL, NULL, &timeout);
        if (ready < 0) {
            if (integral_gb_runtime_socket_error_interrupted(integral_gb_runtime_socket_last_error())) {
                continue;
            }
            break;
        }
        if (ready == 0) {
            break;
        }
        uint8_t packet[1500];
        int n = recv(fd, (char *)packet, sizeof(packet), 0);
        if (n > 0 && parse_response(packet, (size_t)n, service, host_out, host_out_size, port_out)) {
            integral_gb_runtime_socket_close(fd);
            integral_gb_runtime_net_shutdown();
            return 0;
        }
    }

    integral_gb_runtime_socket_close(fd);
    integral_gb_runtime_net_shutdown();
    return -1;
}

int integral_gb_runtime_mdns_discover(char *host_out, size_t host_out_size, unsigned *port_out, unsigned timeout_ms)
{
    return integral_gb_runtime_mdns_discover_named(INTEGRAL_GB_RUNTIME_MDNS_SERVICE,
                                         host_out,
                                         host_out_size,
                                         port_out,
                                         timeout_ms);
}

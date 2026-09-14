/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "stream_server.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifndef _WIN32
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "file_util.h"
#include "joypad.h"
#include "net_compat.h"
#include "protocol.h"
#include "string_util.h"

#define INTEGRAL_GB_RUNTIME_REMOTE_WRITE_TIMEOUT_MS 3000u
#define INTEGRAL_GB_RUNTIME_AUTH_TIMEOUT_US 3000000ULL

static void put_u16(uint8_t *dest, uint16_t value)
{
    dest[0] = (uint8_t)(value >> 8);
    dest[1] = (uint8_t)value;
}

static void put_u32(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)(value >> 24);
    dest[1] = (uint8_t)(value >> 16);
    dest[2] = (uint8_t)(value >> 8);
    dest[3] = (uint8_t)value;
}

static void put_u64(uint8_t *dest, uint64_t value)
{
    for (unsigned i = 0; i < 8; i++) {
        dest[i] = (uint8_t)(value >> (56 - i * 8));
    }
}

static uint32_t get_u32(const uint8_t *src)
{
    return (uint32_t)src[0] << 24 |
           (uint32_t)src[1] << 16 |
           (uint32_t)src[2] << 8 |
           (uint32_t)src[3];
}

static uint64_t get_u64(const uint8_t *src)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) {
        value = (value << 8) | src[i];
    }
    return value;
}

static int wait_read_exact(integral_gb_runtime_socket_t fd,
                           uint8_t *dest,
                           unsigned size,
                           IntegralGBRuntimeStreamWaitCallback wait_callback,
                           void *wait_user)
{
    unsigned offset = 0;
    while (offset < size) {
        ssize_t n = integral_gb_runtime_socket_read(fd, dest + offset, size - offset);
        if (n > 0) {
            offset += (unsigned)n;
            continue;
        }
        if (n == 0) {
            fprintf(stderr, "client disconnected during slot2 upload\n");
            return -1;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (integral_gb_runtime_socket_error_would_block(error_code)) {
            if (wait_callback && !wait_callback(wait_user)) {
                return -1;
            }
            integral_gb_runtime_sleep_ms(10);
            continue;
        }
        fprintf(stderr, "slot2 upload read failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        return -1;
    }
    return 0;
}

static int wait_write_all(integral_gb_runtime_socket_t fd, const uint8_t *src, unsigned size)
{
    unsigned offset = 0;
    uint64_t idle_start_us = integral_gb_runtime_now_us();
    while (offset < size) {
        ssize_t n = integral_gb_runtime_socket_write(fd, src + offset, size - offset);
        if (n > 0) {
            offset += (unsigned)n;
            idle_start_us = integral_gb_runtime_now_us();
            continue;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (n < 0 && integral_gb_runtime_socket_error_would_block(error_code)) {
            uint64_t elapsed_ms = (integral_gb_runtime_now_us() - idle_start_us) / 1000u;
            if (elapsed_ms >= INTEGRAL_GB_RUNTIME_REMOTE_WRITE_TIMEOUT_MS) {
                fprintf(stderr, "remote write timed out\n");
                return -1;
            }
            integral_gb_runtime_sleep_ms(10);
            continue;
        }
        if (n < 0 && integral_gb_runtime_socket_error_connection_lost(error_code)) {
            fprintf(stderr, "remote disconnected during write\n");
            return -1;
        }
        fprintf(stderr, "remote write failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        return -1;
    }
    return 0;
}

static int flush_downlink_before_save(IntegralGBRuntimeStreamServer *server)
{
    if (server->downlink_buffer_sent < server->downlink_buffer_size) {
        if (wait_write_all(server->client_fd,
                           server->downlink_buffer + server->downlink_buffer_sent,
                           server->downlink_buffer_size - server->downlink_buffer_sent) != 0) {
            return -1;
        }
        server->downlink_buffer_sent = server->downlink_buffer_size;
    }

    if (server->downlink_buffer_size > 0 &&
        server->downlink_buffer_sent == server->downlink_buffer_size) {
        if (server->downlink_buffer[0] == INTEGRAL_GB_RUNTIME_PACKET_VIDEO) {
            server->frames_sent++;
        }
        else if (server->downlink_buffer[0] == INTEGRAL_GB_RUNTIME_PACKET_AUDIO) {
            server->audio_packets_sent++;
        }
        server->downlink_buffer_size = 0;
        server->downlink_buffer_sent = 0;
    }
    return 0;
}

static int receive_payload_to_temp(integral_gb_runtime_socket_t fd,
                                   uint32_t size,
                                   const char *template_text,
                                   char *path_out,
                                   size_t path_out_size,
                                   IntegralGBRuntimeStreamWaitCallback wait_callback,
                                   void *wait_user)
{
    char path[512];
#ifdef _WIN32
    const char *template_name = strrchr(template_text, '/');
    template_name = template_name ? template_name + 1 : template_text;
    char time_text[32];
    int time_len = snprintf(time_text, sizeof(time_text), "%lu", (unsigned long)integral_gb_runtime_now_us());
    if (time_len < 0 || (size_t)time_len >= sizeof(time_text)) {
        return -1;
    }
    size_t template_name_len = strlen(template_name);
    size_t time_text_len = (size_t)time_len;
    const char separator[] = "_";
    const char suffix[] = ".tmp";
    if (template_name_len + sizeof(separator) - 1u + time_text_len + sizeof(suffix) > sizeof(path)) {
        return -1;
    }
    size_t offset = 0;
    memcpy(path + offset, template_name, template_name_len);
    offset += template_name_len;
    memcpy(path + offset, separator, sizeof(separator) - 1u);
    offset += sizeof(separator) - 1u;
    memcpy(path + offset, time_text, time_text_len);
    offset += time_text_len;
    memcpy(path + offset, suffix, sizeof(suffix));
    path_out[0] = '\0';
    FILE *out = fopen(path, "wb");
    if (!out) {
        fprintf(stderr, "temp file open failed: %s\n", strerror(errno));
        return -1;
    }
#else
    if (!integral_gb_runtime_copy_text(path, sizeof(path), template_text)) {
        return -1;
    }
    path_out[0] = '\0';
    int out_fd = mkstemp(path);
    if (out_fd < 0) {
        fprintf(stderr, "mkstemp failed: %s\n", strerror(errno));
        return -1;
    }
#endif

    uint8_t buffer[8192];
    uint32_t remaining = size;
    while (remaining > 0) {
        unsigned chunk = remaining > sizeof(buffer) ? (unsigned)sizeof(buffer) : remaining;
        if (wait_read_exact(fd, buffer, chunk, wait_callback, wait_user) != 0) {
#ifdef _WIN32
            fclose(out);
#else
            close(out_fd);
#endif
            integral_gb_runtime_unlink(path);
            return -1;
        }
#ifdef _WIN32
        if (fwrite(buffer, 1, chunk, out) != chunk) {
            fprintf(stderr, "temp file write failed: %s\n", strerror(errno));
            fclose(out);
            integral_gb_runtime_unlink(path);
            return -1;
        }
#else
        unsigned written = 0;
        while (written < chunk) {
            ssize_t n = write(out_fd, buffer + written, chunk - written);
            if (n <= 0) {
                fprintf(stderr, "temp file write failed: %s\n", strerror(errno));
                close(out_fd);
                integral_gb_runtime_unlink(path);
                return -1;
            }
            written += (unsigned)n;
        }
#endif
        remaining -= chunk;
    }

#ifdef _WIN32
    if (fclose(out) != 0) {
        fprintf(stderr, "temp file close failed: %s\n", strerror(errno));
        integral_gb_runtime_unlink(path);
        return -1;
    }
#else
    if (close(out_fd) != 0) {
        fprintf(stderr, "temp file close failed: %s\n", strerror(errno));
        integral_gb_runtime_unlink(path);
        return -1;
    }
#endif
    if (!integral_gb_runtime_copy_text(path_out, path_out_size, path)) {
        integral_gb_runtime_unlink(path);
        return -1;
    }
    return 0;
}

static void close_client(IntegralGBRuntimeStreamServer *server)
{
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd)) {
        integral_gb_runtime_socket_close(server->client_fd);
        server->client_fd = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
        server->packet_buffer_size = 0;
        server->downlink_buffer_size = 0;
        server->downlink_buffer_sent = 0;
        if (server->remote_input_applied && server->remote_buttons_applied != 0) {
            server->remote_release_pending = true;
        }
        server->remote_buttons = 0;
    }
}

int integral_gb_runtime_stream_server_open(IntegralGBRuntimeStreamServer *server, const char *bind_host, unsigned port)
{
    memset(server, 0, sizeof(*server));
    server->listen_fd = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    server->client_fd = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    if (integral_gb_runtime_net_init() != 0) {
        fprintf(stderr, "network init failed\n");
        return -1;
    }

    integral_gb_runtime_socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        int error_code = integral_gb_runtime_socket_last_error();
        fprintf(stderr, "socket failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        return -1;
    }

    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&yes, sizeof(yes));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, bind_host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid bind address '%s'\n", bind_host);
        integral_gb_runtime_socket_close(fd);
        return -1;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        int error_code = integral_gb_runtime_socket_last_error();
        fprintf(stderr, "bind failed on %s:%u: %s\n",
                bind_host,
                port,
                integral_gb_runtime_socket_error_string(error_code));
        integral_gb_runtime_socket_close(fd);
        return -1;
    }
    if (listen(fd, 1) != 0) {
        int error_code = integral_gb_runtime_socket_last_error();
        fprintf(stderr, "listen failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        integral_gb_runtime_socket_close(fd);
        return -1;
    }
    if (integral_gb_runtime_socket_set_nonblocking(fd) != 0) {
        fprintf(stderr, "failed to set listen socket nonblocking\n");
        integral_gb_runtime_socket_close(fd);
        return -1;
    }

    server->listen_fd = fd;
    server->active = true;
    return 0;
}

void integral_gb_runtime_stream_server_set_auth_token(IntegralGBRuntimeStreamServer *server, const char *auth_token)
{
    if (!server || !auth_token || auth_token[0] == '\0') {
        return;
    }
    if (!integral_gb_runtime_copy_text(server->auth_token, sizeof(server->auth_token), auth_token)) {
        server->auth_token[0] = '\0';
        server->auth_required = true;
        server->authenticated = false;
        return;
    }
    server->auth_required = true;
    server->authenticated = false;
}

static void accept_client(IntegralGBRuntimeStreamServer *server)
{
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd)) {
        return;
    }

    integral_gb_runtime_socket_t fd = accept(server->listen_fd, NULL, NULL);
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(fd)) {
        int error_code = integral_gb_runtime_socket_last_error();
        if (!integral_gb_runtime_socket_error_would_block(error_code)) {
            fprintf(stderr, "accept failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        }
        return;
    }

    if (integral_gb_runtime_socket_set_nonblocking(fd) != 0) {
        integral_gb_runtime_socket_close(fd);
        return;
    }

    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, (const char *)&yes, sizeof(yes));
    server->client_fd = fd;
    server->authenticated = !server->auth_required;
    server->auth_buffer_size = 0;
    server->packet_buffer_size = 0;
    server->remote_buttons = 0;
    server->remote_input_seq = 0;
    server->remote_input_client_timestamp_us = 0;
    server->last_applied_input_seq = 0;
    server->last_applied_client_timestamp_us = 0;
    server->client_connected_at_us = integral_gb_runtime_now_us();
    printf("  remote input: client connected\n");
}

int integral_gb_runtime_stream_server_wait_for_slot2_upload(IntegralGBRuntimeStreamServer *server,
                                                  char *rom_path,
                                                  size_t rom_path_size,
                                                  char *save_path,
                                                  size_t save_path_size,
                                                  uint32_t *client_unix_time,
                                                  IntegralGBRuntimeStreamWaitCallback wait_callback,
                                                  void *wait_user)
{
    if (!server->active) {
        return -1;
    }

    printf("  slot2 upload: waiting for client ROM/SAV\n");
    while (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd)) {
        if (wait_callback && !wait_callback(wait_user)) {
            return -1;
        }
        accept_client(server);
        if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd)) {
            integral_gb_runtime_sleep_ms(10);
        }
    }

    uint8_t header[INTEGRAL_GB_RUNTIME_UPLOAD_HEADER_SIZE];
    if (wait_read_exact(server->client_fd, header, sizeof(header), wait_callback, wait_user) != 0) {
        close_client(server);
        return -1;
    }
    if (header[0] != INTEGRAL_GB_RUNTIME_UPLOAD_MAGIC0 ||
        header[1] != INTEGRAL_GB_RUNTIME_UPLOAD_MAGIC1 ||
        header[2] != INTEGRAL_GB_RUNTIME_UPLOAD_MAGIC2 ||
        header[3] != INTEGRAL_GB_RUNTIME_UPLOAD_MAGIC3) {
        fprintf(stderr, "invalid slot2 upload header\n");
        close_client(server);
        return -1;
    }

    uint32_t rom_size = get_u32(header + 4);
    uint32_t save_size = get_u32(header + 8);
    uint32_t uploaded_client_unix_time = get_u32(header + INTEGRAL_GB_RUNTIME_UPLOAD_CLIENT_TIME_OFFSET);
    if (rom_size == 0 || rom_size > INTEGRAL_GB_RUNTIME_MAX_ROM_TRANSFER_BYTES ||
        save_size > INTEGRAL_GB_RUNTIME_MAX_SAVE_TRANSFER_BYTES) {
        fprintf(stderr, "invalid slot2 upload sizes: rom=%u save=%u\n", rom_size, save_size);
        close_client(server);
        return -1;
    }

    char tmp_rom[512] = {0};
    char tmp_save[512] = {0};
    if (receive_payload_to_temp(server->client_fd,
                                rom_size,
                                "/tmp/gb_runtime_slot2_rom_XXXXXX",
                                tmp_rom,
                                sizeof(tmp_rom),
                                wait_callback,
                                wait_user) != 0) {
        close_client(server);
        return -1;
    }
    if (receive_payload_to_temp(server->client_fd,
                                save_size,
                                "/tmp/gb_runtime_slot2_save_XXXXXX",
                                tmp_save,
                                sizeof(tmp_save),
                                wait_callback,
                                wait_user) != 0) {
        integral_gb_runtime_unlink(tmp_rom);
        close_client(server);
        return -1;
    }
    if (!integral_gb_runtime_copy_text(rom_path, rom_path_size, tmp_rom) ||
        !integral_gb_runtime_copy_text(save_path, save_path_size, tmp_save)) {
        integral_gb_runtime_unlink(tmp_rom);
        integral_gb_runtime_unlink(tmp_save);
        close_client(server);
        return -1;
    }
    if (client_unix_time) {
        *client_unix_time = uploaded_client_unix_time;
    }

    server->packet_buffer_size = 0;
    server->slot2_uploads_received++;
    printf("  slot2 upload: received rom=%u byte(s) save=%u byte(s) client_time=%u\n",
           rom_size,
           save_size,
           uploaded_client_unix_time);
    return 0;
}

static void handle_packet(IntegralGBRuntimeStreamServer *server, const uint8_t *packet)
{
    uint32_t seq = get_u32(packet);
    if (server->packets_received > 0 && seq < server->remote_input_seq) {
        return;
    }
    server->remote_input_seq = seq;
    server->remote_input_client_timestamp_us = get_u64(packet + 4);
    server->remote_buttons = packet[12];
    server->packets_received++;
}

static bool auth_packet_matches(IntegralGBRuntimeStreamServer *server)
{
    if (server->auth_buffer[0] != INTEGRAL_GB_RUNTIME_AUTH_MAGIC0 ||
        server->auth_buffer[1] != INTEGRAL_GB_RUNTIME_AUTH_MAGIC1 ||
        server->auth_buffer[2] != INTEGRAL_GB_RUNTIME_AUTH_MAGIC2 ||
        server->auth_buffer[3] != INTEGRAL_GB_RUNTIME_AUTH_MAGIC3 ||
        server->auth_buffer[4] != INTEGRAL_GB_RUNTIME_AUTH_MAGIC4) {
        return false;
    }
    char token[INTEGRAL_GB_RUNTIME_AUTH_TOKEN_SIZE + 1u];
    memcpy(token, server->auth_buffer + 5, INTEGRAL_GB_RUNTIME_AUTH_TOKEN_SIZE);
    token[INTEGRAL_GB_RUNTIME_AUTH_TOKEN_SIZE] = '\0';
    return strcmp(token, server->auth_token) == 0;
}

static bool read_auth_packet(IntegralGBRuntimeStreamServer *server)
{
    while (server->auth_buffer_size < sizeof(server->auth_buffer)) {
        ssize_t n = integral_gb_runtime_socket_read(server->client_fd,
                                          server->auth_buffer + server->auth_buffer_size,
                                          sizeof(server->auth_buffer) - server->auth_buffer_size);
        if (n > 0) {
            server->auth_buffer_size += (unsigned)n;
            continue;
        }
        if (n == 0) {
            printf("  remote input: client disconnected during auth\n");
            close_client(server);
            return false;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (integral_gb_runtime_socket_error_would_block(error_code)) {
            return false;
        }
        fprintf(stderr, "recv failed during auth: %s\n", integral_gb_runtime_socket_error_string(error_code));
        close_client(server);
        return false;
    }
    if (!auth_packet_matches(server)) {
        fprintf(stderr, "remote input auth failed\n");
        close_client(server);
        return false;
    }
    server->authenticated = true;
    server->auth_buffer_size = 0;
    printf("  remote input: client authenticated\n");
    return true;
}

static void read_client(IntegralGBRuntimeStreamServer *server)
{
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd)) {
        return;
    }
    if (server->auth_required && !server->authenticated && !read_auth_packet(server)) {
        return;
    }

    while (true) {
        ssize_t n = integral_gb_runtime_socket_read(server->client_fd,
                                          server->packet_buffer + server->packet_buffer_size,
                                          sizeof(server->packet_buffer) - server->packet_buffer_size);
        if (n > 0) {
            server->packet_buffer_size += (unsigned)n;
            if (server->packet_buffer_size == sizeof(server->packet_buffer)) {
                handle_packet(server, server->packet_buffer);
                server->packet_buffer_size = 0;
            }
            continue;
        }
        if (n == 0) {
            printf("  remote input: client disconnected\n");
            close_client(server);
            return;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (integral_gb_runtime_socket_error_would_block(error_code)) {
            return;
        }
        if (integral_gb_runtime_socket_error_connection_lost(error_code)) {
            printf("  remote input: client disconnected\n");
            close_client(server);
            return;
        }
        fprintf(stderr, "remote input read failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        close_client(server);
        return;
    }
}

void integral_gb_runtime_stream_server_poll(IntegralGBRuntimeStreamServer *server)
{
    if (!server->active) {
        return;
    }
    accept_client(server);
    read_client(server);
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd) &&
        server->auth_required &&
        !server->authenticated &&
        integral_gb_runtime_now_us() - server->client_connected_at_us > INTEGRAL_GB_RUNTIME_AUTH_TIMEOUT_US) {
        fprintf(stderr, "remote input: auth timeout\n");
        close_client(server);
        return;
    }
    if (server->auth_required && !server->authenticated) {
        return;
    }

    while (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd) &&
           server->downlink_buffer_sent < server->downlink_buffer_size) {
        ssize_t n = integral_gb_runtime_socket_write(server->client_fd,
                                           server->downlink_buffer + server->downlink_buffer_sent,
                                           server->downlink_buffer_size - server->downlink_buffer_sent);
        if (n > 0) {
            server->downlink_buffer_sent += (unsigned)n;
            continue;
        }
        int error_code = integral_gb_runtime_socket_last_error();
        if (n < 0 && integral_gb_runtime_socket_error_would_block(error_code)) {
            return;
        }
        if (integral_gb_runtime_socket_error_connection_lost(error_code)) {
            printf("  remote input: client disconnected\n");
            close_client(server);
            return;
        }
        fprintf(stderr, "remote downlink write failed: %s\n", integral_gb_runtime_socket_error_string(error_code));
        close_client(server);
        return;
    }

    if (server->downlink_buffer_size > 0 && server->downlink_buffer_sent == server->downlink_buffer_size) {
        if (server->downlink_buffer[0] == INTEGRAL_GB_RUNTIME_PACKET_VIDEO) {
            server->frames_sent++;
        }
        else if (server->downlink_buffer[0] == INTEGRAL_GB_RUNTIME_PACKET_AUDIO) {
            server->audio_packets_sent++;
        }
        server->downlink_buffer_size = 0;
        server->downlink_buffer_sent = 0;
    }
}

static void set_key_mask(IntegralGBRuntimeSlot *slot, uint8_t buttons, uint8_t button, GB_key_t key)
{
    GB_set_key_state(slot->gb, key, (buttons & button) != 0);
}

static void release_key_mask(IntegralGBRuntimeSlot *slot, uint8_t buttons, uint8_t button, GB_key_t key)
{
    if ((buttons & button) != 0) {
        GB_set_key_state(slot->gb, key, false);
    }
}

static void release_remote_buttons(IntegralGBRuntimeStreamServer *server, IntegralGBRuntimeSlot *slot)
{
    uint8_t buttons = server->remote_buttons_applied;
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_RIGHT, GB_KEY_RIGHT);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_LEFT, GB_KEY_LEFT);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_UP, GB_KEY_UP);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_DOWN, GB_KEY_DOWN);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_A, GB_KEY_A);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_B, GB_KEY_B);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_SELECT, GB_KEY_SELECT);
    release_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_START, GB_KEY_START);
    server->remote_buttons_applied = 0;
    server->remote_input_applied = false;
    server->remote_release_pending = false;
}

void integral_gb_runtime_stream_server_apply_input(IntegralGBRuntimeStreamServer *server, IntegralGBRuntimeSlot *slot)
{
    if (!server->active || !slot || !slot->initialized) {
        return;
    }
    if (server->remote_release_pending) {
        release_remote_buttons(server, slot);
    }
    if (!INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd)) {
        return;
    }

    uint8_t buttons = server->remote_buttons;
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_RIGHT, GB_KEY_RIGHT);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_LEFT, GB_KEY_LEFT);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_UP, GB_KEY_UP);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_DOWN, GB_KEY_DOWN);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_A, GB_KEY_A);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_B, GB_KEY_B);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_SELECT, GB_KEY_SELECT);
    set_key_mask(slot, buttons, INTEGRAL_GB_RUNTIME_BTN_START, GB_KEY_START);
    server->remote_buttons_applied = buttons;
    server->remote_input_applied = buttons != 0;
    server->last_applied_input_seq = server->remote_input_seq;
    server->last_applied_client_timestamp_us = server->remote_input_client_timestamp_us;
}

void integral_gb_runtime_stream_server_queue_frame(IntegralGBRuntimeStreamServer *server, const IntegralGBRuntimeSlot *slot)
{
    if (!server->active || !INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd) ||
        (server->auth_required && !server->authenticated) ||
        !slot || !slot->initialized) {
        return;
    }
    if (server->downlink_buffer_size > 0) {
        return;
    }

    server->downlink_buffer[0] = INTEGRAL_GB_RUNTIME_PACKET_VIDEO;
    put_u32(server->downlink_buffer + 1, server->frame_seq++);
    put_u64(server->downlink_buffer + 5, integral_gb_runtime_now_us());
    put_u16(server->downlink_buffer + 13, INTEGRAL_GB_RUNTIME_GB_WIDTH);
    put_u16(server->downlink_buffer + 15, INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    server->downlink_buffer[17] = INTEGRAL_GB_RUNTIME_PIXEL_FORMAT_RGB565;
    put_u32(server->downlink_buffer + 18, server->last_applied_input_seq);
    put_u64(server->downlink_buffer + 22, server->last_applied_client_timestamp_us);

    uint8_t *payload = server->downlink_buffer + 1 + INTEGRAL_GB_RUNTIME_VIDEO_HEADER_SIZE;
    const uint32_t *pixels = integral_gb_runtime_slot_presented_pixels(slot);
    for (unsigned i = 0; i < INTEGRAL_GB_RUNTIME_GB_WIDTH * INTEGRAL_GB_RUNTIME_GB_HEIGHT; i++) {
        uint16_t pixel = integral_gb_runtime_rgb888_to_rgb565(pixels[i]);
        payload[i * 2] = (uint8_t)(pixel >> 8);
        payload[i * 2 + 1] = (uint8_t)pixel;
    }

    server->downlink_buffer_size = INTEGRAL_GB_RUNTIME_VIDEO_PACKET_SIZE;
    server->downlink_buffer_sent = 0;
    server->frames_queued++;
}

void integral_gb_runtime_stream_server_queue_audio(IntegralGBRuntimeStreamServer *server, const int16_t *samples, unsigned frames)
{
    if (!server->active || !INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd) ||
        (server->auth_required && !server->authenticated) ||
        frames == 0) {
        return;
    }
    if (server->downlink_buffer_size > 0) {
        return;
    }
    if (frames > INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES) {
        frames = INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES;
    }

    server->downlink_buffer[0] = INTEGRAL_GB_RUNTIME_PACKET_AUDIO;
    put_u32(server->downlink_buffer + 1, server->audio_seq++);
    put_u64(server->downlink_buffer + 5, integral_gb_runtime_now_us());
    put_u32(server->downlink_buffer + 13, INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE);
    put_u16(server->downlink_buffer + 17, (uint16_t)frames);

    uint8_t *payload = server->downlink_buffer + 1 + INTEGRAL_GB_RUNTIME_AUDIO_HEADER_SIZE;
    for (unsigned i = 0; i < frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS; i++) {
        uint16_t value = (uint16_t)samples[i];
        payload[i * 2] = (uint8_t)(value >> 8);
        payload[i * 2 + 1] = (uint8_t)value;
    }

    server->downlink_buffer_size = 1 + INTEGRAL_GB_RUNTIME_AUDIO_HEADER_SIZE + frames * INTEGRAL_GB_RUNTIME_AUDIO_BYTES_PER_FRAME;
    server->downlink_buffer_sent = 0;
    server->audio_packets_queued++;
}

int integral_gb_runtime_stream_server_send_save_file(IntegralGBRuntimeStreamServer *server, const char *save_path)
{
    if (!server->active || !INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->client_fd) ||
        (server->auth_required && !server->authenticated) ||
        !save_path) {
        return 0;
    }
    if (flush_downlink_before_save(server) != 0) {
        close_client(server);
        return -1;
    }

    FILE *in = fopen(save_path, "rb");
    if (!in) {
        fprintf(stderr, "slot2 save return: cannot open '%s': %s\n", save_path, strerror(errno));
        return -1;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fclose(in);
        return -1;
    }
    long size_long = ftell(in);
    if (size_long < 0 || (unsigned long)size_long > INTEGRAL_GB_RUNTIME_MAX_SAVE_TRANSFER_BYTES) {
        fprintf(stderr, "slot2 save return: invalid size\n");
        fclose(in);
        return -1;
    }
    rewind(in);

    uint8_t header[1 + INTEGRAL_GB_RUNTIME_SAVE_RETURN_HEADER_SIZE];
    header[0] = INTEGRAL_GB_RUNTIME_PACKET_SAVE_RETURN;
    put_u32(header + 1, (uint32_t)size_long);
    if (wait_write_all(server->client_fd, header, sizeof(header)) != 0) {
        fclose(in);
        close_client(server);
        return -1;
    }

    uint8_t buffer[8192];
    unsigned remaining = (unsigned)size_long;
    while (remaining > 0) {
        unsigned chunk = remaining > sizeof(buffer) ? (unsigned)sizeof(buffer) : remaining;
        if (fread(buffer, 1, chunk, in) != chunk) {
            fclose(in);
            return -1;
        }
        if (wait_write_all(server->client_fd, buffer, chunk) != 0) {
            fclose(in);
            close_client(server);
            return -1;
        }
        remaining -= chunk;
    }
    fclose(in);
    server->save_returns_sent++;
    printf("  slot2 save return: sent %ld byte(s)\n", size_long);
    return 0;
}

void integral_gb_runtime_stream_server_close(IntegralGBRuntimeStreamServer *server)
{
    close_client(server);
    if (INTEGRAL_GB_RUNTIME_SOCKET_IS_VALID(server->listen_fd)) {
        integral_gb_runtime_socket_close(server->listen_fd);
        server->listen_fd = INTEGRAL_GB_RUNTIME_INVALID_SOCKET;
    }
    server->active = false;
    integral_gb_runtime_net_shutdown();
}

void integral_gb_runtime_stream_server_print_stats(const IntegralGBRuntimeStreamServer *server)
{
    if (!server || !server->active) {
        return;
    }
    printf("  remote input packets: %u\n", server->packets_received);
    printf("  remote video frames queued: %u\n", server->frames_queued);
    printf("  remote video frames sent: %u\n", server->frames_sent);
    printf("  remote audio packets queued: %u\n", server->audio_packets_queued);
    printf("  remote audio packets sent: %u\n", server->audio_packets_sent);
    printf("  slot2 uploads received: %u\n", server->slot2_uploads_received);
    printf("  slot2 save returns sent: %u\n", server->save_returns_sent);
}

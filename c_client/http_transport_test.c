/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    ParsedUrl parsed;
    IntegralConnection connection;
    char error[256] = "";
    char response[128] = "";
    if (argc != 3) return 2;
    size_t capacity = (size_t)atoi(argv[2]);
    if (capacity < 1 || capacity > sizeof(response)) return 2;
    if (parse_http_url(argv[1], &parsed, error, sizeof(error)) != 0) {
        printf("URL %s\n", error);
        return 3;
    }
    if (connection_open(&parsed, &connection, error, sizeof(error)) != 0) {
        printf("OPEN %s\n", error);
        return 4;
    }
    const char request[] = "GET / HTTP/1.0\r\n\r\n";
    if (connection_send_all(&connection, request, sizeof(request) - 1) != 0) {
        connection_close(&connection);
        puts("SEND");
        return 5;
    }
    int rc = connection_read_response(&connection, response, capacity);
    connection_close(&connection);
    if (rc != 0) {
        puts("READ");
        return 6;
    }
    printf("OK %zu %s\n", strlen(response), response);
    return 0;
}

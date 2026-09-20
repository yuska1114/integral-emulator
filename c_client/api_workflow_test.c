/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "client_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t read_candidate(const char *path, unsigned char *out, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    size_t size = fread(out, 1, capacity, file);
    int extra = fgetc(file);
    fclose(file);
    return extra == EOF ? size : 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) return 2;
    const char *url = argv[1], *token = argv[2], *mode = argv[3];
    char error[256] = "", state[128] = "";
    int rc = -1;
    if (!strcmp(mode, "login-version")) {
        char result_token[512], username[128];
        int change = 0, allow_import = 0;
        rc = integral_api_login(url, "version-test", "synthetic-password", "primary",
            result_token, sizeof(result_token), username, sizeof(username), &change,
            &allow_import, error, sizeof(error));
        if (!strcmp(argv[4], "ok")) {
            if (rc || strcmp(result_token, "synthetic-token") || strcmp(username, "version-test")) return 1;
        } else if (rc != 426 || strcmp(error, "ASK SERVER ADMIN FOR SUPPORTED VERSION") || result_token[0]) {
            return 1;
        }
        puts(INTEGRAL_CLIENT_MACHINE_VERSION);
        return 0;
    }
    if (!strcmp(mode, "start")) {
        rc = integral_api_start_room(url, token, 16, "trade", state, sizeof(state), error, sizeof(error));
    } else if (!strcmp(mode, "receipt")) {
        unsigned char digest[32];
        memset(digest, 0x77, sizeof(digest));
        rc = integral_api_gb_runtime_fixed_host_submit_terminal_receipt(
            url, token, argv[4], 9001, digest, state, sizeof(state), error, sizeof(error));
    } else if (!strcmp(mode, "finish") && argc == 9) {
        unsigned char host[128 * 1024], remote[128 * 1024], digest[32];
        size_t hs = read_candidate(argv[7], host, sizeof(host));
        size_t rs = read_candidate(argv[8], remote, sizeof(remote));
        if (!hs || !rs) return 2;
        memset(digest, 0x77, sizeof(digest));
        rc = integral_api_gb_runtime_fixed_host_submit_host_finish(
            url, token, argv[4], argv[5], atoll(argv[6]), 9001, digest,
            host, hs, remote, rs, state, sizeof(state), error, sizeof(error));
    } else if (!strcmp(mode, "snapshot")) {
        unsigned char host[128 * 1024], remote[128 * 1024];
        size_t hs = 123, rs = 123;
        memset(host, 0x55, sizeof(host)); memset(remote, 0x55, sizeof(remote));
        rc = integral_api_gb_runtime_fixed_host_download_snapshots(
            url, token, "test-session", host, sizeof(host), &hs, remote, sizeof(remote), &rs,
            error, sizeof(error));
        if (!strcmp(argv[4], "ok")) {
            if (rc || hs != sizeof(host) || rs != sizeof(remote)) return 1;
            for (size_t i = 0; i < hs; ++i) if (host[i] != 'A' || remote[i] != 'B') return 1;
        } else {
            size_t expected_host = !strcmp(argv[4], "remote-error") ? sizeof(host) : 0;
            if (rc == 0 || hs != expected_host || rs) return 1;
            for (size_t i = 0; i < sizeof(host); ++i) if (host[i] || remote[i]) return 1;
        }
        puts("snapshot boundary PASS");
        return 0;
    } else if (!strcmp(mode, "mobile")) {
        IntegralMobileRuntimeContract contract;
        char mobile[128] = "", game[128] = "";
        long long fence = 0;
        integral_mobile_runtime_contract_init(&contract);
        rc = integral_api_start_mobile_session_contract(url, token, "save", "rom", "request",
            "scenario", mobile, sizeof(mobile), game, sizeof(game), &fence,
            &contract, error, sizeof(error));
        integral_mobile_runtime_contract_free(&contract);
        if (rc != atoi(argv[4])) return 1;
        puts("mobile boundary PASS");
        return 0;
    } else return 2;
    if (rc) { fprintf(stderr, "%s\n", error); return 1; }
    puts(state);
    return 0;
}

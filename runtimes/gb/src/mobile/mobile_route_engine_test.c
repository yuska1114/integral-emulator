/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_route_engine.h"
#include "../server/content_hash.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#define mkdir_one(path) _mkdir(path)
#define process_id() _getpid()
#else
#include <sys/stat.h>
#include <unistd.h>
#define mkdir_one(path) mkdir(path, 0700)
#define process_id() getpid()
#endif

static int write_file(const char *path, const void *data, size_t size, char hash[65]) {
    FILE *file = fopen(path, "wb");
    if (!file || fwrite(data, 1, size, file) != size || fclose(file)) return -1;
    unsigned char digest[32];
    integral_gb_runtime_content_sha256(data, size, digest);
    integral_gb_runtime_content_sha256_hex(digest, hash);
    return 0;
}

static void digest_text(const char *text, char hash[65]) {
    unsigned char digest[32];
    integral_gb_runtime_content_sha256(text, strlen(text), digest);
    integral_gb_runtime_content_sha256_hex(digest, hash);
}

static int open_http(IntegralGBRuntimeMobileRouteEngine *engine, unsigned connection,
                     const struct mobile_addr4 *address) {
    return integral_gb_runtime_mobile_route_sock_open(
               engine, connection, MOBILE_SOCKTYPE_TCP, MOBILE_ADDRTYPE_IPV4, 0) &&
           integral_gb_runtime_mobile_route_sock_connect(
               engine, connection, (const struct mobile_addr *)address) == 1 ? 0 : -1;
}

int main(void) {
    char directory[128];
    snprintf(directory, sizeof(directory), "route-engine-test-%ld", (long)process_id());
    if (mkdir_one(directory)) {
        perror("create route-engine test directory");
        return 1;
    }
    static const char dns[] = "{\"schema_version\":1,\"routes\":[{\"name\":\"numbers.invalid\",\"endpoint\":\"mobile-gateway\"}]}";
    static const char http[] = "{\"schema_version\":1,\"initial_state\":{\"connected\":false},\"routes\":[{\"host\":\"numbers.invalid\",\"method\":\"GET\",\"path\":\"/connect\",\"requires\":{\"connected\":false},\"sets\":{\"connected\":true},\"status\":204,\"headers\":{}},{\"host\":\"numbers.invalid\",\"method\":\"GET\",\"path\":\"/number\",\"requires\":{\"connected\":true},\"sets\":{},\"status\":200,\"http_version\":\"1.0\",\"headers\":{},\"body\":{\"content_id\":\"number\",\"offset\":0,\"length\":2}}]}";
    static const char content[] = "42";
    char dns_path[192], http_path[192], content_path[192], manifest_path[192];
    snprintf(dns_path, sizeof(dns_path), "%s/dns.json", directory);
    snprintf(http_path, sizeof(http_path), "%s/http.json", directory);
    snprintf(content_path, sizeof(content_path), "%s/content.bin", directory);
    snprintf(manifest_path, sizeof(manifest_path), "%s/session.manifest", directory);
    char dh[65], hh[65], ch[65];
    if (write_file(dns_path, dns, sizeof(dns)-1u, dh) || write_file(http_path, http, sizeof(http)-1u, hh) || write_file(content_path, content, sizeof(content)-1u, ch)) return 1;
    FILE *manifest = fopen(manifest_path, "wb");
    if (!manifest) return 1;
    fprintf(manifest, "schema=2\nadapter=gb_mobile_v2\npackage=test\nrelease=1\npackage_digest=%064d\nruntime_capability=2\nartifact_count=3\n", 0);
    fprintf(manifest, "artifact.0.role=dns_routes\nartifact.0.content_id=dns\nartifact.0.path=%s\nartifact.0.size=%zu\nartifact.0.sha256=%s\n", dns_path, sizeof(dns)-1u, dh);
    fprintf(manifest, "artifact.1.role=http_routes\nartifact.1.content_id=http\nartifact.1.path=%s\nartifact.1.size=%zu\nartifact.1.sha256=%s\n", http_path, sizeof(http)-1u, hh);
    fprintf(manifest, "artifact.2.role=content_number\nartifact.2.content_id=number\nartifact.2.path=%s\nartifact.2.size=%zu\nartifact.2.sha256=%s\n", content_path, sizeof(content)-1u, ch);
    if (fclose(manifest)) return 1;
    IntegralGBRuntimeMobileSessionManifest loaded; char error[160];
    if (integral_gb_runtime_mobile_session_manifest_load(manifest_path, &loaded, error, sizeof(error))) { fprintf(stderr, "%s\n", error); return 1; }
    IntegralGBRuntimeMobileRouteEngine *engine = integral_gb_runtime_mobile_route_engine_new(&loaded, error, sizeof(error));
    if (!engine) { fprintf(stderr, "%s\n", error); return 1; }
    struct mobile_addr4 http_addr = {.type=MOBILE_ADDRTYPE_IPV4,.port=80,.host={127,0,0,1}};
    char response[1024]; struct mobile_addr peer;
    if (!integral_gb_runtime_mobile_route_sock_open(engine, 0, MOBILE_SOCKTYPE_TCP, MOBILE_ADDRTYPE_IPV4, 0) || integral_gb_runtime_mobile_route_sock_connect(engine, 0, (struct mobile_addr *)&http_addr) != 1) return 1;
    static const char connect_request[] = "GET /connect HTTP/1.1\r\nHost: numbers.invalid\r\n\r\n";
    if (integral_gb_runtime_mobile_route_sock_send(engine,0,connect_request,sizeof(connect_request)-1u,NULL)<0 || integral_gb_runtime_mobile_route_sock_recv(engine,0,response,sizeof(response),&peer)<=0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine,0);
    if (!integral_gb_runtime_mobile_route_sock_open(engine, 0, MOBILE_SOCKTYPE_TCP, MOBILE_ADDRTYPE_IPV4, 0) || integral_gb_runtime_mobile_route_sock_connect(engine, 0, (struct mobile_addr *)&http_addr) != 1) return 1;
    static const char number_request[] = "GET /number HTTP/1.1\r\nHost: numbers.invalid\r\n\r\n";
    int received = integral_gb_runtime_mobile_route_sock_send(engine,0,number_request,sizeof(number_request)-1u,NULL)<0 ? -1 : integral_gb_runtime_mobile_route_sock_recv(engine,0,response,sizeof(response)-1u,&peer);
    if (received <= 0) return 1;
    response[received]=0;
    if (!strstr(response,"HTTP/1.0 200") || !strstr(response,"\r\n\r\n42")) return 1;
    integral_gb_runtime_mobile_route_engine_free(engine);

    char authorization_hash[65];
    digest_text("Synthetic response", authorization_hash);
    char http_v2[4096];
    int http_v2_size = snprintf(
        http_v2, sizeof(http_v2),
        "{\"schema_version\":2,\"initial_state\":{\"authenticated\":false},\"routes\":["
        "{\"host\":\"numbers.invalid\",\"method\":\"GET\",\"path\":\"/auth\","
        "\"requires\":{\"authenticated\":false},\"sets\":{},\"status\":401,"
        "\"request_headers\":[{\"name\":\"Authorization\",\"presence\":\"absent\"}],"
        "\"max_uses\":2,\"headers\":{\"WWW-Authenticate\":\"Synthetic challenge\"}},"
        "{\"host\":\"numbers.invalid\",\"method\":\"GET\",\"path\":\"/auth\","
        "\"requires\":{\"authenticated\":false},\"sets\":{\"authenticated\":true},\"status\":200,"
        "\"request_headers\":[{\"name\":\"Authorization\",\"presence\":\"present\","
        "\"max_length\":128,\"value_sha256\":\"%s\"}],\"max_uses\":2,\"headers\":{}},"
        "{\"host\":\"numbers.invalid\",\"method\":\"POST\",\"path\":\"/record\","
        "\"requires\":{\"authenticated\":true},\"sets\":{},\"status\":200,"
        "\"request_headers\":[{\"name\":\"Content-Type\",\"presence\":\"present\","
        "\"max_length\":32,\"value_exact\":\"application/x-test\"}],"
        "\"request_body\":{\"min_length\":4,\"max_length\":4,\"slices\":[{\"offset\":0,"
        "\"fixed_base64\":\"QUI=\"}]},\"same_body_replay\":true,\"max_uses\":4,\"discard_body\":4,"
        "\"headers\":{},\"body\":{\"content_id\":\"number\",\"offset\":0,\"length\":2}}]}",
        authorization_hash);
    if (http_v2_size <= 0 || (size_t)http_v2_size >= sizeof(http_v2) ||
        write_file(http_path, http_v2, (size_t)http_v2_size, hh)) return 1;
    manifest = fopen(manifest_path, "wb");
    if (!manifest) return 1;
    fprintf(manifest, "schema=2\nadapter=gb_mobile_v2\npackage=test\nrelease=2\npackage_digest=%064d\nruntime_capability=3\nartifact_count=3\n", 0);
    fprintf(manifest, "artifact.0.role=dns_routes\nartifact.0.content_id=dns\nartifact.0.path=%s\nartifact.0.size=%zu\nartifact.0.sha256=%s\n", dns_path, sizeof(dns)-1u, dh);
    fprintf(manifest, "artifact.1.role=http_routes\nartifact.1.content_id=http\nartifact.1.path=%s\nartifact.1.size=%d\nartifact.1.sha256=%s\n", http_path, http_v2_size, hh);
    fprintf(manifest, "artifact.2.role=content_number\nartifact.2.content_id=number\nartifact.2.path=%s\nartifact.2.size=%zu\nartifact.2.sha256=%s\n", content_path, sizeof(content)-1u, ch);
    if (fclose(manifest)) return 1;
    if (integral_gb_runtime_mobile_session_manifest_load(manifest_path, &loaded, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    engine = integral_gb_runtime_mobile_route_engine_new(&loaded, error, sizeof(error));
    if (!engine) { fprintf(stderr, "%s\n", error); return 1; }

    static const char challenge_request[] = "GET /auth HTTP/1.1\r\nHost: numbers.invalid\r\n\r\n";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, challenge_request, sizeof(challenge_request)-1u, NULL) < 0) return 1;
    received = integral_gb_runtime_mobile_route_sock_recv(engine, 0, response, sizeof(response)-1u, &peer);
    if (received <= 0) return 1;
    response[received] = 0;
    if (!strstr(response, "HTTP/1.1 401")) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, challenge_request, sizeof(challenge_request)-1u, NULL) < 0 ||
        integral_gb_runtime_mobile_route_sock_recv(engine, 0, response, sizeof(response), &peer) <= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, challenge_request, sizeof(challenge_request)-1u, NULL) >= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);

    static const char wrong_auth[] = "GET /auth HTTP/1.1\r\nHost: numbers.invalid\r\nAuthorization: wrong\r\n\r\n";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, wrong_auth, sizeof(wrong_auth)-1u, NULL) >= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);
    static const char valid_auth[] = "GET /auth HTTP/1.1\r\nHost: numbers.invalid\r\nAuthorization: Synthetic response\r\n\r\n";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, valid_auth, sizeof(valid_auth)-1u, NULL) < 0 ||
        integral_gb_runtime_mobile_route_sock_recv(engine, 0, response, sizeof(response), &peer) <= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);

    static const char duplicate_header[] = "POST /record HTTP/1.1\r\nHost: numbers.invalid\r\nContent-Type: application/x-test\r\nContent-Type: application/x-test\r\nContent-Length: 4\r\n\r\nABCD";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, duplicate_header, sizeof(duplicate_header)-1u, NULL) >= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);

    static const char record_headers[] = "POST /record HTTP/1.1\r\nHost: numbers.invalid\r\nContent-Type: application/x-test\r\nContent-Length: 4\r\n\r\n";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, record_headers, sizeof(record_headers)-1u, NULL) < 0 ||
        integral_gb_runtime_mobile_route_sock_recv(engine, 0, response, sizeof(response), &peer) != 0 ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, "ABCD", 4, NULL) < 0) return 1;
    received = integral_gb_runtime_mobile_route_sock_recv(engine, 0, response, sizeof(response)-1u, &peer);
    if (received <= 0) return 1;
    response[received] = 0;
    if (!strstr(response, "HTTP/1.1 200") || !strstr(response, "\r\n\r\n42")) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);

    static const char record_same[] = "POST /record HTTP/1.1\r\nHost: numbers.invalid\r\nContent-Type: application/x-test\r\nContent-Length: 4\r\n\r\nABCD";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, record_same, sizeof(record_same)-1u, NULL) < 0 ||
        integral_gb_runtime_mobile_route_sock_recv(engine, 0, response, sizeof(response), &peer) <= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);
    static const char record_changed[] = "POST /record HTTP/1.1\r\nHost: numbers.invalid\r\nContent-Type: application/x-test\r\nContent-Length: 4\r\n\r\nABEF";
    if (open_http(engine, 0, &http_addr) ||
        integral_gb_runtime_mobile_route_sock_send(engine, 0, record_changed, sizeof(record_changed)-1u, NULL) >= 0) return 1;
    integral_gb_runtime_mobile_route_sock_close(engine, 0);
    IntegralGBRuntimeMobileRouteStats stats;
    integral_gb_runtime_mobile_route_stats(engine, &stats);
    if (stats.http_requests != 8u || stats.matched_responses != 5u ||
        stats.unauthorized_responses != 2u || stats.body_requests != 3u ||
        stats.body_bytes != 12u || stats.replay_responses != 1u ||
        integral_gb_runtime_mobile_route_error_count(engine) != 4u) {
        fprintf(stderr, "unexpected route stats: http=%u matched=%u unauthorized=%u body=%u bytes=%llu replay=%u errors=%u\n",
                stats.http_requests, stats.matched_responses, stats.unauthorized_responses,
                stats.body_requests, (unsigned long long)stats.body_bytes,
                stats.replay_responses, integral_gb_runtime_mobile_route_error_count(engine));
        return 1;
    }
    integral_gb_runtime_mobile_route_engine_free(engine);

    remove(manifest_path); remove(content_path); remove(http_path); remove(dns_path);
#ifdef _WIN32
    _rmdir(directory);
#else
    rmdir(directory);
#endif
    puts("mobile route engine tests passed");
    return 0;
}

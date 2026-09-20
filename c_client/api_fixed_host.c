/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "http_client_internal.h"
#include "http_payload_codec.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

 int integral_api_gb_runtime_fixed_host_get_manifest(const char *server_url,
                                    const char *token,
                                    const char *session_id,
                                    char *role_out,
                                    size_t role_out_size,
                                    char *manifest_digest_out,
                                    size_t manifest_digest_out_size,
                                    char *host_game_type_out,
                                    size_t host_game_type_out_size,
                                    char *remote_game_type_out,
                                    size_t remote_game_type_out_size,
                                    char *host_platform_out,
                                    size_t host_platform_out_size,
                                    char *remote_platform_out,
                                    size_t remote_platform_out_size,
                                    char *host_header_title_out,
                                    size_t host_header_title_out_size,
                                    char *remote_header_title_out,
                                    size_t remote_header_title_out_size,
                                    char *runtime_build_id_out,
                                    size_t runtime_build_id_out_size,
                                    char *state_out,
                                    size_t state_out_size,
                                    unsigned *pause_remaining_seconds_out,
                                    char *blocked_reason_out, size_t blocked_reason_size,
                                    char *error_out,
                                    size_t error_out_size)
{
    char path[192];
    unsigned char json[INTEGRAL_API_CONTROL_JSON_MAX + 1u];
    size_t json_size = 0u;
    int result = -1;
    if (blocked_reason_out && blocked_reason_size) blocked_reason_out[0] = '\0';
    if (role_out && role_out_size) role_out[0] = '\0';
    if (manifest_digest_out && manifest_digest_out_size)
        manifest_digest_out[0] = '\0';
    if (host_game_type_out && host_game_type_out_size)
        host_game_type_out[0] = '\0';
    if (remote_game_type_out && remote_game_type_out_size)
        remote_game_type_out[0] = '\0';
    if (host_platform_out && host_platform_out_size) host_platform_out[0] = '\0';
    if (remote_platform_out && remote_platform_out_size) remote_platform_out[0] = '\0';
    if (host_header_title_out && host_header_title_out_size) host_header_title_out[0] = '\0';
    if (remote_header_title_out && remote_header_title_out_size) remote_header_title_out[0] = '\0';
    if (runtime_build_id_out && runtime_build_id_out_size)
        runtime_build_id_out[0] = '\0';
    if (state_out && state_out_size) state_out[0] = '\0';
    if (pause_remaining_seconds_out) *pause_remaining_seconds_out = 0u;
    if (!http_safe_path_token(session_id) || role_out == NULL || role_out_size == 0u ||
        manifest_digest_out == NULL || manifest_digest_out_size == 0u ||
        host_game_type_out == NULL || host_game_type_out_size == 0u ||
        remote_game_type_out == NULL || remote_game_type_out_size == 0u ||
        host_platform_out == NULL || host_platform_out_size == 0u ||
        remote_platform_out == NULL || remote_platform_out_size == 0u ||
        host_header_title_out == NULL || host_header_title_out_size == 0u ||
        remote_header_title_out == NULL || remote_header_title_out_size == 0u ||
        runtime_build_id_out == NULL || runtime_build_id_out_size == 0u ||
        state_out == NULL || state_out_size == 0u ||
        pause_remaining_seconds_out == NULL) {
        http_set_error(error_out, error_out_size, "FIXED HOST MANIFEST INVALID");
        return -1;
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/manifest", session_id);
    if (api_control_json_body_request(
            "GET", server_url, path, token, "", json, sizeof(json) - 1u, &json_size,
            error_out, error_out_size) == 0) {
        json[json_size] = '\0';
        if (extract_json_string((char *)json, "role", role_out, role_out_size) == 0 &&
            extract_json_string((char *)json, "manifest_digest", manifest_digest_out,
                                manifest_digest_out_size) == 0 &&
            extract_json_string((char *)json, "host_game_type", host_game_type_out,
                                host_game_type_out_size) == 0 &&
            extract_json_string((char *)json, "remote_game_type", remote_game_type_out,
                                remote_game_type_out_size) == 0 &&
            extract_json_string((char *)json, "host_platform", host_platform_out,
                                host_platform_out_size) == 0 &&
            extract_json_string((char *)json, "remote_platform", remote_platform_out,
                                remote_platform_out_size) == 0 &&
            extract_json_string((char *)json, "host_rom_header_title", host_header_title_out,
                                host_header_title_out_size) == 0 &&
            extract_json_string((char *)json, "remote_rom_header_title", remote_header_title_out,
                                remote_header_title_out_size) == 0 &&
            extract_json_string((char *)json, "runtime_build_id", runtime_build_id_out,
                                runtime_build_id_out_size) == 0 &&
            extract_json_string((char *)json, "state", state_out, state_out_size) == 0) {
            int remaining = 0;
            if (blocked_reason_out && blocked_reason_size)
                (void)extract_json_string((char *)json, "blocked_reason", blocked_reason_out, blocked_reason_size);
            if (extract_json_int((char *)json, "pause_remaining_seconds", &remaining) == 0 &&
                remaining >= 0) {
                *pause_remaining_seconds_out = (unsigned)remaining;
                result = 0;
            }
            else {
                http_set_error(error_out, error_out_size,
                          "FIXED HOST PAUSE DEADLINE INVALID");
            }
        }
        else {
            http_set_error(error_out, error_out_size,
                      "FIXED HOST MANIFEST RESPONSE INVALID");
        }
    }
    http_secure_wipe(json, sizeof(json));
    return result;
}

int integral_api_gb_runtime_fixed_host_block(const char *server, const char *token,
    const char *session_id, const char *reason, char *error, size_t error_size)
{
    char path[192], body[128], response[4096];
    if (strcmp(reason, "rtc_save_required") && strcmp(reason, "rom_unreadable")) return -1;
    if (snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/blocked", session_id) >= (int)sizeof(path)) return -1;
    snprintf(body, sizeof(body), "{\"reason\":\"%s\"}", reason);
    return api_post_json(server, path, token, body, response, sizeof(response), error, error_size);
}

int integral_api_gb_runtime_fixed_host_submit_preflight(const char *server_url,
                                        const char *token,
                                        const char *session_id,
                                        const char *manifest_digest,
                                        const char *runtime_build_id,
                                        const char *game_type_a,
                                        const char *platform_a,
                                        const char *header_title_a,
                                        const char *game_type_b,
                                        const char *platform_b,
                                        const char *header_title_b,
                                        char *state_out,
                                        size_t state_out_size,
                                        char *error_out,
                                        size_t error_out_size)
{
    char path[192];
    char digest[96];
    char build[192];
    char game_type_a_json[64];
    char game_type_b_json[64];
    char platform_a_json[16];
    char platform_b_json[16];
    char header_a_json[64];
    char header_b_json[64];
    char body[1024];
    char response[INTEGRAL_API_CONTROL_JSON_MAX + INTEGRAL_HTTP_HEADER_MAX + 1u];
    int result = -1;
    if (state_out && state_out_size) state_out[0] = '\0';
    if (!http_safe_path_token(session_id) || manifest_digest == NULL ||
        runtime_build_id == NULL || game_type_a == NULL || game_type_b == NULL ||
        platform_a == NULL || platform_b == NULL ||
        header_title_a == NULL || header_title_b == NULL ||
        state_out == NULL || state_out_size == 0u) {
        http_set_error(error_out, error_out_size, "FIXED HOST PREFLIGHT INVALID");
        return -1;
    }
    json_escape(manifest_digest, digest, sizeof(digest));
    json_escape(runtime_build_id, build, sizeof(build));
    json_escape(game_type_a, game_type_a_json, sizeof(game_type_a_json));
    json_escape(game_type_b, game_type_b_json, sizeof(game_type_b_json));
    json_escape(platform_a, platform_a_json, sizeof(platform_a_json));
    json_escape(platform_b, platform_b_json, sizeof(platform_b_json));
    json_escape(header_title_a, header_a_json, sizeof(header_a_json));
    json_escape(header_title_b, header_b_json, sizeof(header_b_json));
    if (game_type_a[0] == '\0' && game_type_b[0] == '\0') {
        snprintf(body, sizeof(body),
                 "{\"manifest_digest\":\"%s\",\"protocol_id\":\"gb_runtime_fixed_host_v1\","
                 "\"runtime_build_id\":\"%s\",\"available_roms\":[]}",
                 digest, build);
    }
    else if (strcmp(game_type_a, game_type_b) == 0 &&
             strcmp(platform_a, platform_b) == 0 &&
             strcmp(header_title_a, header_title_b) == 0) {
        snprintf(body, sizeof(body),
                 "{\"manifest_digest\":\"%s\",\"protocol_id\":\"gb_runtime_fixed_host_v1\","
                 "\"runtime_build_id\":\"%s\",\"available_roms\":[{\"game_type\":\"%s\",\"platform\":\"%s\",\"rom_header_title\":\"%s\"}]}",
                 digest, build, game_type_a_json, platform_a_json, header_a_json);
    }
    else {
        snprintf(body, sizeof(body),
                 "{\"manifest_digest\":\"%s\",\"protocol_id\":\"gb_runtime_fixed_host_v1\","
                 "\"runtime_build_id\":\"%s\",\"available_roms\":[{\"game_type\":\"%s\",\"platform\":\"%s\",\"rom_header_title\":\"%s\"},{\"game_type\":\"%s\",\"platform\":\"%s\",\"rom_header_title\":\"%s\"}]}",
                 digest, build, game_type_a_json, platform_a_json, header_a_json,
                 game_type_b_json, platform_b_json, header_b_json);
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/preflight", session_id);
    if (api_post_json(server_url, path, token, body, response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "state", state_out, state_out_size) == 0) {
        result = 0;
    }
    else if (error_out && error_out_size && error_out[0] == '\0') {
        http_set_error(error_out, error_out_size,
                  "FIXED HOST PREFLIGHT RESPONSE INVALID");
    }
    http_secure_wipe(body, sizeof(body));
    http_secure_wipe(response, sizeof(response));
    return result;
}

int integral_api_gb_runtime_fixed_host_issue_relay_ticket(const char *server_url,
                                          const char *token,
                                          const char *session_id,
                                          bool resume,
                                          char *relay_host_out,
                                          size_t relay_host_out_size,
                                          unsigned *relay_port_out,
                                          char *relay_transport_out,
                                          size_t relay_transport_out_size,
                                          char *role_out,
                                          size_t role_out_size,
                                          char *scope_out,
                                          size_t scope_out_size,
                                          char *ticket_out,
                                          size_t ticket_out_size,
                                          char *save_policy_out,
                                          size_t save_policy_out_size,
                                          char *error_out,
                                          size_t error_out_size)
{
    char path[192];
    char response[INTEGRAL_API_CONTROL_JSON_MAX + INTEGRAL_HTTP_HEADER_MAX + 1u];
    int relay_port = 0;
    int result = -1;
    if (relay_transport_out && relay_transport_out_size) relay_transport_out[0] = '\0';
    if (!http_safe_path_token(session_id) || relay_host_out == NULL ||
        relay_port_out == NULL || role_out == NULL || scope_out == NULL ||
        relay_transport_out == NULL || relay_transport_out_size == 0u ||
        ticket_out == NULL || save_policy_out == NULL || save_policy_out_size == 0u) {
        http_set_error(error_out, error_out_size, "FIXED HOST RELAY REQUEST INVALID");
        return -1;
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/relay-ticket", session_id);
    if (api_post_json(server_url, path, token, resume ? "{\"resume\":true}" : "{}", response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "relay_host", relay_host_out,
                            relay_host_out_size) == 0 &&
        extract_json_int(response, "relay_port", &relay_port) == 0 &&
        relay_port > 0 && relay_port <= 65535 &&
        extract_json_string(response, "relay_transport", relay_transport_out,
                            relay_transport_out_size) == 0 &&
        (strcmp(relay_transport_out, "tls") == 0 ||
         strcmp(relay_transport_out, "plain") == 0) &&
        extract_json_string(response, "role", role_out, role_out_size) == 0 &&
        extract_json_string(response, "scope", scope_out, scope_out_size) == 0 &&
        extract_json_string(response, "ticket", ticket_out, ticket_out_size) == 0 &&
        extract_json_string(response, "save_policy", save_policy_out,
                            save_policy_out_size) == 0) {
        *relay_port_out = (unsigned)relay_port;
        result = 0;
    }
    else if (error_out && error_out_size && error_out[0] == '\0') {
        http_set_error(error_out, error_out_size, "FIXED HOST RELAY RESPONSE INVALID");
    }
    http_secure_wipe(response, sizeof(response));
    return result;
}

int integral_api_gb_runtime_fixed_host_download_snapshots(const char *server_url,
                                          const char *token,
                                          const char *session_id,
                                          unsigned char *host_save_out,
                                          size_t host_save_capacity,
                                          size_t *host_save_size_out,
                                          unsigned char *remote_save_out,
                                          size_t remote_save_capacity,
                                          size_t *remote_save_size_out,
                                          char *error_out,
                                          size_t error_out_size)
{
    const size_t json_capacity = 6u * 1024u * 1024u;
    const size_t response_capacity = json_capacity + INTEGRAL_HTTP_HEADER_MAX + 1u;
    char path[192];
    unsigned char *json = NULL;
    char *host_encoded = NULL, *remote_encoded = NULL;
    const char *host_section, *remote_section, *json_body;
    char save_policy[32];
    size_t json_size = 0u;
    int result = -1;
    if (host_save_size_out) *host_save_size_out = 0u;
    if (remote_save_size_out) *remote_save_size_out = 0u;
    if (!http_safe_path_token(session_id) || host_save_out == NULL ||
        host_save_size_out == NULL || remote_save_out == NULL ||
        remote_save_size_out == NULL) {
        http_set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT REQUEST INVALID");
        return -1;
    }
    json = malloc(response_capacity);
    host_encoded = malloc(json_capacity / 2u);
    remote_encoded = malloc(json_capacity / 2u);
    if (!json || !host_encoded || !remote_encoded) {
        http_set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT MEMORY FAILED");
        goto done;
    }
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/runtime-snapshots", session_id);
    if (api_json_request("GET", server_url, path, token, "", (char *)json,
                         response_capacity, error_out, error_out_size) != 0)
        goto done;
    json_body = http_body_start((char *)json, strlen((char *)json), &json_size);
    if (json_body == NULL || json_size == 0u || json_size > json_capacity) {
        http_set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT RESPONSE INVALID");
        goto done;
    }
    if (extract_json_string(json_body, "save_policy", save_policy,
                            sizeof(save_policy)) != 0 ||
        (strcmp(save_policy, "discard") != 0 &&
         strcmp(save_policy, "commit_pair") != 0)) {
        http_set_error(error_out, error_out_size, "FIXED HOST SAVE POLICY INVALID");
        goto done;
    }
    host_section = strstr(json_body, "\"host\"");
    remote_section = strstr(json_body, "\"remote\"");
    if (!host_section || !remote_section || host_section >= remote_section ||
        extract_json_string(host_section, "save_data", host_encoded,
                            json_capacity / 2u) != 0 ||
        extract_json_string(remote_section, "save_data", remote_encoded,
                            json_capacity / 2u) != 0 ||
        base64_decode(host_encoded, host_save_out, host_save_capacity,
                      host_save_size_out) != 0 ||
        base64_decode(remote_encoded, remote_save_out, remote_save_capacity,
                      remote_save_size_out) != 0) {
        http_set_error(error_out, error_out_size, "FIXED HOST SNAPSHOT RESPONSE INVALID");
        goto done;
    }
    result = 0;
done:
    if (json) { http_secure_wipe(json, response_capacity); free(json); }
    if (host_encoded) { http_secure_wipe(host_encoded, json_capacity / 2u); free(host_encoded); }
    if (remote_encoded) { http_secure_wipe(remote_encoded, json_capacity / 2u); free(remote_encoded); }
    if (result != 0) {
        if (host_save_out) http_secure_wipe(host_save_out, host_save_capacity);
        if (remote_save_out) http_secure_wipe(remote_save_out, remote_save_capacity);
    }
    return result;
}

static void gb_runtime_fixed_host_digest_hex(const uint8_t digest[32], char output[65])
{
    static const char HEX[] = "0123456789abcdef";
    for (size_t index = 0u; index < 32u; index++) {
        output[index * 2u] = HEX[digest[index] >> 4u];
        output[index * 2u + 1u] = HEX[digest[index] & 15u];
    }
    output[64] = '\0';
}

int integral_api_gb_runtime_fixed_host_submit_host_finish(
    const char *server_url, const char *token, const char *session_id,
    const char *game_session_id, long long fencing_token,
    uint64_t final_frame, const uint8_t terminal_digest[32],
    const uint8_t *host_candidate, size_t host_candidate_size,
    const uint8_t *remote_candidate, size_t remote_candidate_size,
    char *state_out, size_t state_out_size,
    char *error_out, size_t error_out_size)
{
    char path[192], digest[65], game_session[160];
    char *host_encoded = NULL, *remote_encoded = NULL, *body = NULL;
    char response[INTEGRAL_HTTP_HEADER_MAX + 4096u];
    size_t host_encoded_size, remote_encoded_size, body_size;
    int result = -1;
    if (state_out && state_out_size) state_out[0] = '\0';
    if (!http_safe_path_token(session_id) || !game_session_id || !terminal_digest ||
        !host_candidate || !remote_candidate || !host_candidate_size ||
        !remote_candidate_size || fencing_token <= 0 || final_frame == 0u ||
        !state_out || !state_out_size) {
        http_set_error(error_out, error_out_size, "FIXED HOST FINISH INVALID");
        return -1;
    }
    host_encoded_size = ((host_candidate_size + 2u) / 3u) * 4u + 1u;
    remote_encoded_size = ((remote_candidate_size + 2u) / 3u) * 4u + 1u;
    body_size = host_encoded_size + remote_encoded_size + 512u;
    host_encoded = malloc(host_encoded_size);
    remote_encoded = malloc(remote_encoded_size);
    body = malloc(body_size);
    if (!host_encoded || !remote_encoded || !body) {
        http_set_error(error_out, error_out_size, "FIXED HOST FINISH MEMORY FAILED");
        goto done;
    }
    base64_encode(host_candidate, host_candidate_size, host_encoded, host_encoded_size);
    base64_encode(remote_candidate, remote_candidate_size, remote_encoded, remote_encoded_size);
    gb_runtime_fixed_host_digest_hex(terminal_digest, digest);
    json_escape(game_session_id, game_session, sizeof(game_session));
    snprintf(body, body_size,
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld,"
             "\"final_frame\":%llu,\"terminal_digest\":\"%s\","
             "\"host_candidate\":\"%s\",\"remote_candidate\":\"%s\"}",
             game_session, fencing_token, (unsigned long long)final_frame, digest,
             host_encoded, remote_encoded);
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/host-finish", session_id);
    if (api_post_json(server_url, path, token, body, response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "state", state_out, state_out_size) == 0) result = 0;
    else if (error_out && error_out_size && !error_out[0])
        http_set_error(error_out, error_out_size, "FIXED HOST FINISH RESPONSE INVALID");
done:
    http_secure_wipe(response, sizeof(response)); http_secure_wipe(digest, sizeof(digest));
    if (body) { http_secure_wipe(body, body_size); free(body); }
    if (host_encoded) { http_secure_wipe(host_encoded, host_encoded_size); free(host_encoded); }
    if (remote_encoded) { http_secure_wipe(remote_encoded, remote_encoded_size); free(remote_encoded); }
    return result;
}

int integral_api_gb_runtime_fixed_host_submit_terminal_receipt(
    const char *server_url, const char *token, const char *session_id,
    uint64_t final_frame, const uint8_t terminal_digest[32],
    char *state_out, size_t state_out_size,
    char *error_out, size_t error_out_size)
{
    char path[192], digest[65], body[256], response[INTEGRAL_HTTP_HEADER_MAX + 4096u];
    int result = -1;
    if (state_out && state_out_size) state_out[0] = '\0';
    if (!http_safe_path_token(session_id) || !terminal_digest || final_frame == 0u ||
        !state_out || !state_out_size) {
        http_set_error(error_out, error_out_size, "FIXED HOST RECEIPT INVALID");
        return -1;
    }
    gb_runtime_fixed_host_digest_hex(terminal_digest, digest);
    snprintf(body, sizeof(body), "{\"final_frame\":%llu,\"terminal_digest\":\"%s\"}",
             (unsigned long long)final_frame, digest);
    snprintf(path, sizeof(path), "/gb-runtime-fixed-host-sessions/%s/terminal-receipt", session_id);
    if (api_post_json(server_url, path, token, body, response, sizeof(response),
                      error_out, error_out_size) == 0 &&
        extract_json_string(response, "state", state_out, state_out_size) == 0) result = 0;
    else if (error_out && error_out_size && !error_out[0])
        http_set_error(error_out, error_out_size, "FIXED HOST RECEIPT RESPONSE INVALID");
    http_secure_wipe(response, sizeof(response)); http_secure_wipe(body, sizeof(body));
    http_secure_wipe(digest, sizeof(digest)); return result;
}

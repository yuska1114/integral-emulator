/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 3 ||
        (strcmp(argv[2], "tls") != 0 && strcmp(argv[2], "plain") != 0)) {
        fprintf(stderr, "usage: %s SERVER_URL tls|plain\n", argv[0]);
        return 2;
    }
    unsigned char data[16];
    size_t data_size = 0;
    int revision = 0;
    char error[160] = {0};
    char token[160] = {0};
    char authenticated_username[64] = {0};
    int must_change_password = 0;
    int allow_user_initial_save_import = 0;
    if (integral_api_login(
            argv[1], "mixedcase_user", "password123", "primary",
            token, sizeof(token), authenticated_username,
            sizeof(authenticated_username), &must_change_password,
            &allow_user_initial_save_import, error, sizeof(error)) != 0 ||
        strcmp(token, "current-token") != 0 ||
        strcmp(authenticated_username, "MixedCase_User") != 0 ||
        must_change_password || allow_user_initial_save_import) {
        fprintf(stderr, "login display username response failed: %s\n", error);
        return 1;
    }
    IntegralApiRoom matched_room;
    int has_current_room = 0;
    if (integral_api_create_room(argv[1], "current-token", "link_cable",
                                 &matched_room, error, sizeof(error)) != 0 ||
        matched_room.room_number != 7 ||
        strcmp(matched_room.room_code, "48291") != 0 ||
        strcmp(matched_room.room_type, "link_cable") != 0 ||
        !matched_room.creator) {
        fprintf(stderr, "ROOM create response failed: %s\n", error);
        return 1;
    }
    if (integral_api_get_current_room(argv[1], "current-token", &matched_room,
                                      &has_current_room, error, sizeof(error)) != 0 ||
        !has_current_room || matched_room.room_number != 7) {
        fprintf(stderr, "ROOM current response failed: %s\n", error);
        return 1;
    }
    if (integral_api_join_room_code(argv[1], "current-token", "48291",
                                    &matched_room, error, sizeof(error)) != 0 ||
        matched_room.room_number != 65 ||
        strcmp(matched_room.room_type, "n64") != 0 || matched_room.creator) {
        fprintf(stderr, "ROOM join response failed: %s\n", error);
        return 1;
    }
    char applied_rom_id[96] = {0};
    char applied_save_id[96] = {0};
    int requires_confirmation = 0;
    if (integral_api_apply_rom_slot(
            argv[1], "current-token", 6u, "sample.z64",
            "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
            "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", "n64", "JP",
            "SAMPLE N64",
            NULL, 0u, 0, 0, applied_rom_id, sizeof(applied_rom_id),
            applied_save_id, sizeof(applied_save_id), &requires_confirmation,
            error, sizeof(error)) != 0 || requires_confirmation ||
        strcmp(applied_rom_id, "rom-slot6") != 0 ||
        strcmp(applied_save_id, "save-slot6") != 0) {
        fprintf(stderr, "ROM slot-specific apply response failed: %s\n", error);
        return 1;
    }
    if (integral_api_download_n64_runtime_save(argv[1],
                                          "current-token",
                                          "current-session",
                                          "n64",
                                          data,
                                          sizeof(data),
                                          &data_size,
                                          &revision,
                                          error,
                                          sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (data_size != 3u || memcmp(data, "abc", 3u) != 0 || revision != 7) {
        fprintf(stderr, "unexpected save response\n");
        return 1;
    }
    char n64_media_session[96] = {0};
    char n64_relay_host[64] = {0};
    char n64_relay_transport[8] = {0};
    char n64_role[16] = {0};
    char n64_scope[32] = {0};
    char n64_ticket[64] = {0};
    unsigned n64_relay_port = 0u;
    if (integral_api_start_n64_room(
            argv[1], "current-token", 65u, NULL,
            n64_media_session, sizeof(n64_media_session),
            n64_relay_host, sizeof(n64_relay_host), &n64_relay_port,
            n64_relay_transport, sizeof(n64_relay_transport),
            n64_role, sizeof(n64_role), n64_scope, sizeof(n64_scope),
            n64_ticket, sizeof(n64_ticket), error, sizeof(error)) != 0 ||
        strcmp(n64_media_session, "n64-media-current") != 0 ||
        strcmp(n64_relay_host, "relay.example") != 0 ||
        n64_relay_port != 25164u || strcmp(n64_relay_transport, argv[2]) != 0 ||
        strcmp(n64_role, "remote") != 0 ||
        strcmp(n64_scope, "n64_runtime_media") != 0) {
        fprintf(stderr, "N64 ROOM media relay config failed: %s\n", error);
        return 1;
    }
    IntegralApiHeartbeatStatus heartbeat;
    if (integral_api_room_heartbeat_status(
            argv[1], "current-token", &heartbeat, error, sizeof(error)) != 0 ||
        strcmp(heartbeat.lifecycle_kind, "link") != 0 ||
        strcmp(heartbeat.lifecycle_session_id, "completed-session") != 0 ||
        strcmp(heartbeat.lifecycle_status, "CANCELLED") != 0 ||
        strcmp(heartbeat.expires_at, "2026-09-04T01:00:00+00:00") != 0 ||
        strcmp(heartbeat.termination_reason, "game stopped") != 0) {
        fprintf(stderr, "unexpected ROOM lifecycle response: %s\n", error);
        return 1;
    }
    const char *n64_save_ids[] = {"save-n64", "save-gb"};
    char n64_game_session_id[96] = {0};
    long long n64_fencing_token = 0;
    if (integral_api_start_game_with_saves(
            argv[1], "current-token",
            INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT,
            n64_save_ids, 2u,
            n64_game_session_id, sizeof(n64_game_session_id),
            &n64_fencing_token, error, sizeof(error)) != 0 ||
        strcmp(n64_game_session_id, "game-n64_runtime-1") != 0 ||
        n64_fencing_token != 64) {
        fprintf(stderr, "N64 Runtime game start failed: %s\n", error);
        return 1;
    }
    char mobile_session_id[96] = {0};
    char game_session_id[96] = {0};
    long long fencing_token = 0;
    IntegralMobileRuntimeContract mobile_contract;
    IntegralApiMobileScenario scenarios[INTEGRAL_API_MOBILE_SCENARIOS_MAX];
    unsigned scenario_count = 0;
    if (integral_api_list_mobile_scenarios(
            argv[1], "current-token", "save-1", "rom-1",
            scenarios, &scenario_count, error, sizeof(error)) != 0 ||
        scenario_count != 2u || strcmp(scenarios[0].scenario_id, "scenario_alpha") != 0 ||
        strcmp(scenarios[1].display_name, "SYNTHETIC BETA") != 0 ||
        !scenarios[1].is_default) {
        fprintf(stderr, "unexpected Mobile scenarios response: %s\n", error);
        return 1;
    }
    if (integral_api_start_mobile_session_contract(
            argv[1], "current-token", "save-1", "rom-1", "mobile-create:current-1", "default",
            mobile_session_id, sizeof(mobile_session_id),
            game_session_id, sizeof(game_session_id), &fencing_token,
            &mobile_contract, error, sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    if (strcmp(mobile_session_id, "mobile-1") != 0 ||
        strcmp(game_session_id, "game-1") != 0 || fencing_token != 11 ||
        strcmp(mobile_contract.package_id, "synthetic_numbers") != 0 ||
        mobile_contract.artifact_count != 1u ||
        mobile_contract.artifacts[0].size != 3u ||
        memcmp(mobile_contract.artifacts[0].data, "abc", 3u) != 0) {
        fprintf(stderr, "unexpected Mobile response\n");
        return 1;
    }
    integral_mobile_runtime_contract_free(&mobile_contract);
    if (integral_api_start_mobile_session_contract(
            argv[1], "current-token", "save-1", "rom-1",
            "mobile-create:current-conflict", "auth-conflict",
            mobile_session_id, sizeof(mobile_session_id),
            game_session_id, sizeof(game_session_id), &fencing_token,
            &mobile_contract, error, sizeof(error)) !=
            INTEGRAL_API_MOBILE_CREATE_AUTH_SESSION_CONFLICT) {
        fprintf(stderr, "Mobile auth-session conflict was not classified: %s\n", error);
        return 1;
    }
    if (integral_api_start_mobile_session_contract(
            argv[1], "current-token", "save-1", "rom-1",
            "mobile-create:current-aborted", "aborted",
            mobile_session_id, sizeof(mobile_session_id),
            game_session_id, sizeof(game_session_id), &fencing_token,
            &mobile_contract, error, sizeof(error)) !=
            INTEGRAL_API_MOBILE_CREATE_ABORTED) {
        fprintf(stderr, "Mobile aborted create was not classified: %s\n", error);
        return 1;
    }
    if (integral_api_complete_mobile_session(
            argv[1], "current-token", mobile_session_id, game_session_id,
            fencing_token, error, sizeof(error)) != 0) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    char fixed_role[16] = {0};
    char fixed_digest[65] = {0};
    char gb_runtime_fixed_host_title[16] = {0};
    char fixed_remote_title[16] = {0};
    char fixed_host_platform[4] = {0};
    char fixed_remote_platform[4] = {0};
    char fixed_host_header[21] = {0};
    char fixed_remote_header[21] = {0};
    char fixed_runtime_build[64] = {0};
    char fixed_state[24] = {0};
    unsigned fixed_pause_remaining = 0u;
    if (integral_api_gb_runtime_fixed_host_get_manifest(
            argv[1], "current-token", "fixed-session", fixed_role,
            sizeof(fixed_role), fixed_digest, sizeof(fixed_digest),
            gb_runtime_fixed_host_title, sizeof(gb_runtime_fixed_host_title), fixed_remote_title,
            sizeof(fixed_remote_title), fixed_host_platform, sizeof(fixed_host_platform),
            fixed_remote_platform, sizeof(fixed_remote_platform),
            fixed_host_header, sizeof(fixed_host_header),
            fixed_remote_header, sizeof(fixed_remote_header), fixed_runtime_build,
            sizeof(fixed_runtime_build), fixed_state, sizeof(fixed_state),
            &fixed_pause_remaining,
            NULL, 0,
            error, sizeof(error)) != 0 || strcmp(fixed_role, "host") != 0 ||
        strcmp(fixed_digest,
               "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd") != 0 ||
        strcmp(gb_runtime_fixed_host_title, "catalog_alpha") != 0 ||
        strcmp(fixed_remote_title, "catalog_beta") != 0 ||
        strcmp(fixed_host_header, "ALPHA CORE") != 0 ||
        strcmp(fixed_remote_header, "BETA CORE") != 0 ||
        strcmp(fixed_runtime_build, "integral-gb-runtime-fixed-host-v2") != 0 ||
        strcmp(fixed_state, "PREFLIGHT") != 0) {
        fprintf(stderr, "fixed Host manifest failed: %s\n", error);
        return 1;
    }
    if (integral_api_gb_runtime_fixed_host_submit_preflight(
            argv[1], "current-token", "fixed-session", fixed_digest,
            "integral-gb-runtime-fixed-host-v2", gb_runtime_fixed_host_title,
            fixed_host_platform, fixed_host_header,
            fixed_remote_title, fixed_remote_platform, fixed_remote_header,
            fixed_state, sizeof(fixed_state),
            error, sizeof(error)) != 0 || strcmp(fixed_state, "READY") != 0) {
        fprintf(stderr, "fixed Host preflight failed: %s\n", error);
        return 1;
    }
    char fixed_game_session_id[64] = {0};
    long long fixed_fencing_token = 0;
    if (integral_api_get_link_game_fence(
            argv[1], "current-token", "fixed-session",
            fixed_game_session_id, sizeof(fixed_game_session_id),
            &fixed_fencing_token, error, sizeof(error)) != 0 ||
        strcmp(fixed_game_session_id, "game-fixed-1") != 0 ||
        fixed_fencing_token != 77) {
        fprintf(stderr, "fixed Host game fence failed: %s\n", error);
        return 1;
    }
    char fixed_relay_host[64] = {0};
    char fixed_relay_transport[8] = {0};
    char fixed_ticket_role[16] = {0};
    char fixed_scope[48] = {0};
    char fixed_ticket[64] = {0};
    char fixed_save_policy[32] = {0};
    unsigned fixed_relay_port = 0u;
    if (integral_api_gb_runtime_fixed_host_issue_relay_ticket(
            argv[1], "current-token", "fixed-session", false,
            fixed_relay_host, sizeof(fixed_relay_host), &fixed_relay_port,
            fixed_relay_transport, sizeof(fixed_relay_transport),
            fixed_ticket_role, sizeof(fixed_ticket_role), fixed_scope,
            sizeof(fixed_scope), fixed_ticket, sizeof(fixed_ticket),
            fixed_save_policy, sizeof(fixed_save_policy),
            error, sizeof(error)) != 0 ||
        strcmp(fixed_relay_host, "relay.example") != 0 ||
        fixed_relay_port != 25164u || strcmp(fixed_relay_transport, argv[2]) != 0 ||
        strcmp(fixed_ticket_role, "host") != 0 ||
        strcmp(fixed_scope, "gb-runtime-fixed-host-media-v1") != 0 ||
        strcmp(fixed_save_policy, "discard") != 0) {
        fprintf(stderr, "fixed Host relay config failed: %s\n", error);
        return 1;
    }
    unsigned char gb_runtime_fixed_host_save[16], fixed_remote_save[16];
    size_t gb_runtime_fixed_host_save_size = 0u, fixed_remote_save_size = 0u;
    if (integral_api_gb_runtime_fixed_host_download_snapshots(
            argv[1], "current-token", "fixed-session",
            gb_runtime_fixed_host_save, sizeof(gb_runtime_fixed_host_save), &gb_runtime_fixed_host_save_size,
            fixed_remote_save, sizeof(fixed_remote_save), &fixed_remote_save_size,
            error, sizeof(error)) != 0 || gb_runtime_fixed_host_save_size != 3u ||
        fixed_remote_save_size != 3u || memcmp(gb_runtime_fixed_host_save, "abc", 3u) != 0 ||
        memcmp(fixed_remote_save, "def", 3u) != 0) {
        fprintf(stderr, "fixed Host snapshots failed: %s\n", error);
        return 1;
    }
    return 0;
}

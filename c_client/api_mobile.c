/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "http_client_internal.h"
#include "http_payload_codec.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTEGRAL_MOBILE_RESPONSE_MAX (45u * 1024u * 1024u)

int integral_api_list_mobile_scenarios(
    const char *server_url,
    const char *token,
    const char *save_id,
    const char *rom_id,
    IntegralApiMobileScenario *scenarios,
    unsigned *scenario_count,
    char *error_out,
    size_t error_out_size)
{
    if (!server_url || !token || !save_id || !save_id[0] || !rom_id || !rom_id[0] ||
        !scenarios || !scenario_count) {
        http_set_error(error_out, error_out_size, "MOBILE SCENARIO REQUEST INVALID");
        return -1;
    }
    *scenario_count = 0;
    char body[256];
    snprintf(body, sizeof(body), "{\"save_id\":\"%s\",\"rom_id\":\"%s\"}", save_id, rom_id);
    char response[INTEGRAL_API_CONTROL_JSON_MAX];
    if (api_post_json(server_url, "/mobile-scenarios", token, body, response,
                      sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    const char *cursor = strstr(response, "\"scenarios\"");
    cursor = cursor ? strchr(cursor, '[') : NULL;
    if (!cursor) {
        http_set_error(error_out, error_out_size, "MOBILE SCENARIO RESPONSE INVALID");
        return -1;
    }
    cursor++;
    while (*cursor && *cursor != ']') {
        const char *object_start = strchr(cursor, '{');
        if (!object_start) break;
        const char *array_end = strchr(cursor, ']');
        if (array_end && object_start > array_end) break;
        const char *object_end = strchr(object_start, '}');
        if (!object_end || (size_t)(object_end - object_start) >= 512u ||
            *scenario_count >= INTEGRAL_API_MOBILE_SCENARIOS_MAX) {
            http_set_error(error_out, error_out_size, "MOBILE SCENARIO RESPONSE INVALID");
            return -1;
        }
        char object[512];
        size_t object_size = (size_t)(object_end - object_start + 1);
        memcpy(object, object_start, object_size);
        object[object_size] = '\0';
        IntegralApiMobileScenario *item = &scenarios[*scenario_count];
        memset(item, 0, sizeof(*item));
        if (extract_json_string(object, "scenario_id", item->scenario_id, sizeof(item->scenario_id)) != 0 ||
            extract_json_string(object, "display_name", item->display_name, sizeof(item->display_name)) != 0 ||
            extract_json_string(object, "release_id", item->release_id, sizeof(item->release_id)) != 0 ||
            extract_json_bool(object, "default", &item->is_default) != 0 ||
            !http_safe_path_token(item->scenario_id)) {
            http_set_error(error_out, error_out_size, "MOBILE SCENARIO RESPONSE INVALID");
            return -1;
        }
        (*scenario_count)++;
        cursor = object_end + 1;
    }
    if (*scenario_count == 0u) {
        http_set_error(error_out, error_out_size, "NO MOBILE SCENARIO AVAILABLE");
        return -1;
    }
    return 0;
}

int integral_api_start_mobile_session_contract(
    const char *server_url,
    const char *token,
    const char *save_id,
    const char *rom_id,
    const char *create_request_id,
    const char *scenario_id,
    char *mobile_session_id_out,
    size_t mobile_session_id_out_size,
    char *game_session_id_out,
    size_t game_session_id_out_size,
    long long *fencing_token_out,
    IntegralMobileRuntimeContract *contract_out,
    char *error_out,
    size_t error_out_size)
{
    if (!save_id || !save_id[0] || !rom_id || !rom_id[0] ||
        !create_request_id || !create_request_id[0] || !scenario_id || !scenario_id[0] ||
        !mobile_session_id_out || !game_session_id_out || !fencing_token_out ||
        !contract_out) {
        http_set_error(error_out, error_out_size, "MOBILE SESSION REQUEST INVALID");
        return -1;
    }
    char body[512];
    snprintf(body, sizeof(body),
             "{\"save_id\":\"%s\",\"rom_id\":\"%s\",\"request_id\":\"%s\",\"scenario_id\":\"%s\"}",
             save_id, rom_id, create_request_id, scenario_id);
    char *response = malloc(INTEGRAL_MOBILE_RESPONSE_MAX);
    if (!response) {
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    integral_mobile_runtime_contract_init(contract_out);
    int rc = api_post_json(server_url, "/mobile-sessions", token, body,
                           response, INTEGRAL_MOBILE_RESPONSE_MAX,
                           error_out, error_out_size);
    if (rc != 0 && strstr(response, "mobile_create_auth_session_conflict") != NULL) {
        http_set_error(error_out, error_out_size,
                  "MOBILE SESSION IS BOUND TO ANOTHER LOGIN; RETRY AFTER EXPIRY");
        integral_mobile_runtime_contract_free(contract_out);
        free(response);
        return INTEGRAL_API_MOBILE_CREATE_AUTH_SESSION_CONFLICT;
    }
    if (rc != 0 && strstr(response, "mobile_create_aborted") != NULL) {
        http_set_error(error_out, error_out_size,
                  "PREVIOUS MOBILE CREATE WAS ABORTED; RETRY START");
        integral_mobile_runtime_contract_free(contract_out);
        free(response);
        return INTEGRAL_API_MOBILE_CREATE_ABORTED;
    }
    const char *contract_json = strstr(response, "\r\n\r\n");
    contract_json = contract_json ? contract_json + 4 : response;
    char mobile_status[32] = {0};
    if (rc == 0 && strstr(contract_json, "\"runtime_contract\"") == NULL &&
        extract_json_string(response, "status", mobile_status, sizeof(mobile_status)) == 0 &&
        (strcmp(mobile_status, "COMPLETED") == 0 ||
         strcmp(mobile_status, "CANCELLED") == 0 ||
         strcmp(mobile_status, "EXPIRED") == 0 ||
         strcmp(mobile_status, "FAILED") == 0)) {
        http_set_error(error_out, error_out_size, "PREVIOUS MOBILE CREATE IS TERMINAL; RETRY START");
        free(response);
        return INTEGRAL_API_MOBILE_CREATE_ABORTED;
    }
    if (rc == 0 &&
        (extract_json_string(response, "id", mobile_session_id_out, mobile_session_id_out_size) != 0 ||
         extract_json_string(response, "game_session_id", game_session_id_out, game_session_id_out_size) != 0 ||
         extract_json_int64(response, "fencing_token", fencing_token_out) != 0 ||
         integral_mobile_runtime_contract_parse(contract_json, contract_out, error_out, error_out_size) != 0)) {
        integral_mobile_runtime_contract_free(contract_out);
        if (!error_out || !error_out[0]) {
            http_set_error(error_out, error_out_size, "MOBILE SESSION RESPONSE INVALID");
        }
        rc = -1;
    }
    free(response);
    return rc;
}

int integral_api_heartbeat_mobile_session(const char *server_url,
                                     const char *token,
                                     const char *mobile_session_id,
                                     const char *game_session_id,
                                     long long fencing_token,
                                     char *error_out,
                                     size_t error_out_size)
{
    if (!mobile_session_id || !mobile_session_id[0] || !game_session_id ||
        !game_session_id[0] || fencing_token <= 0) {
        http_set_error(error_out, error_out_size, "MOBILE SESSION FENCE REQUIRED");
        return -1;
    }
    char path[192];
    char body[320];
    char response[8192];
    snprintf(path, sizeof(path), "/mobile-sessions/%s/heartbeat", mobile_session_id);
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_complete_mobile_session(const char *server_url,
                                    const char *token,
                                    const char *mobile_session_id,
                                    const char *game_session_id,
                                    long long fencing_token,
                                    char *error_out,
                                    size_t error_out_size)
{
    if (!mobile_session_id || !mobile_session_id[0] || !game_session_id ||
        !game_session_id[0] || fencing_token <= 0) {
        http_set_error(error_out, error_out_size, "MOBILE COMPLETE REQUEST INVALID");
        return -1;
    }
    char body[320];
    snprintf(body, sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id, fencing_token);
    char path[192];
    char response[8192];
    snprintf(path, sizeof(path), "/mobile-sessions/%s/complete", mobile_session_id);
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_cancel_mobile_session(const char *server_url,
                                  const char *token,
                                  const char *mobile_session_id,
                                  const char *game_session_id,
                                  long long fencing_token,
                                  const char *reason,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (!mobile_session_id || !mobile_session_id[0] || !game_session_id ||
        !game_session_id[0] || fencing_token <= 0) {
        http_set_error(error_out, error_out_size, "MOBILE SESSION FENCE REQUIRED");
        return -1;
    }
    char path[192];
    char body[512];
    char response[8192];
    snprintf(path, sizeof(path), "/mobile-sessions/%s/cancel", mobile_session_id);
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld,\"reason\":\"%s\"}",
             game_session_id,
             fencing_token,
             reason && reason[0] ? reason : "client canceled");
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

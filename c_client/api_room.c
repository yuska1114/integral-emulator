/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "http_client_internal.h"
#include "http_payload_codec.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void clear_room(IntegralApiRoom *room)
{
    memset(room, 0, sizeof(*room));
}

static int valid_room_code(const char *code)
{
    if (!code || strlen(code) != 5 || code[0] < '1' || code[0] > '9') return 0;
    for (size_t i = 1; i < 5; i++) {
        if (code[i] < '0' || code[i] > '9') return 0;
    }
    return 1;
}

static int parse_room_object(const char *object, IntegralApiRoom *room)
{
    clear_room(room);
    int room_number = 0;
    if (extract_json_int(object, "room_number", &room_number) != 0 ||
        room_number < 1 || room_number > INTEGRAL_API_ROOMS) return -1;
    room->room_number = (unsigned)room_number;
    (void)extract_json_string(object, "room_code", room->room_code, sizeof(room->room_code));
    (void)extract_json_string(object, "room_type", room->room_type, sizeof(room->room_type));
    (void)extract_json_bool(object, "creator", &room->creator);
    (void)extract_json_string(object, "link_session_id", room->link_session_id, sizeof(room->link_session_id));
    (void)extract_json_string(object, "link_mode", room->link_mode, sizeof(room->link_mode));
    (void)extract_json_bool(object, "game_started", &room->game_started);

    if (room->room_code[0] && !valid_room_code(room->room_code)) return -1;
    if (room->room_type[0]) {
        bool link_room = strcmp(room->room_type, "link_cable") == 0 && room_number <= 64;
        bool n64_room = strcmp(room->room_type, "n64") == 0 && room_number >= 65 && room_number <= 128;
        if (!link_room && !n64_room) return -1;
    }

    const char *users_key = strstr(object, "\"users\"");
    const char *users = users_key ? strchr(users_key, '[') : NULL;
    const char *users_end = users ? json_matching_end(users, '[', ']') : NULL;
    const char *p = users ? users + 1 : NULL;
    unsigned user_index = 0;
    while (p && users_end && p < users_end && user_index < 2) {
        const char *user_start = strchr(p, '{');
        const char *user_end = user_start ? json_matching_end(user_start, '{', '}') : NULL;
        if (!user_start || !user_end || user_end > users_end) break;
        size_t user_size = (size_t)(user_end - user_start + 1);
        char *user_json = malloc(user_size + 1);
        if (!user_json) return -1;
        memcpy(user_json, user_start, user_size);
        user_json[user_size] = '\0';
        char *username = user_index == 0 ? room->user1 : room->user2;
        char *slot = user_index == 0 ? room->slot1 : room->slot2;
        char *n64_slot = user_index == 0 ? room->n64_slot1 : room->n64_slot2;
        char *slot_filename = user_index == 0 ? room->slot_filename1 : room->slot_filename2;
        char *slot_game_type = user_index == 0 ? room->slot_game_type1 : room->slot_game_type2;
        char *slot_header_title = user_index == 0 ? room->slot_header_title1 : room->slot_header_title2;
        (void)extract_json_string(user_json, "username", username, INTEGRAL_API_ROOM_USER_MAX);
        (void)extract_json_string(user_json, "slot", slot, INTEGRAL_API_ROOM_SLOT_MAX);
        (void)extract_json_string(user_json, "n64_slot", n64_slot, INTEGRAL_API_ROOM_SLOT_MAX);
        (void)extract_json_string(user_json, "slot_filename", slot_filename, INTEGRAL_API_ROOM_FILENAME_MAX);
        (void)extract_json_string(user_json, "slot_game_type", slot_game_type, INTEGRAL_API_ROOM_GAME_TYPE_MAX);
        (void)extract_json_string(user_json, "slot_rom_header_title", slot_header_title,
                                  INTEGRAL_API_ROM_HEADER_TITLE_MAX);
        int ready = 0;
        (void)extract_json_bool(user_json, "ready", &ready);
        if (user_index == 0) {
            room->ready1 = ready;
            (void)extract_json_string(user_json, "n64_slot_filename", room->n64_slot_filename1,
                                      sizeof(room->n64_slot_filename1));
            (void)extract_json_string(user_json, "n64_slot_game_type", room->n64_slot_game_type1,
                                      sizeof(room->n64_slot_game_type1));
            (void)extract_json_string(user_json, "n64_slot_rom_header_title",
                                      room->n64_slot_header_title1,
                                      sizeof(room->n64_slot_header_title1));
        }
        else room->ready2 = ready;
        free(user_json);
        user_index++;
        p = user_end + 1;
    }

    const char *chat_key = strstr(object, "\"chat\"");
    const char *chat = chat_key ? strchr(chat_key, '[') : NULL;
    const char *chat_end = chat ? json_matching_end(chat, '[', ']') : NULL;
    p = chat ? chat + 1 : NULL;
    while (p && chat_end && p < chat_end && room->chat_count < INTEGRAL_API_ROOM_CHAT_MAX) {
        const char *item_start = strchr(p, '{');
        const char *item_end = item_start ? json_matching_end(item_start, '{', '}') : NULL;
        if (!item_start || !item_end || item_end > chat_end) break;
        size_t item_size = (size_t)(item_end - item_start + 1);
        char *item_json = malloc(item_size + 1);
        if (!item_json) return -1;
        memcpy(item_json, item_start, item_size);
        item_json[item_size] = '\0';
        char username[INTEGRAL_API_ROOM_USER_MAX] = "USER";
        char message[INTEGRAL_API_ROOM_CHAT_TEXT_MAX] = "";
        (void)extract_json_string(item_json, "username", username, sizeof(username));
        if (extract_json_string(item_json, "message", message, sizeof(message)) == 0) {
            snprintf(room->chat[room->chat_count], sizeof(room->chat[room->chat_count]),
                     "%s: %s", username, message);
            room->chat_count++;
        }
        free(item_json);
        p = item_end + 1;
    }
    return 0;
}

int integral_api_parse_room_matching_response(const char *response,
                                         IntegralApiRoom *room_out,
                                         int *has_room_out)
{
    if (!response || !room_out || !has_room_out) return -1;
    clear_room(room_out);
    *has_room_out = 0;
    const char *room_key = strstr(response, "\"room\"");
    if (!room_key) return -1;
    const char *colon = strchr(room_key, ':');
    if (!colon) return -1;
    const char *value = skip_json_spaces(colon + 1);
    if (strncmp(value, "null", 4) == 0) return 0;
    if (*value != '{') return -1;
    const char *end = json_matching_end(value, '{', '}');
    if (!end) return -1;
    size_t size = (size_t)(end - value + 1);
    char *object = malloc(size + 1);
    if (!object) return -1;
    memcpy(object, value, size);
    object[size] = '\0';
    int result = parse_room_object(object, room_out);
    free(object);
    if (result != 0 || !valid_room_code(room_out->room_code) || !room_out->room_type[0]) return -1;
    *has_room_out = 1;
    return 0;
}

int integral_api_create_room(const char *server_url,
                        const char *token,
                        const char *mode,
                        IntegralApiRoom *room_out,
                        char *error_out,
                        size_t error_out_size)
{
    if (!mode || (strcmp(mode, "link_cable") != 0 && strcmp(mode, "n64") != 0)) {
        http_set_error(error_out, error_out_size, "INVALID ROOM MODE");
        return -1;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"mode\":\"%s\"}", mode);
    char response[65536];
    if (api_post_json(server_url, "/room-matching/create", token, body, response,
                      sizeof(response), error_out, error_out_size) != 0) return -1;
    int has_room = 0;
    if (integral_api_parse_room_matching_response(response, room_out, &has_room) != 0 || !has_room ||
        strcmp(room_out->room_type, mode) != 0 || !room_out->creator) {
        http_set_error(error_out, error_out_size, "INVALID CREATE ROOM RESPONSE");
        return -1;
    }
    return 0;
}

int integral_api_join_room_code(const char *server_url,
                           const char *token,
                           const char *room_code,
                           IntegralApiRoom *room_out,
                           char *error_out,
                           size_t error_out_size)
{
    if (!valid_room_code(room_code)) {
        http_set_error(error_out, error_out_size, "ROOM CODE MUST BE 5 DIGITS");
        return -1;
    }
    char body[64];
    snprintf(body, sizeof(body), "{\"room_code\":\"%s\"}", room_code);
    char response[65536];
    if (api_post_json(server_url, "/room-matching/join", token, body, response,
                      sizeof(response), error_out, error_out_size) != 0) return -1;
    int has_room = 0;
    if (integral_api_parse_room_matching_response(response, room_out, &has_room) != 0 || !has_room ||
        strcmp(room_out->room_code, room_code) != 0) {
        http_set_error(error_out, error_out_size, "INVALID JOIN ROOM RESPONSE");
        return -1;
    }
    return 0;
}

int integral_api_get_current_room(const char *server_url,
                             const char *token,
                             IntegralApiRoom *room_out,
                             int *has_room_out,
                             char *error_out,
                             size_t error_out_size)
{
    char response[65536];
    if (api_get_json(server_url, "/room-matching/current", token, response,
                     sizeof(response), error_out, error_out_size) != 0) return -1;
    if (integral_api_parse_room_matching_response(response, room_out, has_room_out) != 0) {
        http_set_error(error_out, error_out_size, "INVALID CURRENT ROOM RESPONSE");
        return -1;
    }
    return 0;
}

int integral_api_leave_room(const char *server_url,
                        const char *token,
                        char *error_out,
                        size_t error_out_size)
{
    char response[8192];
    if (api_post_json(server_url, "/room-matching/leave", token, "{}", response, sizeof(response), error_out, error_out_size) != 0) return -1;
    int left = 0;
    if (extract_json_bool(response, "left", &left) != 0 || !left) {
        http_set_error(error_out, error_out_size, "ROOM EXIT NOT CONFIRMED");
        return -1;
    }
    return 0;
}

static int terminate_n64_room(const char *server_url, const char *token,
                                   const char *session_id, const char *room_code,
                                   bool preflight_failed, char *error_out, size_t error_out_size)
{
    char path[256], body[96], response[8192];
    if (!session_id || !session_id[0] || !room_code || strlen(room_code) != 5 ||
        strspn(room_code, "0123456789") != 5 ||
        snprintf(path, sizeof(path), "/n64-runtime-media-sessions/%s/terminate", session_id) >= (int)sizeof(path)) {
        http_set_error(error_out, error_out_size, "N64 session ROOM binding missing");
        return -1;
    }
    snprintf(body, sizeof(body), "{\"room_code\":\"%s\"%s}", room_code,
             preflight_failed ? ",\"reason\":\"preflight_failed\"" : "");
    if (api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size) != 0) return -1;
    char status[24];
    if (extract_json_string(response, "status", status, sizeof(status)) != 0 ||
        (strcmp(status, "COMPLETED") && strcmp(status, "CANCELLED") && strcmp(status, "EXPIRED"))) {
        http_set_error(error_out, error_out_size, "N64 TERMINATION NOT CONFIRMED");
        return -1;
    }
    return 0;
}

int integral_api_terminate_n64_room(const char *server_url, const char *token,
    const char *session_id, const char *room_code, char *error_out, size_t error_out_size)
{
    return terminate_n64_room(server_url, token, session_id, room_code, false, error_out, error_out_size);
}

int integral_api_reject_n64_preflight(const char *server_url, const char *token,
    const char *session_id, const char *room_code, char *error_out, size_t error_out_size)
{
    return terminate_n64_room(server_url, token, session_id, room_code, true, error_out, error_out_size);
}

int integral_api_finish_n64_room(const char *server_url, const char *token,
                                const char *session_id, char *error_out,
                                size_t error_out_size)
{
    char path[256], response[8192];
    if (!session_id || !session_id[0] ||
        snprintf(path, sizeof(path), "/n64-runtime-media-sessions/%s/finish", session_id) >= (int)sizeof(path)) {
        http_set_error(error_out, error_out_size, "N64 SESSION REQUIRED");
        return -1;
    }
    int rc = api_post_json(server_url, path, token, "{}", response, sizeof(response), error_out, error_out_size);
    if (rc != 0) return -1;
    char status[32] = {0}, reason[64] = {0};
    if (extract_json_string(response, "status", status, sizeof(status)) != 0 ||
        extract_json_string(response, "termination_reason", reason, sizeof(reason)) != 0) return -1;
    return strcmp(status, "COMPLETED") == 0 && strcmp(reason, "host_finished") == 0 ? 0 : 1;
}

int integral_api_n64_media_state(const char *server_url, const char *token,
                                const char *session_id, int recover, IntegralApiHeartbeatStatus *state,
                                char *error_out, size_t error_out_size)
{
    char path[256], response[8192];
    memset(state, 0, sizeof(*state));
    if (!session_id || !session_id[0] ||
        snprintf(path, sizeof(path), "/n64-runtime-media-sessions/%s%s", session_id, recover ? "/recover" : "") >= (int)sizeof(path)) return -1;
    int rc = recover ? api_post_json(server_url, path, token, "{}", response, sizeof(response), error_out, error_out_size)
                     : api_get_json(server_url, path, token, response, sizeof(response), error_out, error_out_size);
    if (rc != 0) return -1;
    if (extract_json_string(response, "id", state->lifecycle_session_id, sizeof(state->lifecycle_session_id)) != 0 ||
        extract_json_string(response, "status", state->lifecycle_status, sizeof(state->lifecycle_status)) != 0) return -1;
    snprintf(state->lifecycle_kind, sizeof(state->lifecycle_kind), "media");
    (void)extract_json_string(response, "termination_reason", state->termination_reason, sizeof(state->termination_reason));
    return 0;
}

int integral_api_stop_game(const char *server_url,
                      const char *token,
                      const char *game_session_id,
                      long long fencing_token,
                      char *error_out,
                      size_t error_out_size)
{
    if (!game_session_id || game_session_id[0] == '\0' || fencing_token <= 0) {
        http_set_error(error_out, error_out_size, "GAME SESSION FENCE REQUIRED");
        return -1;
    }
    char body[320];
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    char response[8192];
    return api_post_json(server_url, "/game/stop", token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_stop_local_game(const char *server_url,
                            const char *token,
                            const char *game_session_id,
                            long long fencing_token,
                            char *error_out,
                            size_t error_out_size)
{
    if (!game_session_id || game_session_id[0] == '\0' || fencing_token <= 0) {
        http_set_error(error_out, error_out_size, "GAME SESSION FENCE REQUIRED");
        return -1;
    }
    char body[320];
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    char response[8192];
    return api_post_json(server_url, "/game/stop", token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_heartbeat_game(const char *server_url,
                           const char *token,
                           const char *game_session_id,
                           long long fencing_token,
                           char *error_out,
                           size_t error_out_size)
{
    if (!game_session_id || game_session_id[0] == '\0' || fencing_token <= 0) {
        http_set_error(error_out, error_out_size, "GAME SESSION FENCE REQUIRED");
        return -1;
    }
    char body[320];
    snprintf(body,
             sizeof(body),
             "{\"game_session_id\":\"%s\",\"fencing_token\":%lld}",
             game_session_id,
             fencing_token);
    char response[8192];
    return api_post_json(server_url, "/game/heartbeat", token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_get_link_game_fence(const char *server_url,
                                const char *token,
                                const char *expected_link_session_id,
                                char *game_session_id_out,
                                size_t game_session_id_out_size,
                                long long *fencing_token_out,
                                char *error_out,
                                size_t error_out_size)
{
    if (!server_url || !token || !expected_link_session_id ||
        expected_link_session_id[0] == '\0' || !game_session_id_out ||
        game_session_id_out_size == 0u || !fencing_token_out) {
        http_set_error(error_out, error_out_size, "GAME SESSION FENCE REQUEST INVALID");
        return -1;
    }
    game_session_id_out[0] = '\0';
    *fencing_token_out = 0;
    char response[8192];
    if (api_get_json(server_url, "/game/status", token, response,
                     sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    int active = 0;
    int owner = 0;
    char link_session_id[128] = {0};
    if (extract_json_bool(response, "active", &active) != 0 || !active ||
        extract_json_bool(response, "is_owner_auth_session", &owner) != 0 || !owner ||
        extract_json_string(response, "link_session_id", link_session_id,
                            sizeof(link_session_id)) != 0 ||
        strcmp(link_session_id, expected_link_session_id) != 0 ||
        extract_json_string(response, "game_session_id", game_session_id_out,
                            game_session_id_out_size) != 0 ||
        extract_json_int64(response, "fencing_token", fencing_token_out) != 0 ||
        *fencing_token_out <= 0) {
        game_session_id_out[0] = '\0';
        *fencing_token_out = 0;
        http_set_error(error_out, error_out_size, "GAME SESSION FENCE RESPONSE INVALID");
        return -1;
    }
    return 0;
}

int integral_api_start_local_game(const char *server_url,
                             const char *token,
                             const char *save_id1,
                             const char *save_id2,
                             char *game_session_id_out,
                             size_t game_session_id_out_size,
                             long long *fencing_token_out,
                             char *error_out,
                             size_t error_out_size)
{
    const char *save_ids[2];
    unsigned save_count = 0;
    save_ids[save_count++] = save_id1;
    if (save_id2 && save_id2[0] != '\0') {
        save_ids[save_count++] = save_id2;
    }
    return integral_api_start_game_with_saves(server_url,
                                         token,
                                         INTEGRAL_EXECUTION_MODE_LOCAL_CLIENT,
                                         save_ids,
                                         save_count,
                                         game_session_id_out,
                                         game_session_id_out_size,
                                         fencing_token_out,
                                         error_out,
                                         error_out_size);
}

int integral_api_start_game_with_saves(const char *server_url,
                                  const char *token,
                                  const char *execution_mode,
                                  const char *const *save_ids,
                                  unsigned save_count,
                                  char *game_session_id_out,
                                  size_t game_session_id_out_size,
                                  long long *fencing_token_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (game_session_id_out && game_session_id_out_size > 0) {
        game_session_id_out[0] = '\0';
    }
    if (fencing_token_out) {
        *fencing_token_out = 0;
    }
    if (!execution_mode || execution_mode[0] == '\0' || !save_ids || save_count == 0) {
        http_set_error(error_out, error_out_size, "SAVE ID REQUIRED");
        return -1;
    }
    char body[768];
    int n = snprintf(body,
                     sizeof(body),
                     "{\"execution_mode\":\"%s\",\"save_ids\":[",
                     execution_mode);
    if (n <= 0 || (size_t)n >= sizeof(body)) {
        http_set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    size_t used = (size_t)n;
    for (unsigned i = 0; i < save_count; i++) {
        if (!save_ids[i] || save_ids[i][0] == '\0') {
            http_set_error(error_out, error_out_size, "SAVE ID REQUIRED");
            return -1;
        }
        n = snprintf(body + used,
                     sizeof(body) - used,
                     "%s\"%s\"",
                     i == 0 ? "" : ",",
                     save_ids[i]);
        if (n <= 0 || (size_t)n >= sizeof(body) - used) {
            http_set_error(error_out, error_out_size, "REQUEST TOO LARGE");
            return -1;
        }
        used += (size_t)n;
    }
    n = snprintf(body + used, sizeof(body) - used, "]}");
    if (n <= 0 || (size_t)n >= sizeof(body) - used) {
        http_set_error(error_out, error_out_size, "REQUEST TOO LARGE");
        return -1;
    }
    char response[8192];
    int rc = api_post_json(server_url, "/game/start", token, body, response, sizeof(response), error_out, error_out_size);
    if (rc != 0) {
        return rc;
    }
    if (game_session_id_out && game_session_id_out_size > 0) {
        if (extract_json_string(response, "game_session_id", game_session_id_out, game_session_id_out_size) != 0) {
            http_set_error(error_out, error_out_size, "GAME SESSION ID MISSING");
            return -1;
        }
    }
    if (!fencing_token_out || extract_json_int64(response, "fencing_token", fencing_token_out) != 0 || *fencing_token_out <= 0) {
        http_set_error(error_out, error_out_size, "GAME SESSION FENCE MISSING");
        return -1;
    }
    return 0;
}

int integral_api_room_heartbeat(const char *server_url,
                            const char *token,
                            char *error_out,
                            size_t error_out_size)
{
    return integral_api_room_heartbeat_status(
        server_url, token, NULL, error_out, error_out_size
    );
}

int integral_api_room_heartbeat_status(const char *server_url,
                            const char *token,
                            IntegralApiHeartbeatStatus *status_out,
                            char *error_out,
                            size_t error_out_size)
{
    char response[8192];
    if (status_out) {
        memset(status_out, 0, sizeof(*status_out));
    }
    int rc = api_post_json(server_url, "/rooms/heartbeat", token, "{}", response, sizeof(response), error_out, error_out_size);
    if (rc != 0 || !status_out) {
        return rc;
    }
    (void)extract_json_string(response, "kind", status_out->lifecycle_kind, sizeof(status_out->lifecycle_kind));
    (void)extract_json_string(response, "session_id", status_out->lifecycle_session_id, sizeof(status_out->lifecycle_session_id));
    (void)extract_json_string(response, "status", status_out->lifecycle_status, sizeof(status_out->lifecycle_status));
    (void)extract_json_string(response, "termination_reason", status_out->termination_reason, sizeof(status_out->termination_reason));
    (void)extract_json_string(response, "expires_at", status_out->expires_at, sizeof(status_out->expires_at));
    (void)extract_json_int64(response, "unix_time", &status_out->server_unix_time);
    return 0;
}

int integral_api_update_room_state(const char *server_url,
                                    const char *token,
                                    unsigned room_number,
                                    const char *slot,
                                    int ready,
                                    const char *link_mode,
                                    char *error_out,
                                    size_t error_out_size)
{
    char path[80];
    char escaped_slot[64];
    char escaped_mode[64];
    json_escape(slot ? slot : "", escaped_slot, sizeof(escaped_slot));
    snprintf(path, sizeof(path), "/rooms/%u/state", room_number);
    char body[240];
    if (link_mode && link_mode[0] != '\0') {
        json_escape(link_mode, escaped_mode, sizeof(escaped_mode));
        snprintf(body,
                 sizeof(body),
                 "{\"slot\":\"%s\",\"ready\":%s,\"link_mode\":\"%s\"}",
                 escaped_slot,
                 ready ? "true" : "false",
                 escaped_mode);
    }
    else {
        snprintf(body,
                 sizeof(body),
                 "{\"slot\":\"%s\",\"ready\":%s}",
                 escaped_slot,
                 ready ? "true" : "false");
    }
    char response[8192];
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_update_n64_room_state(const char *server_url,
                                        const char *token,
                                        unsigned room_number,
                                        const char *slot,
                                        const char *n64_slot,
                                        int ready,
                                        char *error_out,
                                        size_t error_out_size)
{
    char path[80];
    char escaped_slot[64];
    char escaped_n64_slot[64];
    json_escape(slot ? slot : "", escaped_slot, sizeof(escaped_slot));
    json_escape(n64_slot ? n64_slot : "", escaped_n64_slot, sizeof(escaped_n64_slot));
    snprintf(path, sizeof(path), "/rooms/%u/state", room_number);
    char body[240];
    snprintf(body,
             sizeof(body),
             "{\"slot\":\"%s\",\"n64_slot\":\"%s\",\"ready\":%s}",
             escaped_slot,
             escaped_n64_slot,
             ready ? "true" : "false");
    char response[8192];
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_send_room_chat(const char *server_url,
                                 const char *token,
                                 unsigned room_number,
                                 const char *message,
                                 char *error_out,
                                 size_t error_out_size)
{
    char path[80];
    char escaped_message[384];
    json_escape(message ? message : "", escaped_message, sizeof(escaped_message));
    snprintf(path, sizeof(path), "/rooms/%u/chat", room_number);
    char body[480];
    snprintf(body, sizeof(body), "{\"message\":\"%s\"}", escaped_message);
    char response[8192];
    return api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
}

int integral_api_start_room(const char *server_url,
                             const char *token,
                             unsigned room_number,
                             const char *link_mode,
                             char *session_id_out,
                             size_t session_id_out_size,
                             char *error_out,
                             size_t error_out_size)
{
    if (session_id_out_size > 0) {
        session_id_out[0] = '\0';
    }
    char path[80];
    char escaped_mode[64];
    snprintf(path, sizeof(path), "/rooms/%u/start", room_number);
    json_escape(link_mode ? link_mode : "trade", escaped_mode, sizeof(escaped_mode));
    char body[96];
    snprintf(body, sizeof(body), "{\"link_mode\":\"%s\"}", escaped_mode);
    char response[8192];
    if (api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "id", session_id_out, session_id_out_size) != 0) {
        http_set_error(error_out, error_out_size, "ROOM START RESPONSE MISSING ID");
        return -1;
    }
    return 0;
}

int integral_api_start_n64_room(const char *server_url,
                                 const char *token,
                                 unsigned room_number,
                                 const char *expected_session_id,
                                 char *session_id_out,
                                 size_t session_id_out_size,
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
                                 char *error_out,
                                 size_t error_out_size)
{
    if (!relay_transport_out || relay_transport_out_size == 0u) {
        http_set_error(error_out, error_out_size, "N64 MEDIA START PARAMETERS INVALID");
        return -1;
    }
    if (session_id_out_size > 0) session_id_out[0] = '\0';
    if (relay_host_out_size > 0) relay_host_out[0] = '\0';
    if (relay_transport_out_size > 0) relay_transport_out[0] = '\0';
    if (role_out_size > 0) role_out[0] = '\0';
    if (scope_out_size > 0) scope_out[0] = '\0';
    if (ticket_out_size > 0) ticket_out[0] = '\0';
    if (relay_port_out) *relay_port_out = 0;
    char path[80];
    snprintf(path, sizeof(path), "/rooms/%u/start", room_number);
    char response[8192];
    char escaped_session[256], body[320];
    json_escape(expected_session_id ? expected_session_id : "", escaped_session, sizeof(escaped_session));
    snprintf(body, sizeof(body), "{\"expected_media_session_id\":\"%s\"}", escaped_session);
    if (api_post_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    int relay_port = 0;
    char lifecycle[24] = {0};
    if (extract_json_string(response, "status", lifecycle, sizeof(lifecycle)) == 0 &&
        (!strcmp(lifecycle, "COMPLETED") || !strcmp(lifecycle, "CANCELLED") ||
         !strcmp(lifecycle, "EXPIRED") || !strcmp(lifecycle, "FAILED"))) {
        (void)extract_json_string(response, "id", session_id_out, session_id_out_size);
        return 1;
    }
    if (extract_json_string(response, "id", session_id_out, session_id_out_size) != 0 ||
        extract_json_string(response, "relay_host", relay_host_out, relay_host_out_size) != 0 ||
        extract_json_int(response, "relay_port", &relay_port) != 0 || relay_port <= 0 || relay_port > 65535 ||
        extract_json_string(response, "relay_transport", relay_transport_out, relay_transport_out_size) != 0 ||
        (strcmp(relay_transport_out, "tls") != 0 && strcmp(relay_transport_out, "plain") != 0) ||
        extract_json_string(response, "role", role_out, role_out_size) != 0 ||
        extract_json_string(response, "scope", scope_out, scope_out_size) != 0 ||
        extract_json_string(response, "ticket", ticket_out, ticket_out_size) != 0) {
        http_set_error(error_out, error_out_size, "N64 MEDIA START RESPONSE INVALID");
        return -1;
    }
    if (relay_port_out) {
        *relay_port_out = (unsigned)relay_port;
    }
    return 0;
}

int integral_api_get_link_session_info(const char *server_url,
                                  const char *token,
                                  const char *session_id,
                                  char *status_out,
                                  size_t status_out_size,
                                  int *room_number_out,
                                  char *error_out,
                                  size_t error_out_size)
{
    if (status_out_size > 0) {
        status_out[0] = '\0';
    }
    if (room_number_out) {
        *room_number_out = 0;
    }
    char path[192];
    snprintf(path, sizeof(path), "/link-sessions/%s", session_id);
    char response[8192];
    if (api_get_json(server_url, path, token, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "status", status_out, status_out_size) != 0) {
        http_set_error(error_out, error_out_size, "LINK SESSION RESPONSE MISSING STATUS");
        return -1;
    }
    if (room_number_out) {
        (void)extract_json_int(response, "room_number", room_number_out);
    }
    return 0;
}

int integral_api_get_link_session_protocol(const char *server_url,
                                      const char *token,
                                      const char *session_id,
                                      char *protocol_id_out,
                                      size_t protocol_id_out_size,
                                      char *error_out,
                                      size_t error_out_size)
{
    char path[192];
    char response[8192];
    if (protocol_id_out && protocol_id_out_size) protocol_id_out[0] = '\0';
    if (!http_safe_path_token(session_id) || protocol_id_out == NULL ||
        protocol_id_out_size == 0u) {
        http_set_error(error_out, error_out_size, "LINK SESSION PROTOCOL INVALID");
        return -1;
    }
    snprintf(path, sizeof(path), "/link-sessions/%s", session_id);
    if (api_get_json(server_url, path, token, response, sizeof(response),
                     error_out, error_out_size) != 0 ||
        extract_json_string(response, "protocol_id", protocol_id_out,
                            protocol_id_out_size) != 0) {
        if (error_out && error_out_size && error_out[0] == '\0') {
            http_set_error(error_out, error_out_size,
                      "LINK SESSION RESPONSE MISSING PROTOCOL");
        }
        return -1;
    }
    return 0;
}

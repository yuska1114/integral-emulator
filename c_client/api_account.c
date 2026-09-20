/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "http_client_internal.h"
#include "http_payload_codec.h"
#include "client_version.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int integral_api_login(const char *server_url,
                  const char *username,
                  const char *password,
                  const char *server_id,
                  char *token_out,
                  size_t token_out_size,
                  char *authenticated_username_out,
                  size_t authenticated_username_out_size,
                  int *must_change_password_out,
                  int *allow_user_initial_save_import_out,
                  char *error_out,
                  size_t error_out_size)
{
    token_out[0] = '\0';
    if (authenticated_username_out && authenticated_username_out_size > 0) {
        authenticated_username_out[0] = '\0';
    }
    *must_change_password_out = 0;
    if (allow_user_initial_save_import_out) {
        *allow_user_initial_save_import_out = 0;
    }
    char escaped_user[160];
    char escaped_password[160];
    char escaped_server_id[64];
    json_escape(username, escaped_user, sizeof(escaped_user));
    json_escape(password, escaped_password, sizeof(escaped_password));
    json_escape(server_id && server_id[0] ? server_id : "primary", escaped_server_id, sizeof(escaped_server_id));

    char body[512];
    snprintf(body,
             sizeof(body),
             "{\"username\":\"%s\",\"password\":\"%s\",\"server_id\":\"%s\",\"client_version\":\"%s\"}",
             escaped_user,
             escaped_password,
             escaped_server_id,
             INTEGRAL_CLIENT_MACHINE_VERSION);

    char response[8192];
    int status = api_post_json(server_url, "/auth/login", NULL, body, response, sizeof(response), error_out, error_out_size);
    if (status != 0) {
        return status == 426 ? 426 : -1;
    }
    if (extract_json_string(response, "token", token_out, token_out_size) != 0) {
        http_set_error(error_out, error_out_size, "LOGIN RESPONSE MISSING TOKEN");
        return -1;
    }
    if (!authenticated_username_out || authenticated_username_out_size == 0 ||
        extract_json_string(response, "username", authenticated_username_out,
                            authenticated_username_out_size) != 0) {
        http_set_error(error_out, error_out_size, "LOGIN RESPONSE MISSING USERNAME");
        token_out[0] = '\0';
        return -1;
    }
    (void)extract_json_bool(response, "must_change_password", must_change_password_out);
    if (allow_user_initial_save_import_out) {
        (void)extract_json_bool(response,
                                "allow_user_initial_save_import",
                                allow_user_initial_save_import_out);
    }
    return 0;
}

int integral_api_change_password(const char *server_url,
                            const char *token,
                            const char *new_password,
                            char *error_out,
                            size_t error_out_size)
{
    char escaped_password[160];
    json_escape(new_password, escaped_password, sizeof(escaped_password));
    char body[224];
    snprintf(body, sizeof(body), "{\"new_password\":\"%s\"}", escaped_password);
    char response[8192];
    if (api_post_json(server_url, "/auth/change-password", token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    return 0;
}

int integral_api_get_server_time(const char *server_url,
                            long long *unix_time_out,
                            char *error_out,
                            size_t error_out_size)
{
    if (unix_time_out) {
        *unix_time_out = 0;
    }
    char response[2048];
    if (api_get_json(server_url, "/time", NULL, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (!unix_time_out || extract_json_int64(response, "unix_time", unix_time_out) != 0 || *unix_time_out <= 0) {
        http_set_error(error_out, error_out_size, "TIME RESPONSE MISSING UNIX TIME");
        return -1;
    }
    return 0;
}

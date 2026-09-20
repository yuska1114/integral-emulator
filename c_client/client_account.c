/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_account.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "client_config.h"
#include "credential_store.h"
#include "http_client.h"

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

const char *login_server_id_or_default(const LoginState *state)
{
    return strcasecmp(state->server_id, INTEGRAL_SECONDARY_SERVER_ID) == 0
               ? INTEGRAL_SECONDARY_SERVER_ID
               : INTEGRAL_PRIMARY_SERVER_ID;
}

static void normalize_login_server_selection(LoginState *state)
{
    copy_text(state->server_id, sizeof(state->server_id), login_server_id_or_default(state));
}

const char *login_server_label(const LoginState *state)
{
    return strcasecmp(login_server_id_or_default(state), INTEGRAL_SECONDARY_SERVER_ID) == 0
               ? "SECONDARY"
               : "PRIMARY";
}

void cycle_login_server(LoginState *state)
{
    normalize_login_server_selection(state);
    const char *next_id = strcasecmp(
                              login_server_id_or_default(state),
                              INTEGRAL_PRIMARY_SERVER_ID
                          ) == 0
                              ? INTEGRAL_SECONDARY_SERVER_ID
                              : INTEGRAL_PRIMARY_SERVER_ID;
    copy_text(state->server_id, sizeof(state->server_id), next_id);
    snprintf(state->status, sizeof(state->status), "%s SELECTED", login_server_label(state));
}

void login_state_init(LoginState *state)
{
    memset(state, 0, sizeof(*state));
    copy_text(state->server_id, sizeof(state->server_id), INTEGRAL_PRIMARY_SERVER_ID);
    copy_text(state->status, sizeof(state->status), "ENTER SERVER URL");
    state->selected = FIELD_SERVER;
}

void password_change_state_init(PasswordChangeState *state)
{
    memset(state, 0, sizeof(*state));
    copy_text(state->status, sizeof(state->status), "SET NEW PASSWORD");
    state->selected = PASSWORD_CHANGE_NEW;
}

static void sanitize_config_name(const char *src, char *out, size_t out_size)
{
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && used + 1 < out_size; p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            out[used++] = (char)tolower(*p);
        }
    }
    if (used == 0) {
        copy_text(out, out_size, "user");
        return;
    }
    out[used] = '\0';
}

void build_user_config_path(const char *base_path, const char *username, const char *server_id, char *out, size_t out_size)
{
    char safe_user[64];
    char safe_server[32];
    sanitize_config_name(username, safe_user, sizeof(safe_user));
    sanitize_config_name(server_id && server_id[0] ? server_id : "primary", safe_server, sizeof(safe_server));
    const char *slash = strrchr(base_path, '/');
    const char *dot = strrchr(base_path, '.');
    if (dot && (!slash || dot > slash)) {
        size_t prefix_len = (size_t)(dot - base_path);
        snprintf(out, out_size, "%.*s_%s_%s%s", (int)prefix_len, base_path, safe_server, safe_user, dot);
    }
    else {
        snprintf(out, out_size, "%s_%s_%s", base_path, safe_server, safe_user);
    }
}

void load_login_config(LoginState *state, const char *base_config_path)
{
    IntegralConfigLogin login_config;
    if (integral_config_load_login(base_config_path, &login_config) == 0) {
        if (login_config.server[0] != '\0') {
            copy_text(state->server, sizeof(state->server), login_config.server);
        }
        if (login_config.server_id[0] != '\0') {
            copy_text(state->server_id, sizeof(state->server_id), login_config.server_id);
        }
        if (login_config.remember) {
            copy_text(state->username, sizeof(state->username), login_config.username);
            bool credential_loaded = integral_credential_store_load(
                                         state->server,
                                         state->username,
                                         state->password,
                                         sizeof(state->password)
                                     ) == 0;
            state->remember_login = credential_loaded;
            copy_text(
                state->status,
                sizeof(state->status),
                credential_loaded ? "SAVED LOGIN LOADED" : "SAVED CREDENTIAL UNAVAILABLE"
            );
        }
    }
    normalize_login_server_selection(state);
    if (state->server[0] != '\0') {
        state->selected = FIELD_USERNAME;
    }
}

int save_login_form_config(const LoginState *state, const char *base_config_path)
{
    IntegralConfigLogin login_config;
    memset(&login_config, 0, sizeof(login_config));
    login_config.remember = state->remember_login;
    copy_text(login_config.server, sizeof(login_config.server), state->server);
    copy_text(login_config.server_id, sizeof(login_config.server_id), login_server_id_or_default(state));
    if (state->remember_login) {
        copy_text(login_config.username, sizeof(login_config.username), state->username);
    }
    return integral_config_save_login(base_config_path, &login_config);
}

bool authenticate_login(LoginState *state, int *must_change_password,
                        int *allow_user_initial_save_import, char *error, size_t error_size)
{
    char authenticated_username[64];
    int result = integral_api_login(state->server,
                      state->username,
                      state->password,
                      login_server_id_or_default(state),
                      state->token,
                      sizeof(state->token),
                      authenticated_username,
                      sizeof(authenticated_username),
                      must_change_password,
                      allow_user_initial_save_import,
                      error,
                      error_size);
    if (result != 0) {
        if (result == 426) copy_text(state->status, sizeof(state->status), error);
        else snprintf(state->status, sizeof(state->status), "LOGIN FAILED %s", error);
        return false;
    }
    copy_text(state->username, sizeof(state->username), authenticated_username);
    copy_text(state->status, sizeof(state->status), "LOGIN OK");
    return true;
}

void save_authenticated_login(LoginState *state, const char *base_config_path, bool must_change_password)
{
    IntegralConfigLogin login_config;
    memset(&login_config, 0, sizeof(login_config));
    login_config.remember = must_change_password ? 0 : state->remember_login;
    copy_text(login_config.server, sizeof(login_config.server), state->server);
    copy_text(login_config.server_id, sizeof(login_config.server_id), login_server_id_or_default(state));
    if (state->remember_login && !must_change_password) {
        copy_text(login_config.username, sizeof(login_config.username), state->username);
        if (integral_credential_store_save(
                state->server, state->username, state->password
            ) != 0) {
            login_config.remember = 0;
            state->remember_login = false;
            copy_text(state->status, sizeof(state->status), "LOGIN OK  CREDENTIAL SAVE FAILED");
        }
    }
    if (must_change_password) {
        (void)integral_credential_store_delete(state->server, state->username);
    }
    if (integral_config_save_login(base_config_path, &login_config) != 0) {
        copy_text(state->status, sizeof(state->status), "LOGIN OK  CONFIG SAVE FAILED");
    }
}

static bool password_is_valid_client_side(const char *password)
{
    if (strlen(password) < 8) {
        return false;
    }
    for (const unsigned char *p = (const unsigned char *)password; *p; p++) {
        if (*p > 126 || !isalnum(*p)) {
            return false;
        }
    }
    return true;
}

bool change_account_password(LoginState *login, PasswordChangeState *state, const char *base_config_path)
{
    if (!password_is_valid_client_side(state->new_password)) {
        copy_text(state->status, sizeof(state->status), "PASSWORD MUST BE 8+ ALNUM");
        return false;
    }
    if (strcmp(state->new_password, state->confirm_password) != 0) {
        copy_text(state->status, sizeof(state->status), "CONFIRM DOES NOT MATCH");
        return false;
    }
    char error[160];
    if (integral_api_change_password(login->server,
                                login->token,
                                state->new_password,
                                error,
                                sizeof(error)) != 0) {
        snprintf(state->status, sizeof(state->status), "CHANGE FAILED %s", error);
        return false;
    }

    if (login->remember_login) {
        copy_text(login->password, sizeof(login->password), state->new_password);
        IntegralConfigLogin login_config;
        memset(&login_config, 0, sizeof(login_config));
        login_config.remember = login->remember_login;
        copy_text(login_config.server, sizeof(login_config.server), login->server);
        copy_text(login_config.server_id, sizeof(login_config.server_id), login_server_id_or_default(login));
        copy_text(login_config.username, sizeof(login_config.username), login->username);
        if (integral_credential_store_save(
                login->server, login->username, login->password
            ) != 0 ||
            integral_config_save_login(base_config_path, &login_config) != 0) {
            copy_text(login->status, sizeof(login->status), "PASSWORD CHANGED  CONFIG SAVE FAILED");
        }
        else {
            copy_text(login->status, sizeof(login->status), "PASSWORD CHANGED");
        }
    }
    else {
        copy_text(login->status, sizeof(login->status), "PASSWORD CHANGED");
    }
    return true;
}

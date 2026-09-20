/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ACCOUNT_H
#define INTEGRAL_CLIENT_ACCOUNT_H

#include <stdbool.h>
#include <stddef.h>

#define INTEGRAL_PRIMARY_SERVER_ID "primary"
#define INTEGRAL_SECONDARY_SERVER_ID "secondary"

typedef enum LoginField {
    FIELD_SERVER,
    FIELD_ENV,
    FIELD_USERNAME,
    FIELD_PASSWORD,
    FIELD_REMEMBER,
    FIELD_ACTION,
} LoginField;

typedef enum PasswordChangeField {
    PASSWORD_CHANGE_NEW,
    PASSWORD_CHANGE_CONFIRM,
    PASSWORD_CHANGE_SAVE,
} PasswordChangeField;

typedef struct LoginState {
    char server[128];
    char server_id[32];
    char username[64];
    char password[64];
    char status[160];
    char token[160];
    LoginField selected;
    bool editing;
    bool quit;
    bool password_visible;
    bool remember_login;
} LoginState;

typedef struct PasswordChangeState {
    char new_password[64];
    char confirm_password[64];
    char status[160];
    PasswordChangeField selected;
    bool editing;
    bool password_visible;
} PasswordChangeState;

const char *login_server_id_or_default(const LoginState *state);
const char *login_server_label(const LoginState *state);
void cycle_login_server(LoginState *state);
void login_state_init(LoginState *state);
void password_change_state_init(PasswordChangeState *state);
void build_user_config_path(const char *base_path, const char *username, const char *server_id, char *out, size_t out_size);
void load_login_config(LoginState *state, const char *base_config_path);
int save_login_form_config(const LoginState *state, const char *base_config_path);
bool authenticate_login(LoginState *state, int *must_change_password,
                        int *allow_user_initial_save_import, char *error, size_t error_size);
void save_authenticated_login(LoginState *state, const char *base_config_path, bool must_change_password);
bool change_account_password(LoginState *login, PasswordChangeState *state, const char *base_config_path);

#endif

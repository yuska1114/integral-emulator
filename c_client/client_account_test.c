/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_account.h"
#include "client_config.h"
#include "credential_store.h"
#include "http_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* API and credential boundaries are fakes; config files use the real parser/writer. */
static int login_failure, change_required, credential_failure, change_failure;
static int save_calls, delete_calls, change_calls;

int integral_api_login(const char *server, const char *username, const char *password,
                       const char *server_id, char *token, size_t token_size,
                       char *canonical, size_t canonical_size, int *must_change,
                       int *allow_import, char *error, size_t error_size)
{
    assert(strcmp(server, "https://account.example/api") == 0);
    assert(username[0] && password[0]);
    assert(strcmp(server_id, "secondary") == 0);
    if (login_failure) {
        snprintf(error, error_size, "%s", login_failure == 426 ?
                 "ASK SERVER ADMIN FOR SUPPORTED VERSION" : "DENIED");
        return login_failure == 426 ? 426 : -1;
    }
    snprintf(token, token_size, "test-token");
    snprintf(canonical, canonical_size, "Canonical_User");
    *must_change = change_required;
    *allow_import = 1;
    return 0;
}

int integral_api_change_password(const char *server, const char *token,
                                 const char *password, char *error, size_t size)
{
    assert(strcmp(server, "https://account.example/api") == 0);
    assert(strcmp(token, "test-token") == 0);
    assert(strcmp(password, "NewPass123") == 0);
    change_calls++;
    if (change_failure) {
        snprintf(error, size, "DENIED");
        return -1;
    }
    return 0;
}

int integral_credential_store_save(const char *server, const char *user, const char *password)
{
    assert(strcmp(server, "https://account.example/api") == 0);
    assert(strcmp(user, "Canonical_User") == 0);
    assert(password[0]);
    save_calls++;
    return credential_failure ? -1 : 0;
}

int integral_credential_store_load(const char *server, const char *user, char *password, size_t size)
{
    assert(strcmp(server, "https://account.example/api") == 0);
    assert(strcmp(user, "Canonical_User") == 0);
    if (credential_failure) return -1;
    snprintf(password, size, "SavedPass123");
    return 0;
}

int integral_credential_store_delete(const char *server, const char *user)
{
    assert(strcmp(server, "https://account.example/api") == 0);
    assert(strcmp(user, "Canonical_User") == 0);
    delete_calls++;
    return 0;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    char config_path[1024], user_path[1024], other_path[1024], error[160];
    snprintf(config_path, sizeof(config_path), "%s/login.conf", argv[1]);
    LoginState login;
    login_state_init(&login);
    assert(!login.password_visible && !login.remember_login);
    assert(strcmp(login_server_id_or_default(&login), "primary") == 0);
    cycle_login_server(&login);
    assert(strcmp(login_server_label(&login), "SECONDARY") == 0);
    snprintf(login.server, sizeof(login.server), "https://account.example/api");
    snprintf(login.username, sizeof(login.username), "typed_user");
    snprintf(login.password, sizeof(login.password), "OldPass123");
    int required = 0, allow_import = 0;
    login_failure = 1;
    assert(!authenticate_login(&login, &required, &allow_import, error, sizeof(error)));
    assert(strcmp(login.username, "typed_user") == 0);
    assert(strcmp(login.status, "LOGIN FAILED DENIED") == 0);
    assert(save_calls == 0 && delete_calls == 0);
    login_failure = 426;
    assert(!authenticate_login(&login, &required, &allow_import, error, sizeof(error)));
    assert(strcmp(login.status, "ASK SERVER ADMIN FOR SUPPORTED VERSION") == 0);
    assert(save_calls == 0 && delete_calls == 0);
    login_failure = 0;
    assert(authenticate_login(&login, &required, &allow_import, error, sizeof(error)));
    assert(!required && allow_import == 1);
    assert(strcmp(login.username, "Canonical_User") == 0);
    save_authenticated_login(&login, config_path, false);
    assert(save_calls == 0);
    IntegralConfigLogin saved;
    assert(integral_config_load_login(config_path, &saved) == 0);
    assert(!saved.remember && !saved.username[0]);

    login.remember_login = true;
    save_authenticated_login(&login, config_path, false);
    assert(save_calls == 1);
    LoginState restored;
    login_state_init(&restored);
    load_login_config(&restored, config_path);
    assert(restored.remember_login && restored.selected == FIELD_USERNAME);
    assert(strcmp(restored.password, "SavedPass123") == 0);
    assert(strcmp(restored.server_id, "secondary") == 0);
    credential_failure = 1;
    login_state_init(&restored);
    load_login_config(&restored, config_path);
    assert(!restored.remember_login && !restored.password[0]);
    save_authenticated_login(&login, config_path, false);
    assert(!login.remember_login);
    assert(integral_config_load_login(config_path, &saved) == 0 && !saved.remember);
    credential_failure = 0;

    login.remember_login = true;
    change_required = 1;
    assert(authenticate_login(&login, &required, &allow_import, error, sizeof(error)));
    assert(required);
    int before_save = save_calls;
    save_authenticated_login(&login, config_path, true);
    assert(delete_calls == 1 && save_calls == before_save);
    assert(integral_config_load_login(config_path, &saved) == 0 && !saved.remember);

    PasswordChangeState change;
    password_change_state_init(&change);
    snprintf(change.new_password, sizeof(change.new_password), "short");
    assert(!change_account_password(&login, &change, config_path));
    snprintf(change.new_password, sizeof(change.new_password), "NewPass123");
    assert(!change_account_password(&login, &change, config_path));
    assert(change_calls == 0);
    snprintf(change.confirm_password, sizeof(change.confirm_password), "NewPass123");
    change_failure = 1;
    assert(!change_account_password(&login, &change, config_path));
    assert(strcmp(change.status, "CHANGE FAILED DENIED") == 0);
    assert(save_calls == before_save);
    change_failure = 0;
    assert(change_account_password(&login, &change, config_path));
    assert(change_calls == 2 && save_calls == before_save + 1);
    assert(strcmp(login.password, "NewPass123") == 0);
    assert(strcmp(login.status, "PASSWORD CHANGED") == 0);
    login.remember_login = false;
    assert(save_login_form_config(&login, config_path) == 0);
    assert(integral_config_load_login(config_path, &saved) == 0);
    assert(!saved.remember && !saved.username[0]);

    build_user_config_path("dir/client.conf", "Canonical_User", "secondary", user_path, sizeof(user_path));
    assert(strcmp(user_path, "dir/client_secondary_canonical_user.conf") == 0);
    build_user_config_path("dir/client.conf", "Other", "secondary", other_path, sizeof(other_path));
    assert(strcmp(user_path, other_path) != 0);
    build_user_config_path("dir/client.conf", "Canonical_User", "primary", other_path, sizeof(other_path));
    assert(strcmp(user_path, other_path) != 0);
    puts("account authentication, persistence and config-path checks passed");
    return 0;
}

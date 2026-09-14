/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "credential_store.h"

#include <stdio.h>

#ifdef _WIN32
int main(void)
{
    puts("Linux credential store test is not applicable on Windows");
    return 0;
}
#else
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int mode_is(const char *path, mode_t expected)
{
    struct stat status;
    return lstat(path, &status) == 0 &&
           (status.st_mode & 0777) == expected;
}

static int inspect_files(const char *directory,
                         unsigned expected_count,
                         unsigned expected_passwords)
{
    static const char *passwords[] = {
        "alpha-secret-2", "beta-secret", "gamma-secret",
    };
    DIR *handle = opendir(directory);
    if (!handle) {
        return -1;
    }
    unsigned count = 0;
    unsigned found = 0;
    struct dirent *entry;
    while ((entry = readdir(handle)) != NULL) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char path[4096];
        if (snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name) >=
            (int)sizeof(path) || !mode_is(path, 0600)) {
            closedir(handle);
            return -1;
        }
        FILE *file = fopen(path, "rb");
        char value[128];
        size_t size = file ? fread(value, 1, sizeof(value) - 1u, file) : 0u;
        int read_failed = file ? ferror(file) : 1;
        int close_failed = file ? fclose(file) : 1;
        if (read_failed || close_failed || size == 0u) {
            closedir(handle);
            return -1;
        }
        value[size] = '\0';
        for (unsigned i = 0; i < sizeof(passwords) / sizeof(passwords[0]); i++) {
            if (strcmp(value, passwords[i]) == 0) {
                found |= 1u << i;
            }
        }
        memset(value, 0, sizeof(value));
        count++;
    }
    closedir(handle);
    return count == expected_count && found == expected_passwords ? 0 : -1;
}

static int expect_password(const char *server,
                           const char *username,
                           const char *expected)
{
    char password[128];
    int result = integral_credential_store_load(
        server, username, password, sizeof(password)
    );
    int matches = result == 0 && strcmp(password, expected) == 0;
    memset(password, 0, sizeof(password));
    return matches ? 0 : -1;
}

int main(void)
{
    char root[] = "/tmp/integral-credential-test-XXXXXX";
    if (!mkdtemp(root) || setenv("XDG_CONFIG_HOME", root, 1) != 0) {
        return 1;
    }
    const char *server_a = "https://example.invalid/integral-api";
    const char *server_b = "https://secondary.invalid/integral-api";
    const char *user_a = "test-user-a";
    const char *user_b = "test-user-b";
    char application[4096];
    char credentials[4096];
    if (snprintf(application, sizeof(application), "%s/integral-emulator", root) >=
            (int)sizeof(application) ||
        snprintf(credentials, sizeof(credentials),
                 "%s/integral-emulator/credentials", root) >=
            (int)sizeof(credentials)) {
        return 1;
    }
    char absent[128];
    if (integral_credential_store_load(
            server_a, user_a, absent, sizeof(absent)
        ) != 1 || access(credentials, F_OK) == 0) {
        return 1;
    }
    if (integral_credential_store_save(server_a, user_a, "alpha-secret-1") != 0 ||
        expect_password(server_a, user_a, "alpha-secret-1") != 0 ||
        integral_credential_store_save(server_a, user_a, "alpha-secret-2") != 0 ||
        integral_credential_store_save(server_b, user_a, "beta-secret") != 0 ||
        integral_credential_store_save(server_a, user_b, "gamma-secret") != 0 ||
        expect_password(server_a, user_a, "alpha-secret-2") != 0 ||
        expect_password(server_b, user_a, "beta-secret") != 0 ||
        expect_password(server_a, user_b, "gamma-secret") != 0) {
        return 1;
    }

    if (!mode_is(credentials, 0700) || inspect_files(credentials, 3u, 7u) != 0) {
        return 1;
    }
    if (integral_credential_store_delete(server_a, user_a) != 0) {
        return 1;
    }
    char removed[128];
    if (integral_credential_store_load(
            server_a, user_a, removed, sizeof(removed)
        ) != 1 || inspect_files(credentials, 2u, 6u) != 0) {
        return 1;
    }
    if (integral_credential_store_delete(server_b, user_a) != 0 ||
        integral_credential_store_delete(server_a, user_b) != 0 ||
        inspect_files(credentials, 0u, 0u) != 0 ||
        rmdir(credentials) != 0 || rmdir(application) != 0 || rmdir(root) != 0) {
        return 1;
    }

    char home[] = "/tmp/integral-credential-home-test-XXXXXX";
    if (!mkdtemp(home) || unsetenv("XDG_CONFIG_HOME") != 0 ||
        setenv("HOME", home, 1) != 0 ||
        integral_credential_store_save(server_a, user_a, "alpha-secret-2") != 0 ||
        expect_password(server_a, user_a, "alpha-secret-2") != 0) {
        return 1;
    }
    char config[4096];
    if (snprintf(application, sizeof(application),
                 "%s/.config/integral-emulator", home) >=
            (int)sizeof(application) ||
        snprintf(credentials, sizeof(credentials),
                 "%s/.config/integral-emulator/credentials", home) >=
            (int)sizeof(credentials) ||
        snprintf(config, sizeof(config), "%s/.config", home) >=
            (int)sizeof(config) ||
        !mode_is(credentials, 0700) || inspect_files(credentials, 1u, 1u) != 0 ||
        integral_credential_store_delete(server_a, user_a) != 0 ||
        rmdir(credentials) != 0 || rmdir(application) != 0 ||
        rmdir(config) != 0 || rmdir(home) != 0) {
        return 1;
    }
    puts("Linux credential store save/load/delete permissions: OK");
    return 0;
}
#endif

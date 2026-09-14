/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "credential_store.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if (defined(__APPLE__) && !defined(INTEGRAL_CREDENTIAL_STORE_LINUX_TEST)) || defined(_WIN32)
static int build_account(const char *server,
                         const char *username,
                         char *output,
                         size_t output_size)
{
    if (!server || !server[0] || !username || !username[0] || !output || output_size == 0u) {
        return -1;
    }
    int written = snprintf(output, output_size, "%s|%s", server, username);
    return written > 0 && (size_t)written < output_size ? 0 : -1;
}
#endif

#if defined(__APPLE__) && !defined(INTEGRAL_CREDENTIAL_STORE_LINUX_TEST)
#include <Security/Security.h>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

#define INTEGRAL_CREDENTIAL_SERVICE "INTEGRAL EMULATOR"

int integral_credential_store_load(const char *server,
                                   const char *username,
                                   char *password_out,
                                   size_t password_out_size)
{
    char account[256];
    UInt32 length = 0;
    void *data = NULL;
    if (build_account(server, username, account, sizeof(account)) != 0 ||
        !password_out || password_out_size == 0u) {
        return -1;
    }
    OSStatus status = SecKeychainFindGenericPassword(
        NULL,
        (UInt32)strlen(INTEGRAL_CREDENTIAL_SERVICE), INTEGRAL_CREDENTIAL_SERVICE,
        (UInt32)strlen(account), account,
        &length, &data, NULL
    );
    if (status != errSecSuccess) {
        return status == errSecItemNotFound ? 1 : -1;
    }
    if ((size_t)length >= password_out_size) {
        SecKeychainItemFreeContent(NULL, data);
        return -1;
    }
    memcpy(password_out, data, length);
    password_out[length] = '\0';
    SecKeychainItemFreeContent(NULL, data);
    return 0;
}

int integral_credential_store_save(const char *server,
                                   const char *username,
                                   const char *password)
{
    char account[256];
    SecKeychainItemRef item = NULL;
    if (build_account(server, username, account, sizeof(account)) != 0 || !password) {
        return -1;
    }
    OSStatus status = SecKeychainFindGenericPassword(
        NULL,
        (UInt32)strlen(INTEGRAL_CREDENTIAL_SERVICE), INTEGRAL_CREDENTIAL_SERVICE,
        (UInt32)strlen(account), account,
        NULL, NULL, &item
    );
    if (status == errSecSuccess) {
        status = SecKeychainItemModifyAttributesAndData(
            item, NULL, (UInt32)strlen(password), password
        );
        CFRelease(item);
        return status == errSecSuccess ? 0 : -1;
    }
    if (status != errSecItemNotFound) {
        return -1;
    }
    status = SecKeychainAddGenericPassword(
        NULL,
        (UInt32)strlen(INTEGRAL_CREDENTIAL_SERVICE), INTEGRAL_CREDENTIAL_SERVICE,
        (UInt32)strlen(account), account,
        (UInt32)strlen(password), password,
        NULL
    );
    return status == errSecSuccess ? 0 : -1;
}

int integral_credential_store_delete(const char *server, const char *username)
{
    char account[256];
    SecKeychainItemRef item = NULL;
    if (build_account(server, username, account, sizeof(account)) != 0) {
        return -1;
    }
    OSStatus status = SecKeychainFindGenericPassword(
        NULL,
        (UInt32)strlen(INTEGRAL_CREDENTIAL_SERVICE), INTEGRAL_CREDENTIAL_SERVICE,
        (UInt32)strlen(account), account,
        NULL, NULL, &item
    );
    if (status == errSecItemNotFound) {
        return 0;
    }
    if (status != errSecSuccess) {
        return -1;
    }
    status = SecKeychainItemDelete(item);
    CFRelease(item);
    return status == errSecSuccess ? 0 : -1;
}
#pragma clang diagnostic pop

#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincred.h>

int integral_credential_store_load(const char *server,
                                   const char *username,
                                   char *password_out,
                                   size_t password_out_size)
{
    char target[256];
    PCREDENTIALA credential = NULL;
    if (build_account(server, username, target, sizeof(target)) != 0 ||
        !password_out || password_out_size == 0u) {
        return -1;
    }
    if (!CredReadA(target, CRED_TYPE_GENERIC, 0, &credential)) {
        return GetLastError() == ERROR_NOT_FOUND ? 1 : -1;
    }
    size_t length = credential->CredentialBlobSize;
    if (length >= password_out_size) {
        CredFree(credential);
        return -1;
    }
    memcpy(password_out, credential->CredentialBlob, length);
    password_out[length] = '\0';
    CredFree(credential);
    return 0;
}

int integral_credential_store_save(const char *server,
                                   const char *username,
                                   const char *password)
{
    char target[256];
    CREDENTIALA credential;
    if (build_account(server, username, target, sizeof(target)) != 0 || !password) {
        return -1;
    }
    memset(&credential, 0, sizeof(credential));
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = target;
    credential.CredentialBlobSize = (DWORD)strlen(password);
    credential.CredentialBlob = (LPBYTE)password;
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = (LPSTR)username;
    return CredWriteA(&credential, 0) ? 0 : -1;
}

int integral_credential_store_delete(const char *server, const char *username)
{
    char target[256];
    if (build_account(server, username, target, sizeof(target)) != 0) {
        return -1;
    }
    if (CredDeleteA(target, CRED_TYPE_GENERIC, 0)) {
        return 0;
    }
    return GetLastError() == ERROR_NOT_FOUND ? 0 : -1;
}

#elif defined(__linux__) || defined(INTEGRAL_CREDENTIAL_STORE_LINUX_TEST)
#include <fcntl.h>
#include <openssl/evp.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#ifndef O_CLOEXEC
#define O_CLOEXEC 0
#endif
#ifndef O_NOFOLLOW
#define O_NOFOLLOW 0
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define INTEGRAL_CREDENTIAL_DIRECTORY "integral-emulator/credentials"

static int linux_config_root(char *output, size_t output_size)
{
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    int written;
    if (xdg && xdg[0] == '/') {
        written = snprintf(output, output_size, "%s", xdg);
    }
    else if (home && home[0] == '/') {
        written = snprintf(output, output_size, "%s/.config", home);
    }
    else {
        return -1;
    }
    return written >= 0 && (size_t)written < output_size ? 0 : -1;
}

static int existing_directory(const char *path)
{
    struct stat status;
    return lstat(path, &status) == 0 && S_ISDIR(status.st_mode) ? 0 : -1;
}

static int ensure_directory(const char *path, mode_t create_mode)
{
    if (mkdir(path, create_mode) != 0 && errno != EEXIST) {
        return -1;
    }
    return existing_directory(path);
}

static int linux_credential_directory(char *output,
                                      size_t output_size,
                                      int create)
{
    char config_root[PATH_MAX];
    char application_root[PATH_MAX];
    if (linux_config_root(config_root, sizeof(config_root)) != 0) {
        return -1;
    }
    if (snprintf(application_root, sizeof(application_root),
                 "%s/integral-emulator", config_root) >=
        (int)sizeof(application_root) ||
        snprintf(output, output_size, "%s/%s", config_root,
                 INTEGRAL_CREDENTIAL_DIRECTORY) >= (int)output_size) {
        return -1;
    }
    if (!create) {
        if (existing_directory(output) != 0) {
            return errno == ENOENT ? 1 : -1;
        }
    }
    else if (ensure_directory(config_root, 0700) != 0 ||
             ensure_directory(application_root, 0700) != 0 ||
             ensure_directory(output, 0700) != 0) {
        return -1;
    }
    struct stat status;
    if (lstat(output, &status) != 0 || !S_ISDIR(status.st_mode) ||
        status.st_uid != geteuid()) {
        return -1;
    }
    if ((status.st_mode & 0777) != 0700 && chmod(output, 0700) != 0) {
        return -1;
    }
    return 0;
}

static int linux_credential_name(const char *server,
                                 const char *username,
                                 char output[65])
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_size = 0;
    unsigned char identity[384];
    size_t server_size;
    size_t username_size;
    if (!server || !server[0] || !username || !username[0]) {
        return -1;
    }
    server_size = strlen(server);
    username_size = strlen(username);
    if (server_size + username_size + 1u > sizeof(identity)) {
        return -1;
    }
    memcpy(identity, server, server_size);
    identity[server_size] = '\0';
    memcpy(identity + server_size + 1u, username, username_size);
    if (EVP_Digest(identity, server_size + username_size + 1u,
                   digest, &digest_size, EVP_sha256(), NULL) != 1 ||
        digest_size != 32u) {
        memset(identity, 0, sizeof(identity));
        return -1;
    }
    static const char digits[] = "0123456789abcdef";
    for (unsigned int i = 0; i < digest_size; i++) {
        output[i * 2u] = digits[digest[i] >> 4];
        output[i * 2u + 1u] = digits[digest[i] & 15u];
    }
    output[64] = '\0';
    memset(identity, 0, sizeof(identity));
    memset(digest, 0, sizeof(digest));
    return 0;
}

static int linux_credential_path(const char *server,
                                 const char *username,
                                 char *output,
                                 size_t output_size,
                                 int create_directory)
{
    char directory[PATH_MAX];
    char name[65];
    int directory_result = linux_credential_directory(
        directory, sizeof(directory), create_directory
    );
    if (directory_result != 0) {
        return directory_result;
    }
    if (linux_credential_name(server, username, name) != 0 ||
        snprintf(output, output_size, "%s/%s.password", directory, name) >=
            (int)output_size) {
        return -1;
    }
    return 0;
}

static int write_all(int descriptor, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t written = write(descriptor, data, size);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return -1;
        }
        data += (size_t)written;
        size -= (size_t)written;
    }
    return 0;
}

int integral_credential_store_load(const char *server,
                                   const char *username,
                                   char *password_out,
                                   size_t password_out_size)
{
    char path[PATH_MAX];
    struct stat status;
    if (!password_out || password_out_size < 2u) {
        return -1;
    }
    password_out[0] = '\0';
    int path_result = linux_credential_path(
        server, username, path, sizeof(path), 0
    );
    if (path_result != 0) {
        return path_result;
    }
    int descriptor = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return errno == ENOENT ? 1 : -1;
    }
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode) ||
        status.st_uid != geteuid() || (status.st_mode & 0777) != 0600) {
        close(descriptor);
        return -1;
    }
    size_t used = 0;
    while (used < password_out_size) {
        ssize_t count = read(descriptor, password_out + used,
                             password_out_size - used);
        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count < 0) {
            close(descriptor);
            password_out[0] = '\0';
            return -1;
        }
        if (count == 0) {
            break;
        }
        used += (size_t)count;
    }
    if (close(descriptor) != 0 || used == 0u || used >= password_out_size) {
        memset(password_out, 0, password_out_size);
        return -1;
    }
    password_out[used] = '\0';
    return 0;
}

int integral_credential_store_save(const char *server,
                                   const char *username,
                                   const char *password)
{
    char path[PATH_MAX];
    char temporary_path[PATH_MAX];
    if (!password || !password[0] ||
        linux_credential_path(server, username, path, sizeof(path), 1) != 0 ||
        snprintf(temporary_path, sizeof(temporary_path), "%s.part.%ld", path,
                 (long)getpid()) >= (int)sizeof(temporary_path)) {
        return -1;
    }
    int descriptor = open(temporary_path,
                          O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW,
                          0600);
    if (descriptor < 0) {
        return -1;
    }
    if (fchmod(descriptor, 0600) != 0 ||
        write_all(descriptor, password, strlen(password)) != 0 ||
        fsync(descriptor) != 0) {
        (void)close(descriptor);
        (void)unlink(temporary_path);
        return -1;
    }
    if (close(descriptor) != 0 || rename(temporary_path, path) != 0) {
        (void)unlink(temporary_path);
        return -1;
    }
    return chmod(path, 0600);
}

int integral_credential_store_delete(const char *server, const char *username)
{
    char path[PATH_MAX];
    int path_result = linux_credential_path(
        server, username, path, sizeof(path), 0
    );
    if (path_result == 1) {
        return 0;
    }
    if (path_result != 0) {
        return -1;
    }
    return unlink(path) == 0 || errno == ENOENT ? 0 : -1;
}

#else
int integral_credential_store_load(const char *server,
                                   const char *username,
                                   char *password_out,
                                   size_t password_out_size)
{
    (void)server; (void)username; (void)password_out; (void)password_out_size;
    return -1;
}
int integral_credential_store_save(const char *server,
                                   const char *username,
                                   const char *password)
{
    (void)server; (void)username; (void)password;
    return -1;
}
int integral_credential_store_delete(const char *server, const char *username)
{
    (void)server; (void)username;
    return -1;
}
#endif

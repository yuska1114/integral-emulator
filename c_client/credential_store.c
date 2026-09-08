/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "credential_store.h"

#include <stdio.h>
#include <string.h>

#if defined(__APPLE__) || defined(_WIN32)
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

#ifdef __APPLE__
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

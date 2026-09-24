/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_file_io.h"
#include "../runtimes/gb/src/common/utf8_file.h"
#include "client_config.h"
#include <stdlib.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

int create_export_directory(const char *root, const char *stamp, char *out, size_t capacity)
{
    for (unsigned index = 0; index < 10000; index++) {
        int length = index ? snprintf(out, capacity, "%s/%s_%u", root, stamp, index)
                           : snprintf(out, capacity, "%s/%s", root, stamp);
        if (length < 0 || (size_t)length >= capacity) return -1;
#ifdef _WIN32
        int result = _mkdir(out);
#else
        int result = mkdir(out, 0700);
#endif
        if (result == 0) return 0;
        if (errno != EEXIST) return -1;
    }
    return -1;
}

void export_save_filename(const char *name, unsigned slot_index, char *out, size_t capacity)
{
    char base[96];
    snprintf(base, sizeof(base), "%s", name ? name : "");
    char *dot = strrchr(base, '.');
    if (dot) *dot = '\0';
    char safe[96];
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)base; *p && used + 1 < sizeof(safe); p++) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            (*p >= '0' && *p <= '9') || *p == '_' || *p == '-') safe[used++] = (char)*p;
        else if (*p == ' ' || *p == '.') safe[used++] = '_';
    }
    safe[used] = '\0';
    snprintf(out, capacity, "ROM%u_%s.sav", slot_index + 1, used ? safe : "save");
}
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#elif defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(INTEGRAL_USE_OPENSSL)
#include <openssl/evp.h>
#endif



#if !defined(_WIN32) && !defined(__APPLE__) && defined(INTEGRAL_USE_OPENSSL)
static int digest_file_hex_openssl(const char *path,
                                   const EVP_MD *md,
                                   char *out,
                                   size_t out_size)
{
    if (!md) {
        return -1;
    }
    int digest_size = EVP_MD_get_size(md);
    if (digest_size <= 0 || out_size < (size_t)digest_size * 2u + 1u) {
        return -1;
    }
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int actual_size = 0;
    int rc = -1;
    if (!ctx || EVP_DigestInit_ex(ctx, md, NULL) != 1) {
        goto done;
    }
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (EVP_DigestUpdate(ctx, buffer, n) != 1) {
            goto done;
        }
    }
    if (ferror(file) != 0 || EVP_DigestFinal_ex(ctx, digest, &actual_size) != 1 ||
        actual_size != (unsigned int)digest_size) {
        goto done;
    }
    for (unsigned int i = 0; i < actual_size; i++) {
        snprintf(out + i * 2u, out_size - i * 2u, "%02x", digest[i]);
    }
    rc = 0;
done:
    EVP_MD_CTX_free(ctx);
    fclose(file);
    return rc;
}
#endif

#if !defined(_WIN32) && !defined(__APPLE__) && !defined(INTEGRAL_USE_OPENSSL)
static bool shell_quote_path(const char *path, char *out, size_t out_size)
{
    size_t used = 0;
    if (out_size < 3) {
        return false;
    }
    out[used++] = '\'';
    for (const char *p = path; *p; p++) {
        const char *chunk = *p == '\'' ? "'\\''" : NULL;
        if (chunk) {
            size_t len = strlen(chunk);
            if (used + len + 2 > out_size) {
                return false;
            }
            memcpy(out + used, chunk, len);
            used += len;
        }
        else {
            if (used + 2 > out_size) {
                return false;
            }
            out[used++] = *p;
        }
    }
    out[used++] = '\'';
    out[used] = '\0';
    return true;
}

static int digest_file_hex_command(const char *path,
                                   const char *tool,
                                   size_t digest_chars,
                                   char *out,
                                   size_t out_size)
{
    if (out_size < digest_chars + 1u) {
        return -1;
    }
    char quoted[640];
    if (!shell_quote_path(path, quoted, sizeof(quoted))) {
        return -1;
    }
    char command[768];
    int n = snprintf(command, sizeof(command), "%s -- %s", tool, quoted);
    if (n < 0 || (size_t)n >= sizeof(command)) {
        return -1;
    }
    FILE *pipe = popen(command, "r");
    if (!pipe) {
        return -1;
    }
    char line[256];
    bool ok = fgets(line, sizeof(line), pipe) != NULL;
    int status = pclose(pipe);
    if (!ok || status != 0 || strlen(line) < digest_chars) {
        return -1;
    }
    for (size_t i = 0; i < digest_chars; i++) {
        if (!isxdigit((unsigned char)line[i])) {
            return -1;
        }
        out[i] = (char)tolower((unsigned char)line[i]);
    }
    out[digest_chars] = '\0';
    return 0;
}

#endif

bool runtime_session_id_is_path_safe(const char *session_id)
{
    if (!session_id || !session_id[0]) return false;
    size_t length = strlen(session_id);
    if (length > 95u) return false;
    for (const unsigned char *cursor = (const unsigned char *)session_id; *cursor; cursor++) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') return false;
    }
    return true;
}

bool format_runtime_session_path(char *out,
                                        size_t out_size,
                                        const char *root,
                                        const char *session_id,
                                        const char *relative_path)
{
    if (!out || out_size == 0 || !root ||
        !runtime_session_id_is_path_safe(session_id)) {
        return false;
    }
    int length;
    if (relative_path && relative_path[0] != '\0') {
        length = snprintf(out, out_size, "%s/%s/%s", root, session_id, relative_path);
    }
    else {
        length = snprintf(out, out_size, "%s/%s", root, session_id);
    }
    return length > 0 && (size_t)length < out_size;
}

int ensure_directory(const char *path)
{
#ifdef _WIN32
    if (_mkdir(path) == 0 || errno == EEXIST) {
#else
    if (mkdir(path, 0755) == 0 || errno == EEXIST) {
#endif
        return 0;
    }
    return -1;
}

int ensure_private_runtime_directory(const char *path)
{
#ifdef _WIN32
    return ensure_directory(path);
#else
    if (mkdir(path, 0700) != 0 && errno != EEXIST) return -1;
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISDIR(status.st_mode)) return -1;
    return chmod(path, 0700);
#endif
}

int sha256_file_hex(const char *path, char *out, size_t out_size)
{
#ifdef _WIN32
    const size_t digest_size = 32;
    if (out_size < digest_size * 2 + 1) {
        return -1;
    }
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[32];
    int rc = -1;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, NULL, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0))) {
        goto done;
    }
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer, (ULONG)n, 0))) {
            goto done;
        }
    }
    if (ferror(file) != 0 ||
        !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, (ULONG)sizeof(digest), 0))) {
        goto done;
    }
    for (size_t i = 0; i < digest_size; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    rc = 0;
done:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    fclose(file);
    return rc;
#elif !defined(__APPLE__) && defined(INTEGRAL_USE_OPENSSL)
    return digest_file_hex_openssl(path, EVP_sha256(), out, out_size);
#elif defined(__APPLE__)
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    CC_SHA256_CTX ctx;
    CC_SHA256_Init(&ctx);
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        CC_SHA256_Update(&ctx, buffer, (CC_LONG)n);
    }
    bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok || out_size < CC_SHA256_DIGEST_LENGTH * 2 + 1) {
        return -1;
    }
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    for (size_t i = 0; i < CC_SHA256_DIGEST_LENGTH; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    return 0;
#else
    return digest_file_hex_command(path, "sha256sum", 64, out, out_size);
#endif
}

int sha1_file_hex(const char *path, char *out, size_t out_size)
{
#ifdef _WIN32
    const size_t digest_size = 20;
    if (out_size < digest_size * 2 + 1) {
        return -1;
    }
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    BCRYPT_ALG_HANDLE alg = NULL;
    BCRYPT_HASH_HANDLE hash = NULL;
    unsigned char digest[20];
    int rc = -1;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, NULL, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, NULL, 0, NULL, 0, 0))) {
        goto done;
    }
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer, (ULONG)n, 0))) {
            goto done;
        }
    }
    if (ferror(file) != 0 ||
        !BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, (ULONG)sizeof(digest), 0))) {
        goto done;
    }
    for (size_t i = 0; i < digest_size; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    rc = 0;
done:
    if (hash) {
        BCryptDestroyHash(hash);
    }
    if (alg) {
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    fclose(file);
    return rc;
#elif !defined(__APPLE__) && defined(INTEGRAL_USE_OPENSSL)
    return digest_file_hex_openssl(path, EVP_sha1(), out, out_size);
#elif defined(__APPLE__)
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    CC_SHA1_CTX ctx;
    CC_SHA1_Init(&ctx);
    unsigned char buffer[4096];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) {
        CC_SHA1_Update(&ctx, buffer, (CC_LONG)n);
    }
    bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok || out_size < CC_SHA1_DIGEST_LENGTH * 2 + 1) {
        return -1;
    }
    unsigned char digest[CC_SHA1_DIGEST_LENGTH];
    CC_SHA1_Final(digest, &ctx);
    for (size_t i = 0; i < CC_SHA1_DIGEST_LENGTH; i++) {
        snprintf(out + i * 2, out_size - i * 2, "%02x", digest[i]);
    }
    return 0;
#else
    return digest_file_hex_command(path, "sha1sum", 40, out, out_size);
#endif
}

int read_binary_file(const char *path, unsigned char *out, size_t out_capacity, size_t *out_size)
{
    *out_size = 0;
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    size_t n = fread(out, 1, out_capacity, file);
    bool ok = ferror(file) == 0;
    int extra = fgetc(file);
    fclose(file);
    if (!ok || extra != EOF) {
        return -1;
    }
    *out_size = n;
    return 0;
}

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


bool local_file_exists(const char *path)
{
    return path && path[0] && integral_access(path, R_OK) == 0;
}


int write_binary_file(const char *path, const unsigned char *data, size_t data_size)
{
    FILE *file = integral_fopen(path, "wb");
    if (!file) {
        return -1;
    }
    int rc = 0;
    if (data_size > 0 && fwrite(data, 1, data_size, file) != data_size) {
        rc = -1;
    }
    if (fflush(file) != 0) {
        rc = -1;
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        rc = -1;
    }
    if (fclose(file) != 0) {
        rc = -1;
    }
    return rc;
}


int copy_binary_file_limited(const char *source_path, const char *dest_path, size_t max_size)
{
    unsigned char *data = NULL;
    size_t data_size = 0;
    if (read_binary_file_alloc(source_path, &data, &data_size, max_size) != 0) {
        return -1;
    }
    int rc = write_private_runtime_file(dest_path, data, data_size);
    free(data);
    return rc;
}


int atomic_replace_binary_file(const char *path, const unsigned char *data, size_t data_size)
{
    char temp_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(temp_path, sizeof(temp_path), "%s.tmp.%ld", path, (long)getpid());
    if (write_private_runtime_file(temp_path, data, data_size) != 0) {
        return -1;
    }
#ifdef _WIN32
    bool replaced = false;
    DWORD replace_error = ERROR_SUCCESS;
    for (unsigned attempt = 0; attempt < 20u; attempt++) {
        if (MoveFileExA(temp_path, path,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            replaced = true;
            break;
        }
        replace_error = GetLastError();
        if (replace_error != ERROR_SHARING_VIOLATION &&
            replace_error != ERROR_ACCESS_DENIED) {
            break;
        }
        Sleep(1u);
    }
    if (!replaced) {
#else
    if (rename(temp_path, path) != 0) {
#endif
#ifdef _WIN32
        if (replace_error == ERROR_SUCCESS) replace_error = GetLastError();
        remove(temp_path);
        SetLastError(replace_error);
#else
        int replace_error = errno;
        remove(temp_path);
        errno = replace_error;
#endif
        return -1;
    }
    return 0;
}


int write_private_runtime_file(const char *path,
                                      const unsigned char *data,
                                      size_t data_size)
{
#ifdef _WIN32
    return write_binary_file(path, data, data_size);
#else
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    int rc = 0;
    if (data_size > 0 && fwrite(data, 1, data_size, file) != data_size) rc = -1;
    if (fflush(file) != 0 || fsync(fileno(file)) != 0) rc = -1;
    if (fclose(file) != 0) rc = -1;
    return rc;
#endif
}


void make_safe_outbox_token(const char *source, char *out, size_t out_size)
{
    size_t used = 0;
    if (out_size == 0) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)source; p && *p && used + 1 < out_size; p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            out[used++] = (char)*p;
        }
    }
    if (used == 0) {
        copy_text(out, out_size, "save");
    }
    else {
        out[used] = '\0';
    }
}


int make_private_runtime_file(const char *path)
{
#ifdef _WIN32
    (void)path;
    return 0;
#else
    struct stat status;
    if (lstat(path, &status) != 0 || !S_ISREG(status.st_mode)) return -1;
    return chmod(path, 0600);
#endif
}


int append_flushed_text_line(const char *path, const char *line)
{
#ifdef _WIN32
    FILE *file = integral_fopen(path, "ab");
#else
    int flags = O_WRONLY | O_CREAT | O_APPEND;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags, 0600);
    if (fd < 0) return -1;
    if (fchmod(fd, 0600) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return -1;
    }
    FILE *file = fdopen(fd, "ab");
#endif
    if (!file) {
#ifndef _WIN32
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
#endif
        return -1;
    }
    int rc = 0;
    if (fwrite(line, 1, strlen(line), file) != strlen(line) ||
        fwrite("\n", 1, 1, file) != 1) {
        rc = -1;
    }
    if (fflush(file) != 0) {
        rc = -1;
    }
#ifdef _WIN32
    if (_commit(_fileno(file)) != 0) {
#else
    if (fsync(fileno(file)) != 0) {
#endif
        rc = -1;
    }
    if (fclose(file) != 0) {
        rc = -1;
    }
    return rc;
}


int read_binary_file_alloc(const char *path, unsigned char **out, size_t *out_size, size_t max_size)
{
    *out = NULL;
    *out_size = 0;
    FILE *file = integral_fopen(path, "rb");
    if (!file) {
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return -1;
    }
    long size = ftell(file);
    if (size < 0 || (size_t)size > max_size || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return -1;
    }
    if (size == 0) {
        fclose(file);
        return 0;
    }
    unsigned char *data = malloc((size_t)size);
    if (!data) {
        fclose(file);
        return -1;
    }
    size_t n = fread(data, 1, (size_t)size, file);
    bool ok = ferror(file) == 0 && n == (size_t)size;
    fclose(file);
    if (!ok) {
        free(data);
        return -1;
    }
    *out = data;
    *out_size = n;
    return 0;
}


void make_safe_n64_runtime_save_name(const char *source, char *out, size_t out_size)
{
    size_t used = 0;
    if (out_size == 0) {
        return;
    }
    for (const unsigned char *p = (const unsigned char *)source; p && *p && used + 1 < out_size; p++) {
        if (isalnum(*p) || *p == '_' || *p == '-') {
            out[used++] = (char)*p;
        }
        else if (*p == ' ' || *p == '.') {
            out[used++] = '_';
        }
    }
    if (used == 0) {
        copy_text(out, out_size, "n64_save");
    }
    else {
        out[used] = '\0';
    }
}

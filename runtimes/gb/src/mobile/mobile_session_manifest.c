/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_session_manifest.h"

#include "../server/content_hash.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void fail(char *out, size_t size, const char *message) { if (out && size) snprintf(out, size, "%s", message); }
static int identifier(const char *value)
{
    size_t n = value ? strlen(value) : 0u; if (!n || n >= 96u) return 0;
    for (size_t i = 0; i < n; ++i) { char c = value[i]; if (!((c >= 'a' && c <= 'z') || isdigit((unsigned char)c) || c == '_' || c == '-' || c == '.')) return 0; }
    return 1;
}
static int digest_string(const char *value)
{
    if (!value || strlen(value) != 64u) return 0;
    for (size_t i = 0; i < 64u; ++i) if (!isdigit((unsigned char)value[i]) && !(value[i] >= 'a' && value[i] <= 'f')) return 0;
    return 1;
}
static int parse_size(const char *value, size_t *out)
{
    char *end = NULL;
    errno = 0;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (errno || !end || *end || parsed > 16u * 1024u * 1024u) {
        return -1;
    }
    *out = (size_t)parsed;
    return 0;
}
static int read_hash(const char *path, size_t expected_size, char hex[65])
{
    struct stat info;
#ifndef _WIN32
    if (lstat(path, &info) || S_ISLNK(info.st_mode)) return -1;
#endif
    if (stat(path, &info) || !S_ISREG(info.st_mode) || (size_t)info.st_size != expected_size) return -1;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    IntegralGBRuntimeContentSha256 hash;
    integral_gb_runtime_content_sha256_init(&hash);
    unsigned char buffer[8192];
    size_t total = 0;
    for (;;) {
        size_t n = fread(buffer, 1, sizeof(buffer), file);
        if (n) {
            integral_gb_runtime_content_sha256_update(&hash, buffer, n);
            total += n;
        }
        if (n < sizeof(buffer)) break;
    }
    if (ferror(file) || fclose(file) || total != expected_size) {
        return -1;
    }
    unsigned char digest[32];
    integral_gb_runtime_content_sha256_finish(&hash, digest);
    integral_gb_runtime_content_sha256_hex(digest, hex);
    return 0;
}
static int assign(char *target, size_t target_size, const char *value) { if (strlen(value) >= target_size) return -1; memcpy(target, value, strlen(value) + 1u); return 0; }

int integral_gb_runtime_mobile_session_manifest_load(const char *manifest_path, IntegralGBRuntimeMobileSessionManifest *manifest, char *error_out, size_t error_out_size)
{
    if (!manifest_path || !manifest) { fail(error_out, error_out_size, "manifest input invalid"); return -1; }
    memset(manifest, 0, sizeof(*manifest)); FILE *file = fopen(manifest_path, "rb"); if (!file) { fail(error_out, error_out_size, "manifest open failed"); return -1; }
    char line[1400]; size_t declared_count = (size_t)-1; int schema = 0;
    while (fgets(line, sizeof(line), file)) {
        char *newline = strchr(line, '\n'); if (!newline) { fclose(file); fail(error_out, error_out_size, "manifest line too long"); return -1; } *newline = '\0';
        char *equals = strchr(line, '='); if (!equals) goto invalid; *equals++ = '\0';
        if (!strcmp(line, "schema")) schema = !strcmp(equals, "2");
        else if (!strcmp(line, "adapter")) { if (assign(manifest->adapter_id, sizeof(manifest->adapter_id), equals)) goto invalid; }
        else if (!strcmp(line, "package")) { if (assign(manifest->package_id, sizeof(manifest->package_id), equals)) goto invalid; }
        else if (!strcmp(line, "release")) { if (assign(manifest->release_id, sizeof(manifest->release_id), equals)) goto invalid; }
        else if (!strcmp(line, "package_digest")) { if (assign(manifest->package_digest, sizeof(manifest->package_digest), equals)) goto invalid; }
        else if (!strcmp(line, "runtime_capability")) { size_t value; if (parse_size(equals, &value) || value < 2u || value > 3u) goto invalid; manifest->runtime_capability_version = (unsigned)value; }
        else if (!strcmp(line, "artifact_count")) { if (parse_size(equals, &declared_count) || declared_count > INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_MAX_ARTIFACTS) goto invalid; manifest->artifact_count = declared_count; }
        else if (!strncmp(line, "artifact.", 9u)) {
            char *field = strchr(line + 9, '.'); if (!field) goto invalid; *field++ = '\0'; size_t index; if (parse_size(line + 9, &index) || index >= INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_MAX_ARTIFACTS) goto invalid;
            IntegralGBRuntimeMobileManifestArtifact *a = &manifest->artifacts[index];
            if (!strcmp(field, "role")) { if (assign(a->role, sizeof(a->role), equals)) goto invalid; }
            else if (!strcmp(field, "content_id")) { if (assign(a->content_id, sizeof(a->content_id), equals)) goto invalid; }
            else if (!strcmp(field, "path")) { if (assign(a->path, sizeof(a->path), equals)) goto invalid; }
            else if (!strcmp(field, "size")) { if (parse_size(equals, &a->size)) goto invalid; }
            else if (!strcmp(field, "sha256")) { if (assign(a->sha256, sizeof(a->sha256), equals)) goto invalid; }
            else goto invalid;
        } else goto invalid;
    }
    if (ferror(file) || fclose(file) || !schema || declared_count == (size_t)-1 || strcmp(manifest->adapter_id, "gb_mobile_v2") || !identifier(manifest->package_id) || !identifier(manifest->release_id) || !digest_string(manifest->package_digest) || manifest->runtime_capability_version < 2u || manifest->runtime_capability_version > 3u) goto failed;
    char directory[INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_PATH_MAX];
    if (strlen(manifest_path) >= sizeof(directory)) goto failed;
    memcpy(directory, manifest_path, strlen(manifest_path) + 1u);
    char *slash = strrchr(directory, '/');
#ifdef _WIN32
    char *backslash = strrchr(directory, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    if (!slash) goto failed;
    *slash = '\0';
    size_t directory_size = strlen(directory);
    size_t total = 0;
    for (size_t i = 0; i < manifest->artifact_count; ++i) {
        IntegralGBRuntimeMobileManifestArtifact *a = &manifest->artifacts[i]; char actual[65];
        if (!identifier(a->role) || !identifier(a->content_id) || !digest_string(a->sha256) ||
            !a->path[0] || strstr(a->path, "..") ||
            strncmp(a->path, directory, directory_size) ||
            (a->path[directory_size] != '/' && a->path[directory_size] != '\\') ||
            strchr(a->path + directory_size + 1u, '/') ||
            strchr(a->path + directory_size + 1u, '\\')) goto failed;
        for (size_t prior = 0; prior < i; ++prior) if (!strcmp(a->role, manifest->artifacts[prior].role) || !strcmp(a->content_id, manifest->artifacts[prior].content_id)) goto failed;
        if (read_hash(a->path, a->size, actual) || strcmp(actual, a->sha256)) goto failed;
        total += a->size; if (total > 32u * 1024u * 1024u) goto failed;
    }
    return 0;
invalid:
    fclose(file);
failed:
    memset(manifest, 0, sizeof(*manifest)); fail(error_out, error_out_size, "manifest validation failed"); return -1;
}

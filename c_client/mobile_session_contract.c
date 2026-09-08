/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_session_contract.h"

#include "../runtimes/gb/src/server/content_hash.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

typedef enum JsonKind { JSON_UNDEFINED, JSON_OBJECT, JSON_ARRAY, JSON_STRING, JSON_PRIMITIVE } JsonKind;
typedef struct JsonToken { JsonKind kind; int start; int end; int size; int parent; } JsonToken;
typedef struct JsonParser { unsigned pos; unsigned next; int parent; } JsonParser;

static void fail(char *out, size_t size, const char *message)
{
    if (out && size) snprintf(out, size, "%s", message);
}

static JsonToken *alloc_token(JsonParser *p, JsonToken *tokens, size_t count)
{
    if (p->next >= count) return NULL;
    JsonToken *token = &tokens[p->next++];
    token->kind = JSON_UNDEFINED; token->start = token->end = -1;
    token->size = 0; token->parent = -1;
    return token;
}

static int parse_json(const char *text, JsonToken *tokens, size_t count)
{
    JsonParser p = {0, 0, -1};
    for (; text[p.pos]; ++p.pos) {
        char c = text[p.pos];
        if (isspace((unsigned char)c) || c == ':' || c == ',') continue;
        if (c == '{' || c == '[') {
            JsonToken *t = alloc_token(&p, tokens, count); if (!t) return -1;
            t->kind = c == '{' ? JSON_OBJECT : JSON_ARRAY; t->start = (int)p.pos; t->parent = p.parent;
            if (p.parent >= 0) tokens[p.parent].size++;
            p.parent = (int)p.next - 1;
        } else if (c == '}' || c == ']') {
            JsonKind expected = c == '}' ? JSON_OBJECT : JSON_ARRAY;
            int index = p.parent;
            if (index < 0 || tokens[index].kind != expected) return -1;
            tokens[index].end = (int)p.pos + 1; p.parent = tokens[index].parent;
        } else if (c == '"') {
            JsonToken *t = alloc_token(&p, tokens, count); if (!t) return -1;
            t->kind = JSON_STRING; t->start = (int)++p.pos; t->parent = p.parent;
            for (; text[p.pos] && text[p.pos] != '"'; ++p.pos) {
                if ((unsigned char)text[p.pos] < 0x20u) return -1;
                if (text[p.pos] == '\\') {
                    if (!text[++p.pos] || strchr("\"\\/bfnrt", text[p.pos]) == NULL) return -1;
                }
            }
            if (text[p.pos] != '"') return -1;
            t->end = (int)p.pos; if (p.parent >= 0) tokens[p.parent].size++;
        } else {
            JsonToken *t = alloc_token(&p, tokens, count); if (!t) return -1;
            t->kind = JSON_PRIMITIVE; t->start = (int)p.pos; t->parent = p.parent;
            while (text[p.pos] && !isspace((unsigned char)text[p.pos]) &&
                   strchr(",]}:", text[p.pos]) == NULL) ++p.pos;
            t->end = (int)p.pos; --p.pos; if (p.parent >= 0) tokens[p.parent].size++;
        }
    }
    if (p.parent != -1 || p.next == 0 || tokens[0].kind != JSON_OBJECT) return -1;
    return (int)p.next;
}

static int token_next(const JsonToken *tokens, int count, int index)
{
    int end = tokens[index].end;
    for (++index; index < count && tokens[index].start < end; ++index) {}
    return index;
}

static int token_equals(const char *json, const JsonToken *token, const char *value)
{
    size_t n = strlen(value);
    return token->kind == JSON_STRING && token->end - token->start == (int)n &&
           memcmp(json + token->start, value, n) == 0;
}

static int object_value(const char *json, const JsonToken *tokens, int count, int object, const char *key)
{
    if (object < 0 || tokens[object].kind != JSON_OBJECT) return -1;
    int index = object + 1;
    while (index < count && tokens[index].start < tokens[object].end) {
        int value = index + 1;
        if (value >= count || tokens[index].kind != JSON_STRING) return -1;
        if (token_equals(json, &tokens[index], key)) return value;
        index = token_next(tokens, count, value);
    }
    return -1;
}

static int copy_identifier(const char *json, const JsonToken *token, char *out, size_t out_size)
{
    int length = token->end - token->start;
    if (token->kind != JSON_STRING || length <= 0 || (size_t)length >= out_size) return -1;
    for (int i = 0; i < length; ++i) {
        char c = json[token->start + i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return -1;
    }
    memcpy(out, json + token->start, (size_t)length); out[length] = '\0'; return 0;
}

static int parse_integer(const char *json, const JsonToken *token, long long *out)
{
    char value[32]; int length = token->end - token->start; char *end = NULL;
    if (token->kind != JSON_PRIMITIVE || length <= 0 || (size_t)length >= sizeof(value)) return -1;
    memcpy(value, json + token->start, (size_t)length); value[length] = '\0'; errno = 0;
    long long result = strtoll(value, &end, 10);
    if (errno || !end || *end) return -1;
    *out = result;
    return 0;
}

static int lower_sha256(const char *value)
{
    if (strlen(value) != 64u) return 0;
    for (size_t i = 0; i < 64u; ++i) if (!isdigit((unsigned char)value[i]) && !(value[i] >= 'a' && value[i] <= 'f')) return 0;
    return 1;
}

static int b64value(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2;
    return -1;
}

static int decode_base64(const char *json, const JsonToken *token, unsigned char *out, size_t capacity, size_t *size_out)
{
    int length = token->end - token->start; size_t used = 0;
    if (token->kind != JSON_STRING || length < 0 || length % 4 != 0) return -1;
    for (int offset = 0; offset < length; offset += 4) {
        int v[4]; for (int i = 0; i < 4; ++i) { v[i] = b64value(json[token->start + offset + i]); if (v[i] == -1) return -1; }
        if (v[0] < 0 || v[1] < 0 || (v[2] == -2 && v[3] != -2) || (offset + 4 < length && (v[2] < 0 || v[3] < 0))) return -1;
        unsigned n = ((unsigned)v[0] << 18) | ((unsigned)v[1] << 12) | (v[2] >= 0 ? (unsigned)v[2] << 6 : 0u) | (v[3] >= 0 ? (unsigned)v[3] : 0u);
        if (used >= capacity) return -1;
        out[used++] = (unsigned char)(n >> 16);
        if (v[2] >= 0) {
            if (used >= capacity) return -1;
            out[used++] = (unsigned char)(n >> 8);
        }
        if (v[3] >= 0) {
            if (used >= capacity) return -1;
            out[used++] = (unsigned char)n;
        }
    }
    *size_out = used; return 0;
}

void integral_mobile_runtime_contract_init(IntegralMobileRuntimeContract *contract) { if (contract) memset(contract, 0, sizeof(*contract)); }
void integral_mobile_runtime_contract_free(IntegralMobileRuntimeContract *contract)
{
    if (!contract) return;
    for (size_t i = 0; i < contract->artifact_count; ++i) {
        free(contract->artifacts[i].data);
    }
    memset(contract, 0, sizeof(*contract));
}

int integral_mobile_runtime_contract_parse(const char *json, IntegralMobileRuntimeContract *contract, char *error_out, size_t error_out_size)
{
    JsonToken tokens[512]; int count; int root; long long integer; size_t total = 0;
    if (!json || !contract) { fail(error_out, error_out_size, "runtime contract input invalid"); return -1; }
    integral_mobile_runtime_contract_init(contract); count = parse_json(json, tokens, 512u);
    if (count < 0) { fail(error_out, error_out_size, "runtime contract JSON invalid"); return -1; }
    root = object_value(json, tokens, count, 0, "runtime_contract"); if (root < 0) root = 0;
    int schema = object_value(json, tokens, count, root, "schema_version");
    int adapter = object_value(json, tokens, count, root, "adapter_id");
    int package = object_value(json, tokens, count, root, "package_id");
    int release = object_value(json, tokens, count, root, "release_id");
    int digest = object_value(json, tokens, count, root, "package_digest");
    int capability = object_value(json, tokens, count, root, "runtime_capability_version");
    int artifacts = object_value(json, tokens, count, root, "artifacts");
    if (schema < 0 || adapter < 0 || package < 0 || release < 0 || digest < 0 || capability < 0 ||
        tokens[root].size != 14 ||
        parse_integer(json, &tokens[schema], &integer) || integer != 2 ||
        copy_identifier(json, &tokens[adapter], contract->adapter_id, sizeof(contract->adapter_id)) || strcmp(contract->adapter_id, "gb_mobile_v2") ||
        copy_identifier(json, &tokens[package], contract->package_id, sizeof(contract->package_id)) ||
        copy_identifier(json, &tokens[release], contract->release_id, sizeof(contract->release_id)) ||
        copy_identifier(json, &tokens[digest], contract->package_digest, sizeof(contract->package_digest)) || !lower_sha256(contract->package_digest) ||
        parse_integer(json, &tokens[capability], &integer) || integer < 2 || integer > 3 ||
        artifacts < 0 || tokens[artifacts].kind != JSON_ARRAY) goto invalid;
    contract->schema_version = 2;
    contract->runtime_capability_version = (int)integer;
    int index = artifacts + 1;
    while (index < count && tokens[index].start < tokens[artifacts].end) {
        if (contract->artifact_count >= INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACTS || tokens[index].kind != JSON_OBJECT) goto invalid;
        IntegralMobileArtifact *a = &contract->artifacts[contract->artifact_count];
        int role = object_value(json, tokens, count, index, "role"); int cid = object_value(json, tokens, count, index, "content_id");
        int size = object_value(json, tokens, count, index, "size"); int hash = object_value(json, tokens, count, index, "sha256"); int data = object_value(json, tokens, count, index, "data");
        if (tokens[index].size != 10 || role < 0 || cid < 0 || size < 0 || hash < 0 || data < 0 || copy_identifier(json, &tokens[role], a->role, sizeof(a->role)) || copy_identifier(json, &tokens[cid], a->content_id, sizeof(a->content_id)) || copy_identifier(json, &tokens[hash], a->sha256, sizeof(a->sha256)) || !lower_sha256(a->sha256) || parse_integer(json, &tokens[size], &integer) || integer < 0 || integer > INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACT_SIZE) goto invalid;
        for (size_t prior = 0; prior < contract->artifact_count; ++prior) if (!strcmp(a->role, contract->artifacts[prior].role) || !strcmp(a->content_id, contract->artifacts[prior].content_id)) goto invalid;
        a->data = malloc(integer ? (size_t)integer : 1u); if (!a->data) goto invalid; a->size = (size_t)integer;
        size_t decoded = 0; if (decode_base64(json, &tokens[data], a->data, a->size, &decoded) || decoded != a->size) goto invalid;
        unsigned char digest[32]; char digest_hex[65]; integral_gb_runtime_content_sha256(a->data, a->size, digest); integral_gb_runtime_content_sha256_hex(digest, digest_hex);
        if (strcmp(digest_hex, a->sha256)) goto invalid;
        total += a->size; if (total > INTEGRAL_MOBILE_CONTRACT_MAX_TOTAL_SIZE) goto invalid;
        contract->artifact_count++; index = token_next(tokens, count, index);
    }
    return 0;
invalid:
    integral_mobile_runtime_contract_free(contract); fail(error_out, error_out_size, "runtime contract validation failed"); return -1;
}

static int safe_component(const char *value) { return value && value[0] && strstr(value, "..") == NULL && strchr(value, '/') == NULL && strchr(value, '\\') == NULL; }

static FILE *open_private_runtime_file(const char *path)
{
#ifdef _WIN32
    return fopen(path, "wb");
#else
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    int fd = open(path, flags, 0600);
    if (fd < 0) return NULL;
    if (fchmod(fd, 0600) != 0) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
        return NULL;
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
        int saved_errno = errno;
        close(fd);
        errno = saved_errno;
    }
    return file;
#endif
}

int integral_mobile_runtime_contract_write_manifest(const IntegralMobileRuntimeContract *contract, const char *session_directory, const char *manifest_path, char *error_out, size_t error_out_size)
{
    if (!contract || !safe_component(contract->package_id) || !safe_component(contract->release_id) || !session_directory || !manifest_path) { fail(error_out, error_out_size, "manifest request invalid"); return -1; }
    FILE *manifest = open_private_runtime_file(manifest_path); if (!manifest) { fail(error_out, error_out_size, "manifest open failed"); return -1; }
    int ok = fprintf(manifest, "schema=2\nadapter=%s\npackage=%s\nrelease=%s\npackage_digest=%s\nruntime_capability=%d\nartifact_count=%zu\n", contract->adapter_id, contract->package_id, contract->release_id, contract->package_digest, contract->runtime_capability_version, contract->artifact_count) > 0;
    for (size_t i = 0; ok && i < contract->artifact_count; ++i) {
        const IntegralMobileArtifact *a = &contract->artifacts[i]; char path[1024]; snprintf(path, sizeof(path), "%s/artifact-%02zu.bin", session_directory, i);
        FILE *file = open_private_runtime_file(path);
        if (!file) {
            ok = 0;
            break;
        }
        int artifact_ok = !a->size || fwrite(a->data, 1, a->size, file) == a->size;
        if (fclose(file)) artifact_ok = 0;
        if (!artifact_ok) {
            ok = 0;
            break;
        }
        ok = fprintf(manifest, "artifact.%zu.role=%s\nartifact.%zu.content_id=%s\nartifact.%zu.path=%s\nartifact.%zu.size=%zu\nartifact.%zu.sha256=%s\n", i, a->role, i, a->content_id, i, path, i, a->size, i, a->sha256) > 0;
    }
    if (fclose(manifest)) ok = 0;
    if (!ok) {
        fail(error_out, error_out_size, "manifest write failed");
        return -1;
    }
    return 0;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_save_sync.h"
#include "client_save_outbox.h"
#include "client_file_io.h"
#include "http_client.h"
#include "../runtimes/gb/src/server/content_hash.h"
#include <errno.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#endif

enum { INFLIGHT_HEADER = 4096 };
typedef struct SaveInflight {
    char server[160], account[64], save_id[96], source[INTEGRAL_CONFIG_PATH_MAX];
    char hash[65], request[192], error[160];
    int revision, committed;
    unsigned char bytes[INTEGRAL_MAX_SAVE_BYTES];
    size_t size;
} SaveInflight;

static void data_hash(const void *data, size_t size, char out[65])
{
    uint8_t digest[32];
    integral_gb_runtime_content_sha256(data, size, digest);
    integral_gb_runtime_content_sha256_hex(digest, out);
}

static bool inflight_path(const char *server, const char *account, const char *id,
                          char *path, size_t capacity)
{
    char scope[512], hash[65];
    int n = snprintf(scope, sizeof(scope), "%s\n%s\n%s", server, account, id);
    if (n < 0 || (size_t)n >= sizeof(scope) || strchr(server, '\n') ||
        strchr(account, '\n') || strchr(id, '\n')) return false;
    data_hash(scope, (size_t)n, hash);
    n = snprintf(path, capacity, INTEGRAL_SAVE_OUTBOX_DIR "/%s.inflight", hash);
    return n > 0 && (size_t)n < capacity;
}

static bool inflight_write(const char *path, const SaveInflight *entry)
{
    unsigned char *record = calloc(1, INFLIGHT_HEADER + entry->size);
    if (!record) return false;
    int n = snprintf((char *)record, INFLIGHT_HEADER,
        "inflight=1\tserver=%s\taccount=%s\tsave_id=%s\tpath=%s\texpected_revision=%d"
        "\tcommitted=%d\tsha256=%s\trequest_id=%s\terror=%s\n",
        entry->server, entry->account, entry->save_id, entry->source,
        entry->revision, entry->committed, entry->hash, entry->request, entry->error);
    bool ok = n > 0 && n < INFLIGHT_HEADER;
    if (ok) {
        memcpy(record + INFLIGHT_HEADER, entry->bytes, entry->size);
        ok = atomic_replace_binary_file(path, record, INFLIGHT_HEADER + entry->size) == 0;
    }
    free(record);
    return ok;
}

static bool inflight_read(const char *path, SaveInflight *entry)
{
    unsigned char *record = NULL;
    size_t size = 0;
    if (read_binary_file_alloc(path, &record, &size, INFLIGHT_HEADER + INTEGRAL_MAX_SAVE_BYTES) != 0)
        return false;
    bool ok = size > INFLIGHT_HEADER && memchr(record, 0, INFLIGHT_HEADER) != NULL;
    char revision[32], committed[32], hash[65];
#define FIELD(key, member) extract_tsv_field((char *)record, key, entry->member, sizeof(entry->member))
    ok = ok && FIELD("server", server) && FIELD("account", account) &&
        FIELD("save_id", save_id) && FIELD("path", source) && FIELD("sha256", hash) &&
        FIELD("request_id", request) && FIELD("error", error) &&
        extract_tsv_field((char *)record, "expected_revision", revision, sizeof(revision)) &&
        extract_tsv_field((char *)record, "committed", committed, sizeof(committed));
#undef FIELD
    if (ok) {
        entry->revision = atoi(revision);
        entry->committed = atoi(committed);
        entry->size = size - INFLIGHT_HEADER;
        memcpy(entry->bytes, record + INFLIGHT_HEADER, entry->size);
        data_hash(entry->bytes, entry->size, hash);
        ok = entry->revision >= 0 && entry->committed >= 0 && !strcmp(hash, entry->hash);
    }
    free(record);
    return ok;
}

/* Always persist the immutable request before PUT. A committed envelope is
 * retained until its successor is atomically staged, so a crash cannot lose
 * either the recovered revision or the ordering of the latest working SAV. */
bool process_save_inflight(const char *server, const char *token, const char *game,
                           long long fence, LocalSyncSlot *slot, bool final)
{
    char path[INTEGRAL_CONFIG_PATH_MAX];
    if (!inflight_path(server, slot->account, slot->save_id, path, sizeof(path)) ||
        ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) return false;
    SaveInflight *entry = calloc(1, sizeof(*entry));
    if (!entry) return false;
    bool exists = local_file_exists(path), ok = false;
    if (exists && (!inflight_read(path, entry) || strcmp(entry->server, server) ||
        strcmp(entry->account, slot->account) || strcmp(entry->save_id, slot->save_id))) goto done;
    if (!exists) {
        snprintf(entry->server, sizeof(entry->server), "%s", server);
        snprintf(entry->account, sizeof(entry->account), "%s", slot->account);
        snprintf(entry->save_id, sizeof(entry->save_id), "%s", slot->save_id);
        entry->committed = slot->revision;
        snprintf(entry->hash, sizeof(entry->hash), "%s", slot->last_hash);
    }
    /* At most the outstanding request and one subsequent snapshot per call. */
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
        if (!exists || entry->committed) {
            int revision = exists ? entry->committed : slot->revision;
            unsigned char latest[INTEGRAL_MAX_SAVE_BYTES];
            size_t size = 0;
            char hash[65];
            if (read_binary_file(slot->save_path, latest, sizeof(latest), &size) != 0) goto done;
            data_hash(latest, size, hash);
            slot->revision = revision;
            if (!strcmp(hash, entry->hash)) {
                snprintf(slot->last_hash, sizeof(slot->last_hash), "%s", hash);
                if (exists && remove(path) != 0) goto done;
                slot->preserve_save_path = false;
                ok = true;
                goto done;
            }
            entry->revision = revision;
            entry->committed = 0;
            entry->size = size;
            memcpy(entry->bytes, latest, size);
            snprintf(entry->hash, sizeof(entry->hash), "%s", hash);
            snprintf(entry->source, sizeof(entry->source), "%s", slot->save_path);
            snprintf(entry->request, sizeof(entry->request), "save-%s-r%d-%.24s",
                     slot->save_id, revision, hash);
            snprintf(entry->error, sizeof(entry->error), "SAVE RESPONSE PENDING");
            if (!inflight_write(path, entry)) goto done;
            exists = true;
        }
        int revision = 0;
        char error[160] = {0};
        int result = game && game[0]
            ? integral_api_upload_save_fenced(server, token, entry->save_id, entry->revision,
                entry->bytes, entry->size, game, fence, entry->request, &revision, error, sizeof(error))
            : integral_api_upload_save_with_request_id(server, token, entry->save_id, entry->revision,
                entry->bytes, entry->size, entry->request, &revision, error, sizeof(error));
        if (result != 0) {
            /* A definite HTTP refusal is distinguished in diagnostics, but
             * never authorizes skipping an older unresolved request. */
            snprintf(entry->error, sizeof(entry->error), "%s: %.120s",
                     result > 0 ? "SAVE REJECTED" : "SAVE RESULT UNKNOWN", error);
            for (char *p = entry->error; *p; ++p) if (*p == '\t' || *p == '\n' || *p == '\r') *p = ' ';
            /* An unfenced recovery may be refused while the original monitor
             * still owns the server lock. Do not overwrite that monitor's
             * newer envelope with this reader's older snapshot. */
            if (game && game[0]) (void)inflight_write(path, entry);
            slot->preserve_save_path = true;
            ok = final; /* request and latest working file both retained */
            goto done;
        }
        if (revision <= entry->revision) goto done;
        entry->committed = revision;
        entry->error[0] = 0;
        if (!inflight_write(path, entry)) goto done;
        slot->revision = revision;
        snprintf(slot->last_hash, sizeof(slot->last_hash), "%s", entry->hash);
        char latest_hash[65];
        if (sha256_file_hex(slot->save_path, latest_hash, sizeof(latest_hash)) == 0 &&
            !strcmp(latest_hash, entry->hash)) {
            if (remove(path) != 0) goto done;
            slot->preserve_save_path = false;
            ok = true;
            goto done;
        }
    }
    /* The committed envelope is also retained across this bounded return. */
    slot->preserve_save_path = true;
    ok = true;
done:
    if (!ok && exists) slot->preserve_save_path = true;
    free(entry);
    return ok;
}

#include <dirent.h>
#include <sys/stat.h>

#ifdef _WIN32
static struct tm *localtime_r(const time_t *timep, struct tm *result)
{
    return localtime_s(result, timep) == 0 ? result : NULL;
}

#endif

static bool local_regular_file(const char *path)
{
    struct stat info;
    return path && path[0] && stat(path, &info) == 0 && S_ISREG(info.st_mode);
}


static bool managed_runtime_file_path(const char *path)
{
    static const char prefix[] = "runtime/";
    if (!path || strncmp(path, prefix, sizeof(prefix) - 1u) != 0) return false;
    const char *component = path + sizeof(prefix) - 1u;
    while (*component) {
        const char *end = strchr(component, '/');
        size_t length = end ? (size_t)(end - component) : strlen(component);
        if (length == 0 ||
            (length == 1 && component[0] == '.') ||
            (length == 2 && component[0] == '.' && component[1] == '.')) {
            return false;
        }
        if (!end) return true;
        component = end + 1;
    }
    return false;
}


bool extract_tsv_field(const char *line, const char *key, char *out, size_t out_size)
{
    size_t key_len = strlen(key);
    const char *p = line;
    while (p && *p) {
        if (strncmp(p, key, key_len) == 0 && p[key_len] == '=') {
            const char *value = p + key_len + 1;
            const char *end = strchr(value, '\t');
            if (!end) {
                end = value + strcspn(value, "\r\n");
            }
            size_t len = (size_t)(end - value);
            if (len >= out_size) {
                len = out_size > 0 ? out_size - 1 : 0;
            }
            if (out_size > 0) {
                memcpy(out, value, len);
                out[len] = '\0';
            }
            return true;
        }
        p = strchr(p, '\t');
        if (p) {
            p++;
        }
    }
    return false;
}


bool write_save_upload_outbox(const char *server,
                                     const LocalSyncSlot *sync_slot,
                                     const unsigned char *save_data,
                                     size_t save_size,
                                     const char *current_hash,
                                     const char *error,
                                     const char *game_session_id,
                                     long long fencing_token,
                                     const char *request_id)
{
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
        client_save_log("save_upload_outbox_failed", "save_id=%s reason=mkdir", sync_slot->save_id);
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_now);

    char safe_id[96];
    make_safe_outbox_token(sync_slot->save_id, safe_id, sizeof(safe_id));

    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(save_path,
             sizeof(save_path),
             "%s/%s_%ld_%s_rev%d.sav",
             INTEGRAL_SAVE_OUTBOX_DIR,
             timestamp,
             (long)getpid(),
             safe_id,
             sync_slot->revision);
    if (atomic_replace_binary_file(save_path, save_data, save_size) != 0) {
        client_save_log("save_upload_outbox_failed", "save_id=%s reason=write path=%s", sync_slot->save_id, save_path);
        return false;
    }

    char line[4096];
    snprintf(line,
             sizeof(line),
             "%s\tserver=%s\tsave_id=%s\texpected_revision=%d\tbytes=%zu\tsha256=%s\tpath=%s\tgame_session_id=%s\tfencing_token=%lld\trequest_id=%s\taccount=%s\terror=%s",
             timestamp,
             server,
             sync_slot->save_id,
             sync_slot->revision,
             save_size,
             current_hash,
             save_path,
             game_session_id ? game_session_id : "",
             fencing_token,
             request_id ? request_id : "",
             sync_slot->account,
             error ? error : "");
    char record_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(record_path, sizeof(record_path), "%s.pending", save_path);
    if (atomic_replace_binary_file(record_path, (const unsigned char *)line, strlen(line)) != 0) {
        client_save_log("save_upload_outbox_record_failed",
                   "save_id=%s path=%s record=%s",
                   sync_slot->save_id,
                   save_path,
                   record_path);
        return false;
    }
    client_save_log("save_upload_outbox_written",
               "save_id=%s path=%s record=%s",
               sync_slot->save_id,
               save_path,
               record_path);
    return true;
}


bool write_save_recovery_pointer(const char *server,
                                        const LocalSyncSlot *sync_slot,
                                        const char *source_path,
                                        const char *error,
                                        const char *game_session_id,
                                        long long fencing_token)
{
    char inflight[INTEGRAL_CONFIG_PATH_MAX];
    if (inflight_path(server, sync_slot->account, sync_slot->save_id, inflight, sizeof(inflight)) &&
        local_file_exists(inflight)) {
        /* The outstanding request owns recovery order and its working file.
         * Do not create a second, stale-revision upload for that file. */
        return local_regular_file(source_path);
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
        return false;
    }
    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_now);
    char safe_id[96];
    make_safe_outbox_token(sync_slot->save_id, safe_id, sizeof(safe_id));
    char record_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(record_path,
             sizeof(record_path),
             "%s/%s_%ld_%s_rev%d.recovery.pending",
             INTEGRAL_SAVE_OUTBOX_DIR,
             timestamp,
             (long)getpid(),
             safe_id,
             sync_slot->revision);
    char line[4096];
    snprintf(line,
             sizeof(line),
             "%s\tserver=%s\tsave_id=%s\texpected_revision=%d\tbytes=unknown\tsha256=unknown\tpath=%s\tgame_session_id=%s\tfencing_token=%lld\trequest_id=%s-%ld-%s-%d\taccount=%s\terror=%s",
             timestamp,
             server,
             sync_slot->save_id,
             sync_slot->revision,
             source_path,
             game_session_id ? game_session_id : "",
             fencing_token,
             timestamp,
             (long)getpid(),
             safe_id,
             sync_slot->revision,
             sync_slot->account,
             error ? error : "recovery_required");
    if (atomic_replace_binary_file(record_path, (const unsigned char *)line, strlen(line)) != 0) {
        client_save_log("save_recovery_record_failed", "save_id=%s source=%s", sync_slot->save_id, source_path);
        return false;
    }
    unsigned char *verified = NULL;
    size_t verified_size = 0;
    bool verified_ok = local_regular_file(source_path) &&
                       read_binary_file_alloc(record_path, &verified, &verified_size, sizeof(line)) == 0 &&
                       verified_size == strlen(line) &&
                       memcmp(verified, line, verified_size) == 0;
    free(verified);
    if (!verified_ok) {
        (void)remove(record_path);
        client_save_log("save_recovery_record_failed",
                   "save_id=%s source=%s reason=readback",
                   sync_slot->save_id,
                   source_path);
        return false;
    }
    client_save_log("save_recovery_record_written",
               "save_id=%s source=%s record=%s",
               sync_slot->save_id,
               source_path,
               record_path);
    return true;
}


bool complete_replayed_outbox_entry(const char *save_path,
                                            const char *record_path,
                                            const char *completion_path,
                                            const char *completion,
                                            size_t completion_size)
{
    if (!save_path || !completion_path) return false;
    if (!local_file_exists(completion_path) &&
        (!completion || completion_size == 0 ||
         atomic_replace_binary_file(completion_path,
                                    (const unsigned char *)completion,
                                    completion_size) != 0)) {
        return false;
    }
    if (remove(save_path) != 0 && errno != ENOENT) return false;
    if (record_path && record_path[0] != '\0' &&
        remove(record_path) != 0 && errno != ENOENT) {
        return false;
    }
    return true;
}


static void replay_save_upload_outbox_line(const char *line,
                                           const char *record_path,
                                           const char *server,
                                           const char *token,
                                           const char *only_save_id,
                                           const char *account,
                                           unsigned *replayed,
                                           unsigned *pending)
{
    char entry_server[160];
    char save_id[96];
    char entry_account[64];
    char revision_text[32];
    char request_id[192];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    if (!extract_tsv_field(line, "server", entry_server, sizeof(entry_server)) ||
        !extract_tsv_field(line, "save_id", save_id, sizeof(save_id)) ||
        !extract_tsv_field(line, "expected_revision", revision_text, sizeof(revision_text)) ||
        !extract_tsv_field(line, "path", save_path, sizeof(save_path)) ||
        strcmp(entry_server, server) != 0 ||
        strcmp(save_id, only_save_id) != 0) {
        return;
    }
    if (!extract_tsv_field(line, "account", entry_account, sizeof(entry_account))) {
        (*pending)++;
        return;
    }
    if (strcmp(account, entry_account)) return;
    if (!extract_tsv_field(line, "request_id", request_id, sizeof(request_id))) {
        request_id[0] = '\0';
    }
    if (!managed_runtime_file_path(save_path) ||
        (make_private_runtime_file(save_path) != 0 && errno != ENOENT)) {
        (*pending)++;
        return;
    }

    char completion_path[INTEGRAL_CONFIG_PATH_MAX];
    int completion_length = snprintf(completion_path,
                                     sizeof(completion_path),
                                     "%s.complete",
                                     save_path);
    if (completion_length < 0 || (size_t)completion_length >= sizeof(completion_path)) {
        (*pending)++;
        return;
    }
    bool upload_completed = local_file_exists(completion_path);
    if (upload_completed && make_private_runtime_file(completion_path) != 0) {
        (*pending)++;
        return;
    }
    if (upload_completed) {
        if (!complete_replayed_outbox_entry(save_path, record_path,
                                            completion_path, NULL, 0)) {
            (*pending)++;
        }
        return;
    }
    unsigned char *save_data = NULL;
    size_t save_size = 0;
    if (read_binary_file_alloc(save_path, &save_data, &save_size, INTEGRAL_MAX_SAVE_BYTES) != 0) {
        (*pending)++;
        return;
    }

    char error[160];
    int next_revision = 0;
    int expected_revision = atoi(revision_text);
    if (integral_api_upload_save_with_request_id(server,
                                            token,
                                            save_id,
                                            expected_revision,
                                            save_data,
                                            save_size,
                                            request_id,
                                            &next_revision,
                                            error,
                                            sizeof(error)) != 0) {
        client_save_log("save_upload_outbox_replay_failed", "save_id=%s error=%s", save_id, error);
        (*pending)++;
        free(save_data);
        return;
    }
    free(save_data);

    char completion[384];
    int completion_size = snprintf(completion,
                                   sizeof(completion),
                                   "save_id=%s\texpected_revision=%d\tnext_revision=%d\trequest_id=%s",
                                   save_id,
                                   expected_revision,
                                   next_revision,
                                   request_id);
    if (completion_size < 0 || (size_t)completion_size >= sizeof(completion) ||
        !complete_replayed_outbox_entry(save_path,
                                        record_path,
                                        completion_path,
                                        completion,
                                        (size_t)completion_size)) {
        client_save_log("save_upload_outbox_completion_failed", "save_id=%s path=%s", save_id, completion_path);
        (*pending)++;
        return;
    }
    char replayed_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(replayed_path, sizeof(replayed_path), "%s/replayed.tsv", INTEGRAL_SAVE_OUTBOX_DIR);
    char replay_line[768];
    snprintf(replay_line,
             sizeof(replay_line),
             "save_id=%s\texpected_revision=%d\tnext_revision=%d\trequest_id=%s",
             save_id,
             expected_revision,
             next_revision,
             request_id);
    (void)append_flushed_text_line(replayed_path, replay_line);
    client_save_log("save_upload_outbox_replayed", "save_id=%s revision=%d", save_id, next_revision);
    (*replayed)++;
}


static bool outbox_record_pending(const char *record, const char *server, const char *save_id,
                                  const char *account, char *error, size_t error_size)
{
    char line[4096], entry_server[160], entry_save[96], path[INTEGRAL_CONFIG_PATH_MAX];
    FILE *file = fopen(record, "rb");
    if (!file) return false;
    bool pending = false;
    char entry_account[64];
    while (fgets(line, sizeof(line), file)) {
        if (!extract_tsv_field(line, "server", entry_server, sizeof(entry_server)) ||
            !extract_tsv_field(line, "save_id", entry_save, sizeof(entry_save)) ||
            strcmp(server, entry_server) || strcmp(save_id, entry_save)) continue;
        if (!extract_tsv_field(line, "account", entry_account, sizeof(entry_account))) {
            if (error_size) snprintf(error, error_size, "OUTBOX OWNER MISSING - RECOVERY REQUIRED");
            pending = true;
            break;
        }
        if (strcmp(account, entry_account)) continue;
        (void)extract_tsv_field(line, "error", error, error_size);
        char complete[INTEGRAL_CONFIG_PATH_MAX + 10];
        if (!extract_tsv_field(line, "path", path, sizeof(path)) || !managed_runtime_file_path(path)) {
            pending = true;
            break;
        }
        snprintf(complete, sizeof(complete), "%s.complete", path);
        if (!local_regular_file(complete)) { pending = true; break; }
    }
    fclose(file);
    return pending;
}

bool save_upload_outbox_pending(const char *server, const char *save_id,
                                const char *account, char *error, size_t error_size)
{
    if (!server || !save_id || !save_id[0]) return false;
    char inflight[INTEGRAL_CONFIG_PATH_MAX];
    if (inflight_path(server, account, save_id, inflight, sizeof(inflight)) && local_file_exists(inflight)) {
        SaveInflight *entry = calloc(1, sizeof(*entry));
        bool valid = entry && inflight_read(inflight, entry) && !strcmp(entry->server, server) &&
                     !strcmp(entry->account, account) && !strcmp(entry->save_id, save_id);
        if (error_size) snprintf(error, error_size, "%s", valid && entry->error[0] ? entry->error : "SAV RECOVERY PENDING");
        free(entry);
        return true;
    }
    if (outbox_record_pending(INTEGRAL_SAVE_OUTBOX_DIR "/outbox.tsv", server, save_id, account, error, error_size)) return true;
    DIR *directory = opendir(INTEGRAL_SAVE_OUTBOX_DIR);
    if (!directory) return false;
    bool pending = false;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        size_t size = strlen(entry->d_name);
        if (size <= 8 || strcmp(entry->d_name + size - 8, ".pending")) continue;
        char path[INTEGRAL_CONFIG_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", INTEGRAL_SAVE_OUTBOX_DIR, entry->d_name) >= (int)sizeof(path)) continue;
        if (outbox_record_pending(path, server, save_id, account, error, error_size)) { pending = true; break; }
    }
    closedir(directory);
    return pending;
}

unsigned replay_save_upload_outbox(const char *server,
                                          const char *token,
                                          const char *only_save_id,
                                          unsigned *pending_out, const char *account)
{
    if (pending_out) {
        *pending_out = 0;
    }
    if (ensure_private_runtime_directory("runtime") != 0 ||
        ensure_private_runtime_directory(INTEGRAL_SAVE_OUTBOX_DIR) != 0) {
        if (pending_out) *pending_out = 1;
        return 0;
    }
    char manifest_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(manifest_path, sizeof(manifest_path), "%s/outbox.tsv", INTEGRAL_SAVE_OUTBOX_DIR);
    if (make_private_runtime_file(manifest_path) != 0 && errno != ENOENT) {
        if (pending_out) *pending_out = 1;
        return 0;
    }
    FILE *manifest = fopen(manifest_path, "rb");

    unsigned replayed = 0;
    unsigned pending = 0;
    char inflight[INTEGRAL_CONFIG_PATH_MAX];
    if (inflight_path(server, account, only_save_id, inflight, sizeof(inflight)) && local_file_exists(inflight)) {
        SaveInflight *entry = calloc(1, sizeof(*entry));
        LocalSyncSlot slot = {0};
        bool valid = entry && inflight_read(inflight, entry) &&
                     !strcmp(entry->server, server) && !strcmp(entry->account, account) &&
                     !strcmp(entry->save_id, only_save_id) && managed_runtime_file_path(entry->source);
        if (valid) {
            snprintf(slot.account, sizeof(slot.account), "%s", account);
            snprintf(slot.save_id, sizeof(slot.save_id), "%s", only_save_id);
            snprintf(slot.save_path, sizeof(slot.save_path), "%s", entry->source);
            (void)process_save_inflight(server, token, NULL, 0, &slot, false);
        }
        free(entry);
        if (local_file_exists(inflight)) {
            if (manifest) fclose(manifest);
            if (pending_out) *pending_out = 1;
            return 0;
        }
        replayed++;
        if (valid) (void)remove(slot.save_path);
    }
    char line[4096];
    if (manifest) {
        while (fgets(line, sizeof(line), manifest)) {
            replay_save_upload_outbox_line(line, NULL, server, token, only_save_id, account, &replayed, &pending);
        }
        fclose(manifest);
    }

    DIR *dir = opendir(INTEGRAL_SAVE_OUTBOX_DIR);
    if (dir) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            size_t name_len = strlen(entry->d_name);
            if (name_len <= 8 || strcmp(entry->d_name + name_len - 8, ".pending") != 0) {
                continue;
            }
            char record_path[INTEGRAL_CONFIG_PATH_MAX];
            snprintf(record_path, sizeof(record_path), "%s/%s", INTEGRAL_SAVE_OUTBOX_DIR, entry->d_name);
            if (make_private_runtime_file(record_path) != 0) {
                pending++;
                continue;
            }
            unsigned char *record_data = NULL;
            size_t record_size = 0;
            if (read_binary_file_alloc(record_path, &record_data, &record_size, sizeof(line) - 1) != 0) {
                pending++;
                continue;
            }
            memcpy(line, record_data, record_size);
            line[record_size] = '\0';
            free(record_data);
            replay_save_upload_outbox_line(line,
                                           record_path,
                                           server,
                                           token,
                                           only_save_id,
                                           account,
                                           &replayed,
                                           &pending);
        }
        closedir(dir);
    }
    if (pending_out) {
        *pending_out = pending;
    }
    return replayed;
}

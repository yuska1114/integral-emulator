/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_SAVE_OUTBOX_H
#define INTEGRAL_CLIENT_SAVE_OUTBOX_H
#include "client_save_sync.h"
#define INTEGRAL_SAVE_OUTBOX_DIR "runtime/save-outbox"
bool write_save_upload_outbox(const char *server, const LocalSyncSlot *,
    const unsigned char *, size_t, const char *hash, const char *error,
    const char *game_session, long long fencing_token, const char *request_id);
bool write_save_recovery_pointer(const char *server, const LocalSyncSlot *,
    const char *path, const char *error, const char *game_session, long long fencing_token);
unsigned replay_save_upload_outbox(const char *server, const char *token,
    const char *save_id, unsigned *pending, const char *account);
bool complete_replayed_outbox_entry(const char *save_path, const char *record_path,
    const char *completion_path, const char *completion, size_t completion_size);
bool extract_tsv_field(const char *, const char *, char *, size_t);
bool save_upload_outbox_pending(const char *server, const char *save_id,
    const char *account, char *error, size_t error_size);
/* One atomic, save-specific request envelope; no credentials are persisted. */
bool process_save_inflight(const char *server, const char *token, const char *game,
    long long fence, LocalSyncSlot *slot, bool final);
#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_FILE_IO_H
#define INTEGRAL_CLIENT_FILE_IO_H
#include <stddef.h>
#include <stdbool.h>
bool runtime_session_id_is_path_safe(const char *session_id);
bool format_runtime_session_path(char *out, size_t out_size, const char *root,
                                 const char *session_id, const char *relative_path);
int ensure_directory(const char *path);
int create_export_directory(const char *root, const char *stamp, char *out, size_t capacity);
void export_save_filename(const char *name, unsigned slot_index, char *out, size_t capacity);
int ensure_private_runtime_directory(const char *path);
#define INTEGRAL_MAX_SAVE_BYTES (128u * 1024u)
int sha256_file_hex(const char *, char *, size_t);
int sha1_file_hex(const char *, char *, size_t);
int read_binary_file(const char *, unsigned char *, size_t, size_t *);
bool local_file_exists(const char *);
int write_binary_file(const char *, const unsigned char *, size_t);
int write_private_runtime_file(const char *, const unsigned char *, size_t);
int copy_binary_file_limited(const char *, const char *, size_t);
int make_private_runtime_file(const char *);
int append_flushed_text_line(const char *, const char *);
int read_binary_file_alloc(const char *, unsigned char **, size_t *, size_t);
int atomic_replace_binary_file(const char *, const unsigned char *, size_t);
void make_safe_outbox_token(const char *, char *, size_t);
void make_safe_n64_runtime_save_name(const char *, char *, size_t);
#endif

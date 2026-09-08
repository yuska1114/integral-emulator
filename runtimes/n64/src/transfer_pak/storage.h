/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_TRANSFER_PAK_STORAGE_H
#define INTEGRAL_N64_RUNTIME_TRANSFER_PAK_STORAGE_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define TRANSFER_PAK_STORAGE_PATH_CAPACITY 1024u
#define TRANSFER_PAK_STORAGE_MAX_FILE_SIZE (64u * 1024u * 1024u)

int transfer_pak_storage_ensure_directory(const char *path);
int transfer_pak_storage_slot_path(const char *directory, uint32_t slot,
                                   const char *extension, char *output,
                                   size_t capacity);
int transfer_pak_storage_file_size(const char *path, uint32_t *size);
int transfer_pak_storage_copy(FILE *input, FILE *output, uint32_t size);
int transfer_pak_storage_replace(const char *temporary,
                                 const char *destination);

#endif

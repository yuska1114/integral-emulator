/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_TRANSFER_PAK_SAVE_STAGE_H
#define INTEGRAL_N64_RUNTIME_TRANSFER_PAK_SAVE_STAGE_H

#include "transfer_pak/storage.h"

#include <stddef.h>
#include <stdint.h>

typedef struct TransferPakSaveStage {
    int active;
    uint32_t ram_size;
    uint32_t original_size;
    uint32_t merged_size;
    char original_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    char working_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    char rtc_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
} TransferPakSaveStage;

uint32_t transfer_pak_gb_ram_size(const char *rom_path);
int transfer_pak_save_stage_prepare(TransferPakSaveStage *stage, const char *rom_path,
                             const char *save_path, char *mupen_path,
                             size_t mupen_path_capacity);
int transfer_pak_save_stage_finalize(TransferPakSaveStage *stage);
void transfer_pak_save_stage_discard(TransferPakSaveStage *stage);

#endif


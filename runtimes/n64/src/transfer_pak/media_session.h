/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_TRANSFER_PAK_MEDIA_SESSION_H
#define INTEGRAL_N64_RUNTIME_TRANSFER_PAK_MEDIA_SESSION_H

#include "transfer_pak/save_stage.h"

#include <stddef.h>

typedef struct TransferPakMediaSession {
    int ready[4];
    char rom_paths[4][TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    char ram_paths[4][TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    TransferPakSaveStage saves[4];
} TransferPakMediaSession;

int transfer_pak_media_session_prepare(TransferPakMediaSession *session,
                                const char *storage);
int transfer_pak_media_session_prepare_mask(TransferPakMediaSession *session,
                                     const char *storage,
                                     unsigned int slot_mask);
int transfer_pak_media_session_finalize(TransferPakMediaSession *session);
void transfer_pak_media_session_discard(TransferPakMediaSession *session);
int transfer_pak_media_session_ready(const TransferPakMediaSession *session,
                              int controller);
char *transfer_pak_media_session_get_rom(void *context, int controller);
char *transfer_pak_media_session_get_ram(void *context, int controller);

#endif

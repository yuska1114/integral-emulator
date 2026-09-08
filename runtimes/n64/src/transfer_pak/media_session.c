/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "transfer_pak/media_session.h"

#include "transfer_pak/storage.h"

#include <stdlib.h>
#include <string.h>

static char *duplicate_path(const char *path)
{
    size_t length = strlen(path);
    char *copy = malloc(length + 1u);
    if (copy != NULL) {
        memcpy(copy, path, length + 1u);
    }
    return copy;
}

int transfer_pak_media_session_prepare(TransferPakMediaSession *session,
                                const char *storage)
{
    return transfer_pak_media_session_prepare_mask(session, storage, 0x0fu);
}

int transfer_pak_media_session_prepare_mask(TransferPakMediaSession *session,
                                     const char *storage,
                                     unsigned int slot_mask)
{
    int controller;
    if (session == NULL || storage == NULL) {
        return -1;
    }
    memset(session, 0, sizeof(*session));
    for (controller = 0; controller < 4; ++controller) {
        uint32_t file_size;
        uint32_t slot = (uint32_t)controller + 1u;
        char save_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
        if ((slot_mask & (1u << (unsigned int)controller)) == 0u) {
            continue;
        }
        if (transfer_pak_storage_slot_path(storage, slot, "gbc",
                                    session->rom_paths[controller],
                                    sizeof(session->rom_paths[controller])) != 0 ||
            transfer_pak_storage_slot_path(storage, slot, "sav", save_path,
                                    sizeof(save_path)) != 0) {
            transfer_pak_media_session_discard(session);
            return -1;
        }
        if (transfer_pak_storage_file_size(session->rom_paths[controller],
                                    &file_size) != 0 ||
            file_size == 0u ||
            transfer_pak_storage_file_size(save_path, &file_size) != 0) {
            session->rom_paths[controller][0] = '\0';
            continue;
        }
        if (transfer_pak_save_stage_prepare(&session->saves[controller],
                                     session->rom_paths[controller], save_path,
                                     session->ram_paths[controller],
                                     sizeof(session->ram_paths[controller])) != 0) {
            transfer_pak_media_session_discard(session);
            return -1;
        }
        session->ready[controller] = 1;
    }
    return 0;
}

int transfer_pak_media_session_finalize(TransferPakMediaSession *session)
{
    int controller;
    int result = 0;
    if (session == NULL) {
        return -1;
    }
    for (controller = 0; controller < 4; ++controller) {
        if (transfer_pak_save_stage_finalize(&session->saves[controller]) != 0) {
            result = -1;
        }
        session->ready[controller] = 0;
    }
    return result;
}

void transfer_pak_media_session_discard(TransferPakMediaSession *session)
{
    int controller;
    if (session == NULL) {
        return;
    }
    for (controller = 0; controller < 4; ++controller) {
        transfer_pak_save_stage_discard(&session->saves[controller]);
    }
    memset(session, 0, sizeof(*session));
}

int transfer_pak_media_session_ready(const TransferPakMediaSession *session,
                              int controller)
{
    return session != NULL && controller >= 0 && controller < 4
               ? session->ready[controller]
               : 0;
}

char *transfer_pak_media_session_get_rom(void *context, int controller)
{
    TransferPakMediaSession *session = context;
    return transfer_pak_media_session_ready(session, controller)
               ? duplicate_path(session->rom_paths[controller])
               : NULL;
}

char *transfer_pak_media_session_get_ram(void *context, int controller)
{
    TransferPakMediaSession *session = context;
    return transfer_pak_media_session_ready(session, controller)
               ? duplicate_path(session->ram_paths[controller])
               : NULL;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_TRANSFER_MEMORY_SESSION_H
#define INTEGRAL_TRANSFER_MEMORY_SESSION_H
#include "transfer_pak/media_session.h"
#include "../../../common/n64_transfer_memory.h"
#include "../../../common/transfer_sav_ipc.h"
typedef struct TransferPakMemorySession {
    TransferSavPair pair;
    IntegralTransferMemory slots[2];
} TransferPakMemorySession;
int transfer_pak_memory_prepare(TransferPakMemorySession *, TransferPakMediaSession *,
                                const char *storage, const char *session, int fd);
void transfer_pak_memory_clear(TransferPakMemorySession *);
#endif

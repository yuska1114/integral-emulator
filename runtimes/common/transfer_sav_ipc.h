/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_TRANSFER_SAV_IPC_H
#define INTEGRAL_TRANSFER_SAV_IPC_H
#include <stddef.h>
#include <stdint.h>
#include "../gb/src/server/secure_memory.h"
#define TRANSFER_SAV_MAX (128u * 1024u + 48u)
typedef struct TransferSavPair {
    IntegralGBRuntimeSecureBuffer saves[2];
    size_t lengths[2];
    uint32_t revisions[2];
} TransferSavPair;
/* ROOM only: no initial-save or RTC synthesis. Header must contain 0x150 bytes. */
int transfer_sav_ready(const unsigned char *header, const unsigned char *save, size_t size);
void transfer_sav_pair_clear(TransferSavPair *);
/* read/write callbacks return bytes transferred, zero for EOF, -1 for failure. */
typedef int (*TransferSavIO)(void *, void *, size_t);
int transfer_sav_send(TransferSavIO, void *, const char *session, const TransferSavPair *);
int transfer_sav_receive(TransferSavIO, void *, const char *session, TransferSavPair *);
int transfer_sav_pipe(int fd[2]);
void transfer_sav_close(int fd);
int transfer_sav_pipe_read(void *, void *, size_t);
int transfer_sav_pipe_write(void *, void *, size_t);
#endif

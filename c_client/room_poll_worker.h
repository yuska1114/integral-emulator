/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_ROOM_POLL_WORKER_H
#define INTEGRAL_ROOM_POLL_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "http_client.h"

typedef struct IntegralRoomPollWorker IntegralRoomPollWorker;

typedef struct IntegralRoomPollResult {
    IntegralApiRoom room;
    char server[128];
    char token[160];
    char error[160];
    unsigned room_number;
    uint32_t request_id;
    uint32_t elapsed_ms;
    bool heartbeat_attempted;
    bool heartbeat_succeeded;
    IntegralApiHeartbeatStatus heartbeat_status;
    int room_result;
    bool has_room;
} IntegralRoomPollResult;

IntegralRoomPollWorker *integral_room_poll_worker_create(void);
void integral_room_poll_worker_destroy(IntegralRoomPollWorker *worker);

/* Returns 0 when started, 1 when a request is already active, and -1 on error. */
int integral_room_poll_worker_start(IntegralRoomPollWorker *worker,
                                const char *server,
                                const char *token,
                                unsigned room_number,
                                uint32_t request_id,
                                bool send_heartbeat);

/* Returns 1 with a completed result, 0 while idle/running, and -1 on error. */
int integral_room_poll_worker_take(IntegralRoomPollWorker *worker,
                               IntegralRoomPollResult *result);

bool integral_room_poll_worker_is_busy(IntegralRoomPollWorker *worker);

bool integral_room_heartbeat_attempt_due(uint32_t last_attempt_ticks,
                                          bool has_attempted,
                                          uint32_t now_ticks,
                                          uint32_t interval_ms);

#endif

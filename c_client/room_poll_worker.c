/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "room_poll_worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

struct IntegralRoomPollWorker {
    SDL_mutex *mutex;
    SDL_Thread *thread;
    bool done;
    IntegralRoomPollResult result;
};

static uint64_t performance_us(void)
{
    uint64_t frequency = SDL_GetPerformanceFrequency();
    if (!frequency) return (uint64_t)SDL_GetTicks() * 1000u;
    return SDL_GetPerformanceCounter() * 1000000u / frequency;
}

static void copy_text(char *destination, size_t size, const char *source)
{
    if (!destination || !size) return;
    snprintf(destination, size, "%s", source ? source : "");
}

static int room_poll_thread(void *opaque)
{
    IntegralRoomPollWorker *worker = opaque;
    IntegralRoomPollResult *result = &worker->result;
    uint64_t started_us = performance_us();
    result->error[0] = '\0';
    if (result->heartbeat_attempted) {
        char heartbeat_error[160];
        result->heartbeat_succeeded =
            integral_api_room_heartbeat_status(result->server,
                                    result->token,
                                    &result->heartbeat_status,
                                    heartbeat_error,
                                    sizeof(heartbeat_error)) == 0;
        if (!result->heartbeat_succeeded) {
            copy_text(result->error, sizeof(result->error), heartbeat_error);
        }
    }
    char room_error[160];
    IntegralApiRoom current;
    int has_room = 0;
    result->room_result = integral_api_get_current_room(result->server,
                                                    result->token,
                                                    &current,
                                                    &has_room,
                                                    room_error,
                                                    sizeof(room_error));
    memset(&result->room, 0, sizeof(result->room));
    result->has_room = false;
    if (result->room_result == 0 && has_room && current.room_number >= 1 &&
        current.room_number <= INTEGRAL_API_ROOMS) {
        result->room = current;
        result->has_room = true;
    }
    if (result->room_result != 0) {
        copy_text(result->error, sizeof(result->error), room_error);
    }
    uint64_t elapsed_us = performance_us() - started_us;
    result->elapsed_ms = elapsed_us / 1000u > UINT32_MAX
                             ? UINT32_MAX
                             : (uint32_t)(elapsed_us / 1000u);
    SDL_LockMutex(worker->mutex);
    worker->done = true;
    SDL_UnlockMutex(worker->mutex);
    return 0;
}

IntegralRoomPollWorker *integral_room_poll_worker_create(void)
{
    IntegralRoomPollWorker *worker = calloc(1, sizeof(*worker));
    if (!worker) return NULL;
    worker->mutex = SDL_CreateMutex();
    if (!worker->mutex) {
        free(worker);
        return NULL;
    }
    return worker;
}

void integral_room_poll_worker_destroy(IntegralRoomPollWorker *worker)
{
    if (!worker) return;
    SDL_LockMutex(worker->mutex);
    SDL_Thread *thread = worker->thread;
    SDL_UnlockMutex(worker->mutex);
    if (thread) SDL_WaitThread(thread, NULL);
    SDL_DestroyMutex(worker->mutex);
    free(worker);
}

int integral_room_poll_worker_start(IntegralRoomPollWorker *worker,
                                const char *server,
                                const char *token,
                                unsigned room_number,
                                uint32_t request_id,
                                bool send_heartbeat)
{
    if (!worker || !server || !server[0] || !token || !token[0]) return -1;
    SDL_LockMutex(worker->mutex);
    if (worker->thread) {
        SDL_UnlockMutex(worker->mutex);
        return 1;
    }
    memset(&worker->result, 0, sizeof(worker->result));
    copy_text(worker->result.server, sizeof(worker->result.server), server);
    copy_text(worker->result.token, sizeof(worker->result.token), token);
    worker->result.room_number = room_number;
    worker->result.request_id = request_id;
    worker->result.heartbeat_attempted = send_heartbeat;
    worker->done = false;
    worker->thread = SDL_CreateThread(room_poll_thread, "integral-room-poll", worker);
    if (!worker->thread) {
        SDL_UnlockMutex(worker->mutex);
        return -1;
    }
    SDL_UnlockMutex(worker->mutex);
    return 0;
}

int integral_room_poll_worker_take(IntegralRoomPollWorker *worker,
                               IntegralRoomPollResult *result)
{
    if (!worker || !result) return -1;
    SDL_LockMutex(worker->mutex);
    if (!worker->thread || !worker->done) {
        SDL_UnlockMutex(worker->mutex);
        return 0;
    }
    SDL_Thread *thread = worker->thread;
    SDL_UnlockMutex(worker->mutex);
    SDL_WaitThread(thread, NULL);
    SDL_LockMutex(worker->mutex);
    *result = worker->result;
    worker->thread = NULL;
    worker->done = false;
    SDL_UnlockMutex(worker->mutex);
    return 1;
}

bool integral_room_poll_worker_is_busy(IntegralRoomPollWorker *worker)
{
    if (!worker) return false;
    SDL_LockMutex(worker->mutex);
    bool busy = worker->thread != NULL;
    SDL_UnlockMutex(worker->mutex);
    return busy;
}

bool integral_room_heartbeat_attempt_due(uint32_t last_attempt_ticks,
                                          bool has_attempted,
                                          uint32_t now_ticks,
                                          uint32_t interval_ms)
{
    return !has_attempted || now_ticks - last_attempt_ticks >= interval_ms;
}

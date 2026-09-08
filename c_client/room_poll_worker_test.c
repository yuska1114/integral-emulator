/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "room_poll_worker.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include <SDL.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    assert(SDL_Init(0) == 0);
    assert(integral_room_heartbeat_attempt_due(0u, false, 0u, 5000u));
    assert(!integral_room_heartbeat_attempt_due(0u, true, 4999u, 5000u));
    assert(integral_room_heartbeat_attempt_due(0u, true, 5000u, 5000u));
    assert(!integral_room_heartbeat_attempt_due(5000u, true, 9999u, 5000u));
    assert(integral_room_heartbeat_attempt_due(5000u, true, 10000u, 5000u));
    IntegralRoomPollWorker *worker = integral_room_poll_worker_create();
    assert(worker != NULL);
    assert(integral_room_poll_worker_start(worker,
                                       "http://127.0.0.1:1",
                                       "smoke-token",
                                       65,
                                       7,
                                       true) == 0);
    assert(integral_room_poll_worker_is_busy(worker));
    IntegralRoomPollResult result;
    int taken = 0;
    Uint32 deadline = SDL_GetTicks() + 5000u;
    while (taken == 0 && (Sint32)(SDL_GetTicks() - deadline) < 0) {
        taken = integral_room_poll_worker_take(worker, &result);
        SDL_Delay(1);
    }
    assert(taken == 1);
    assert(result.room_number == 65);
    assert(result.request_id == 7);
    assert(strcmp(result.server, "http://127.0.0.1:1") == 0);
    assert(result.heartbeat_attempted);
    assert(!result.heartbeat_succeeded);
    assert(result.room_result != 0);
    assert(!integral_room_poll_worker_is_busy(worker));
    integral_room_poll_worker_destroy(worker);
    SDL_Quit();
    puts("room poll worker test passed");
    return 0;
}

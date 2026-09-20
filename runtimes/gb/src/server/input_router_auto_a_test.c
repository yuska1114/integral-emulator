/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_router.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "joypad.h"

#define CHECK(value)                                                          \
    do {                                                                      \
        if (!(value)) {                                                       \
            fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static max_align_t gb_storage[2];
static bool a_pressed[2];
static unsigned a_writes[2];
static IntegralGBRuntimeSlot slots[2];
static IntegralGBRuntimeInputRouter router;

void GB_set_key_state(GB_gameboy_t *gb, GB_key_t key, bool pressed)
{
    unsigned index;
    if (gb == (GB_gameboy_t *)&gb_storage[0]) {
        index = 0;
    }
    else if (gb == (GB_gameboy_t *)&gb_storage[1]) {
        index = 1;
    }
    else {
        abort();
    }
    if (key == GB_KEY_A) {
        a_pressed[index] = pressed;
        a_writes[index]++;
    }
}

static void reset_router(void)
{
    memset(slots, 0, sizeof(slots));
    memset(a_pressed, 0, sizeof(a_pressed));
    memset(a_writes, 0, sizeof(a_writes));
    for (unsigned i = 0; i < 2; i++) {
        slots[i].gb = (GB_gameboy_t *)&gb_storage[i];
        slots[i].initialized = true;
    }
    integral_gb_runtime_input_router_init(&router, &slots[0], &slots[1]);
}

static bool send_key(Uint32 type, SDL_Keycode key)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.keysym.sym = key;
    return integral_gb_runtime_input_router_handle_event(&router, &event);
}

static int test_normal_press_release_and_hold(void)
{
    reset_router();
    for (unsigned frame = 0; frame < 4; frame++) {
        integral_gb_runtime_input_router_apply_auto_a(&router, frame, 0, 2);
    }
    CHECK(a_writes[0] == 0 && a_writes[1] == 0);

    CHECK(send_key(SDL_KEYDOWN, SDLK_z));
    CHECK(a_pressed[0] && !a_pressed[1]);
    unsigned slot1_writes = a_writes[0];
    for (unsigned frame = 4; frame < 12; frame++) {
        integral_gb_runtime_input_router_apply_auto_a(&router, frame, 0, 2);
        integral_gb_runtime_input_router_update_turbo(&router);
        CHECK(a_pressed[0] && a_writes[0] == slot1_writes);
    }
    CHECK(send_key(SDL_KEYUP, SDLK_z));
    CHECK(!a_pressed[0] && a_writes[0] == slot1_writes + 1);

    CHECK(send_key(SDL_KEYDOWN, SDLK_g));
    CHECK(a_pressed[1]);
    unsigned slot2_writes = a_writes[1];
    for (unsigned frame = 12; frame < 16; frame++) {
        integral_gb_runtime_input_router_apply_auto_a(&router, frame, 0, 0);
        CHECK(a_pressed[1] && a_writes[1] == slot2_writes);
    }
    CHECK(send_key(SDL_KEYUP, SDLK_g));
    CHECK(!a_pressed[1] && a_writes[1] == slot2_writes + 1);
    return 0;
}

static int test_auto_a_pulse_and_restoration(void)
{
    reset_router();
    integral_gb_runtime_input_router_apply_auto_a(&router, 0, 3, 2);
    CHECK(a_pressed[0] && a_pressed[1]);
    integral_gb_runtime_input_router_apply_auto_a(&router, 1, 3, 2);
    CHECK(!a_pressed[0] && !a_pressed[1]);
    integral_gb_runtime_input_router_apply_auto_a(&router, 2, 3, 2);
    CHECK(a_pressed[0] && a_pressed[1]);
    integral_gb_runtime_input_router_apply_auto_a(&router, 3, 3, 2);
    CHECK(!a_pressed[0] && !a_pressed[1]);
    unsigned slot1_writes = a_writes[0];
    unsigned slot2_writes = a_writes[1];
    integral_gb_runtime_input_router_apply_auto_a(&router, 4, 3, 2);
    CHECK(a_writes[0] == slot1_writes && a_writes[1] == slot2_writes);

    reset_router();
    CHECK(send_key(SDL_KEYDOWN, SDLK_z));
    CHECK(send_key(SDL_KEYDOWN, SDLK_g));
    integral_gb_runtime_input_router_apply_auto_a(&router, 0, 2, 2);
    integral_gb_runtime_input_router_apply_auto_a(&router, 1, 2, 2);
    CHECK(!a_pressed[0] && !a_pressed[1]);
    integral_gb_runtime_input_router_apply_auto_a(&router, 2, 2, 2);
    CHECK(a_pressed[0] && a_pressed[1]);
    CHECK(send_key(SDL_KEYUP, SDLK_z));
    CHECK(send_key(SDL_KEYUP, SDLK_g));
    CHECK(!a_pressed[0] && !a_pressed[1]);
    return 0;
}

static int test_turbo_a_and_normal_hold(void)
{
    reset_router();
    CHECK(send_key(SDL_KEYDOWN, SDLK_b));
    CHECK(send_key(SDL_KEYDOWN, SDLK_z));
    CHECK(send_key(SDL_KEYUP, SDLK_z));
    CHECK(send_key(SDL_KEYUP, SDLK_b));
    CHECK(router.turbo_hold_active && a_pressed[0]);

    unsigned writes = a_writes[0];
    for (unsigned frame = 0; frame < 8; frame++) {
        integral_gb_runtime_input_router_apply_auto_a(&router, frame, 0, 2);
    }
    CHECK(a_pressed[0] && a_writes[0] == writes);
    router.turbo_last_tick_ms = SDL_GetTicks() - 70u;
    integral_gb_runtime_input_router_update_turbo(&router);
    CHECK(!a_pressed[0]);
    integral_gb_runtime_input_router_apply_auto_a(&router, 0, 1, 0);
    CHECK(a_pressed[0]);
    integral_gb_runtime_input_router_apply_auto_a(&router, 1, 1, 0);
    CHECK(!a_pressed[0]);
    writes = a_writes[0];
    integral_gb_runtime_input_router_apply_auto_a(&router, 8, 0, 2);
    CHECK(!a_pressed[0] && a_writes[0] == writes);
    router.turbo_last_tick_ms = SDL_GetTicks() - 70u;
    integral_gb_runtime_input_router_update_turbo(&router);
    CHECK(a_pressed[0]);

    CHECK(send_key(SDL_KEYDOWN, SDLK_b));
    CHECK(send_key(SDL_KEYUP, SDLK_b));
    CHECK(!router.turbo_hold_active && !a_pressed[0]);
    CHECK(send_key(SDL_KEYDOWN, SDLK_z));
    CHECK(a_pressed[0]);
    writes = a_writes[0];
    integral_gb_runtime_input_router_apply_auto_a(&router, 9, 0, 2);
    CHECK(a_pressed[0] && a_writes[0] == writes);
    CHECK(send_key(SDL_KEYUP, SDLK_z));
    CHECK(!a_pressed[0]);
    return 0;
}

static int test_local_utils(void)
{
    reset_router();
    integral_gb_runtime_input_router_set_keymaps(&router, NULL, NULL,
        SDLK_z, SDLK_p, SDLK_ESCAPE, SDLK_g, SDLK_r);
    integral_gb_runtime_input_router_disable_speed_controls(&router);
    CHECK(send_key(SDL_KEYDOWN, SDLK_z));
    CHECK(a_pressed[0]);
    CHECK(send_key(SDL_KEYDOWN, SDLK_g));
    CHECK(a_pressed[1]);
    CHECK(!router.turbo_hold_active);
    CHECK(send_key(SDL_KEYDOWN, SDLK_p));
    CHECK(integral_gb_runtime_input_router_take_screenshot_request(&router));
    CHECK(!integral_gb_runtime_input_router_take_screenshot_request(&router));
    CHECK(send_key(SDL_KEYDOWN, SDLK_r));
    CHECK(integral_gb_runtime_input_router_take_reset_request(&router));
    CHECK(!integral_gb_runtime_input_router_take_reset_request(&router));
    return 0;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL timer init failed: %s\n", SDL_GetError());
        return 1;
    }
    int result = test_normal_press_release_and_hold();
    if (result == 0) result = test_auto_a_pulse_and_restoration();
    if (result == 0) result = test_turbo_a_and_normal_hold();
    if (result == 0) result = test_local_utils();
    SDL_Quit();
    if (result == 0) {
        puts("GB LOCAL A input/auto-A/Turbo test ok");
    }
    return result;
}

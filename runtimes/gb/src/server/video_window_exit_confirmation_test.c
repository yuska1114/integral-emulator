/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_router.h"
#include "video_window.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

static void push_key(Uint32 type, SDL_Keycode key)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.type = type;
    event.key.keysym.sym = key;
    CHECK(SDL_PushEvent(&event) == 1);
}

static IntegralGBRuntimeVideoWindowPollResult poll_window(
    IntegralGBRuntimeVideoWindow *window, IntegralGBRuntimeInputRouter *input)
{
    return integral_gb_runtime_video_window_poll(window, input, NULL, NULL);
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
    CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    IntegralGBRuntimeVideoWindow *window = NULL;
    CHECK(integral_gb_runtime_video_window_open_titled_unthrottled(
              &window, 1, 1, "exit confirmation test") == 0);
    IntegralGBRuntimeInputRouter input;
    integral_gb_runtime_input_router_init(&input, NULL, NULL);
    integral_gb_runtime_input_router_disable_speed_controls(&input);

    push_key(SDL_KEYDOWN, integral_gb_runtime_key_config_fast_default());
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(integral_gb_runtime_input_router_speed_multiplier(&input) == 1u);
    CHECK(!integral_gb_runtime_input_router_take_speed_multiplier_changed(&input));
    push_key(SDL_KEYDOWN, integral_gb_runtime_key_config_turbo_hold_default());
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(!input.turbo_hold_active);
    CHECK(!input.turbo_capture_active);

    push_key(SDL_KEYDOWN, SDLK_z);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(input.slot1_normal_buttons != 0u);

    push_key(SDL_KEYDOWN, SDLK_ESCAPE);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(input.slot1_normal_buttons == 0u);
    push_key(SDL_KEYDOWN, SDLK_RETURN);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);

    SDL_Event close_event;
    memset(&close_event, 0, sizeof(close_event));
    close_event.type = SDL_WINDOWEVENT;
    close_event.window.event = SDL_WINDOWEVENT_CLOSE;
    CHECK(SDL_PushEvent(&close_event) == 1);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    push_key(SDL_KEYDOWN, SDLK_ESCAPE);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);

    SDL_Event quit_event;
    memset(&quit_event, 0, sizeof(quit_event));
    quit_event.type = SDL_QUIT;
    CHECK(SDL_PushEvent(&quit_event) == 1);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    push_key(SDL_KEYDOWN, SDLK_RIGHT);
    push_key(SDL_KEYDOWN, SDLK_RETURN);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_RETURN_MENU);

    integral_gb_runtime_video_window_close(window);
    printf("PASS video window exit confirmation checks=%u\n", checks);
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slot.h"
#include "video_window.h"
#include <assert.h>
#undef main
static void counted_reset(IntegralGBRuntimeSlot *slot);
static IntegralGBRuntimeVideoWindowPollResult injected_poll(
    IntegralGBRuntimeVideoWindow *, IntegralGBRuntimeInputRouter *,
    const IntegralGBRuntimeSlot *, const IntegralGBRuntimeSlot *);
#define main server_program_main
#define integral_gb_runtime_slot_reset counted_reset
#define integral_gb_runtime_video_window_poll injected_poll
#include "main.c"
#undef integral_gb_runtime_slot_reset
#undef integral_gb_runtime_video_window_poll
#undef main
#ifdef _WIN32
#define main SDL_main
#endif

static const IntegralGBRuntimeSlot *reset_slots[2];
static unsigned reset_count, polls;
static void counted_reset(IntegralGBRuntimeSlot *slot)
{
    assert(reset_count < 2);
    reset_slots[reset_count++] = slot;
    integral_gb_runtime_slot_reset(slot);
}

static IntegralGBRuntimeVideoWindowPollResult injected_poll(
    IntegralGBRuntimeVideoWindow *window, IntegralGBRuntimeInputRouter *input,
    const IntegralGBRuntimeSlot *a, const IntegralGBRuntimeSlot *b)
{
    IntegralGBRuntimeVideoWindowPollResult result =
        integral_gb_runtime_video_window_poll(window, input, a, b);
    if (polls++ == 0) {
        SDL_Event event = {.type = SDL_KEYDOWN};
        event.key.keysym.sym = input->reset_key;
        assert(integral_gb_runtime_input_router_handle_event(input, &event));
        event.key.keysym.sym = input->screenshot_key;
        assert(integral_gb_runtime_input_router_handle_event(input, &event));
    }
    return result;
}

int main(int argc, char **argv)
{
    int result = server_program_main(argc, argv);
    assert(result == 0 && reset_count == 2 && reset_slots[0] != reset_slots[1]);
    puts("SERVER2 real main-loop reset of both slots and capture PASS");
    return result;
}

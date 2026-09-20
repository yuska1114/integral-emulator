/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_INPUT_ROUTER_H
#define INTEGRAL_GB_RUNTIME_INPUT_ROUTER_H

#include <SDL.h>
#include <stdbool.h>

#include "key_config.h"
#include "slot.h"

typedef struct IntegralGBRuntimeInputRouter {
    IntegralGBRuntimeSlot *slot1;
    IntegralGBRuntimeSlot *slot2;
    IntegralGBRuntimeKeyConfig slot1_keys;
    IntegralGBRuntimeKeyConfig slot2_keys;
    SDL_Keycode fast_key;
    SDL_Keycode screenshot_key;
    SDL_Keycode escape_key;
    SDL_Keycode turbo_hold_key;
    SDL_Keycode reset_key;
    uint8_t slot1_normal_buttons;
    uint8_t slot2_normal_buttons;
    uint8_t slot1_turbo_buttons;
    uint8_t slot2_turbo_buttons;
    uint8_t slot1_turbo_applied_buttons;
    uint8_t slot2_turbo_applied_buttons;
    uint8_t slot1_applied_buttons;
    uint8_t slot2_applied_buttons;
    uint32_t turbo_last_tick_ms;
    unsigned speed_multiplier;
    bool speed_multiplier_changed;
    bool screenshot_requested;
    bool escape_requested;
    bool reset_requested;
    bool screenshot_held, escape_held, reset_held;
    bool fast_held, turbo_key_held;
    bool turbo_hold_active;
    bool turbo_capture_active;
    bool turbo_phase_on;
    bool turbo_macro_control;
    bool turbo_macro_toggle_requested;
    bool game_input_blocked;
} IntegralGBRuntimeInputRouter;

void integral_gb_runtime_input_router_init(IntegralGBRuntimeInputRouter *router,
                                  IntegralGBRuntimeSlot *slot1,
                                  IntegralGBRuntimeSlot *slot2);
void integral_gb_runtime_input_router_set_keymaps(IntegralGBRuntimeInputRouter *router,
                                         const IntegralGBRuntimeKeyConfig *slot1_keys,
                                         const IntegralGBRuntimeKeyConfig *slot2_keys,
                                         SDL_Keycode fast_key,
                                         SDL_Keycode screenshot_key,
                                         SDL_Keycode escape_key,
                                         SDL_Keycode turbo_hold_key,
                                         SDL_Keycode reset_key);
void integral_gb_runtime_input_router_set_speed_multiplier(IntegralGBRuntimeInputRouter *router, unsigned speed_multiplier);
void integral_gb_runtime_input_router_disable_speed_controls(IntegralGBRuntimeInputRouter *router);
bool integral_gb_runtime_input_router_handle_event(IntegralGBRuntimeInputRouter *router, const SDL_Event *event);
void integral_gb_runtime_input_router_update_turbo(IntegralGBRuntimeInputRouter *router);
void integral_gb_runtime_input_router_apply_auto_a(const IntegralGBRuntimeInputRouter *router,
                                                   unsigned frame,
                                                   unsigned auto_a_frames,
                                                   unsigned auto_a_pulse);
void integral_gb_runtime_input_router_release_all(IntegralGBRuntimeInputRouter *router);
void integral_gb_runtime_input_router_set_turbo_macro_control(IntegralGBRuntimeInputRouter *router, bool enabled);
bool integral_gb_runtime_input_router_take_turbo_macro_toggle(IntegralGBRuntimeInputRouter *router);
void integral_gb_runtime_input_router_set_game_input_blocked(IntegralGBRuntimeInputRouter *router, bool blocked);
unsigned integral_gb_runtime_input_router_speed_multiplier(const IntegralGBRuntimeInputRouter *router);
bool integral_gb_runtime_input_router_take_speed_multiplier_changed(IntegralGBRuntimeInputRouter *router);
bool integral_gb_runtime_input_router_describe_turbo(const IntegralGBRuntimeInputRouter *router,
                                           char *dest,
                                           size_t dest_size);
bool integral_gb_runtime_input_router_take_screenshot_request(IntegralGBRuntimeInputRouter *router);
bool integral_gb_runtime_input_router_take_escape_request(IntegralGBRuntimeInputRouter *router);
bool integral_gb_runtime_input_router_take_reset_request(IntegralGBRuntimeInputRouter *router);
uint8_t integral_gb_runtime_input_router_slot1_buttons(
    const IntegralGBRuntimeInputRouter *router);
void integral_gb_runtime_input_router_print_keymap(const IntegralGBRuntimeInputRouter *router);

#endif

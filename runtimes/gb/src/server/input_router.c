/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_router.h"

#include <stdio.h>
#include <string.h>

#include "joypad.h"
#include "protocol.h"

static void apply_router_buttons(IntegralGBRuntimeInputRouter *router);

void integral_gb_runtime_input_router_init(IntegralGBRuntimeInputRouter *router,
                                  IntegralGBRuntimeSlot *slot1,
                                  IntegralGBRuntimeSlot *slot2)
{
    router->slot1 = slot1;
    router->slot2 = slot2;
    integral_gb_runtime_key_config_slot1_default(&router->slot1_keys);
    integral_gb_runtime_key_config_slot2_default(&router->slot2_keys);
    router->fast_key = integral_gb_runtime_key_config_fast_default();
    router->screenshot_key = integral_gb_runtime_key_config_screenshot_default();
    router->escape_key = integral_gb_runtime_key_config_escape_default();
    router->turbo_hold_key = integral_gb_runtime_key_config_turbo_hold_default();
    router->reset_key = integral_gb_runtime_key_config_reset_default();
    router->slot1_normal_buttons = 0;
    router->slot2_normal_buttons = 0;
    router->slot1_turbo_buttons = 0;
    router->slot2_turbo_buttons = 0;
    router->slot1_turbo_applied_buttons = 0;
    router->slot2_turbo_applied_buttons = 0;
    router->slot1_applied_buttons = 0;
    router->slot2_applied_buttons = 0;
    router->turbo_last_tick_ms = SDL_GetTicks();
    router->speed_multiplier = 1;
    router->speed_multiplier_changed = false;
    router->screenshot_requested = false;
    router->escape_requested = false;
    router->reset_requested = false;
    router->turbo_hold_active = false;
    router->turbo_capture_active = false;
    router->turbo_phase_on = true;
    router->turbo_macro_control = false;
    router->turbo_macro_toggle_requested = false;
    router->game_input_blocked = false;
}

void integral_gb_runtime_input_router_set_keymaps(IntegralGBRuntimeInputRouter *router,
                                         const IntegralGBRuntimeKeyConfig *slot1_keys,
                                         const IntegralGBRuntimeKeyConfig *slot2_keys,
                                         SDL_Keycode fast_key,
                                         SDL_Keycode screenshot_key,
                                         SDL_Keycode escape_key,
                                         SDL_Keycode turbo_hold_key,
                                         SDL_Keycode reset_key)
{
    if (slot1_keys) {
        router->slot1_keys = *slot1_keys;
    }
    if (slot2_keys) {
        router->slot2_keys = *slot2_keys;
    }
    if (fast_key != SDLK_UNKNOWN) {
        router->fast_key = fast_key;
    }
    if (screenshot_key != SDLK_UNKNOWN) {
        router->screenshot_key = screenshot_key;
    }
    if (escape_key != SDLK_UNKNOWN) {
        router->escape_key = escape_key;
    }
    router->turbo_hold_key = turbo_hold_key;
    if (reset_key != SDLK_UNKNOWN) {
        router->reset_key = reset_key;
    }
}

void integral_gb_runtime_input_router_set_speed_multiplier(IntegralGBRuntimeInputRouter *router, unsigned speed_multiplier)
{
    if (!router) {
        return;
    }
    if (speed_multiplier < 1u || speed_multiplier > 4u) {
        speed_multiplier = 1u;
    }
    router->speed_multiplier = speed_multiplier;
}

void integral_gb_runtime_input_router_disable_speed_controls(IntegralGBRuntimeInputRouter *router)
{
    if (!router) {
        return;
    }
    router->fast_key = SDLK_UNKNOWN;
    router->turbo_hold_key = SDLK_UNKNOWN;
    router->speed_multiplier = 1u;
    router->speed_multiplier_changed = false;
    router->slot1_turbo_buttons = 0u;
    router->slot2_turbo_buttons = 0u;
    router->turbo_hold_active = false;
    router->turbo_capture_active = false;
    router->turbo_phase_on = true;
    apply_router_buttons(router);
}

static void set_slot_key(IntegralGBRuntimeSlot *slot, GB_key_t gb_key, bool pressed)
{
    if (slot && slot->initialized) {
        GB_set_key_state(slot->gb, gb_key, pressed);
    }
}

static void set_slot_button(IntegralGBRuntimeSlot *slot, uint8_t button, bool pressed)
{
    switch (button) {
        case INTEGRAL_GB_RUNTIME_BTN_RIGHT:
            set_slot_key(slot, GB_KEY_RIGHT, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_LEFT:
            set_slot_key(slot, GB_KEY_LEFT, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_UP:
            set_slot_key(slot, GB_KEY_UP, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_DOWN:
            set_slot_key(slot, GB_KEY_DOWN, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_A:
            set_slot_key(slot, GB_KEY_A, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_B:
            set_slot_key(slot, GB_KEY_B, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_SELECT:
            set_slot_key(slot, GB_KEY_SELECT, pressed);
            break;
        case INTEGRAL_GB_RUNTIME_BTN_START:
            set_slot_key(slot, GB_KEY_START, pressed);
            break;
    }
}

static void apply_button_mask(IntegralGBRuntimeSlot *slot, uint8_t previous, uint8_t next)
{
    static const uint8_t buttons[] = {
        INTEGRAL_GB_RUNTIME_BTN_RIGHT,
        INTEGRAL_GB_RUNTIME_BTN_LEFT,
        INTEGRAL_GB_RUNTIME_BTN_UP,
        INTEGRAL_GB_RUNTIME_BTN_DOWN,
        INTEGRAL_GB_RUNTIME_BTN_A,
        INTEGRAL_GB_RUNTIME_BTN_B,
        INTEGRAL_GB_RUNTIME_BTN_SELECT,
        INTEGRAL_GB_RUNTIME_BTN_START,
    };
    uint8_t changed = (uint8_t)(previous ^ next);
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if (changed & buttons[i]) {
            set_slot_button(slot, buttons[i], (next & buttons[i]) != 0);
        }
    }
}

static void apply_router_buttons(IntegralGBRuntimeInputRouter *router)
{
    uint8_t slot1_turbo = router->turbo_hold_active && router->turbo_phase_on ? router->slot1_turbo_buttons : 0u;
    uint8_t slot2_turbo = router->turbo_hold_active && router->turbo_phase_on ? router->slot2_turbo_buttons : 0u;
    uint8_t slot1_next = (uint8_t)(router->slot1_normal_buttons | slot1_turbo);
    uint8_t slot2_next = (uint8_t)(router->slot2_normal_buttons | slot2_turbo);
    apply_button_mask(router->slot1, router->slot1_applied_buttons, slot1_next);
    apply_button_mask(router->slot2, router->slot2_applied_buttons, slot2_next);
    router->slot1_turbo_applied_buttons = slot1_turbo;
    router->slot2_turbo_applied_buttons = slot2_turbo;
    router->slot1_applied_buttons = slot1_next;
    router->slot2_applied_buttons = slot2_next;
}

bool integral_gb_runtime_input_router_handle_event(IntegralGBRuntimeInputRouter *router, const SDL_Event *event)
{
    if (event->type == SDL_QUIT) {
        return false;
    }
    if (event->type != SDL_KEYDOWN &&
        event->type != SDL_KEYUP &&
        event->type != SDL_CONTROLLERBUTTONDOWN &&
        event->type != SDL_CONTROLLERBUTTONUP &&
        event->type != SDL_CONTROLLERAXISMOTION &&
        event->type != SDL_JOYBUTTONDOWN &&
        event->type != SDL_JOYBUTTONUP &&
        event->type != SDL_JOYAXISMOTION &&
        event->type != SDL_JOYHATMOTION) {
        return true;
    }
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && event->key.repeat) {
        return true;
    }

    bool pressed = false;
    if (integral_gb_runtime_key_config_binding_matches_event(router->fast_key, event, &pressed)) {
        if (pressed) {
            router->speed_multiplier = router->speed_multiplier >= 4u ? 1u : router->speed_multiplier + 1u;
            router->speed_multiplier_changed = true;
        }
        return true;
    }
    if (integral_gb_runtime_key_config_binding_matches_event(router->screenshot_key, event, &pressed)) {
        if (pressed) {
            router->screenshot_requested = true;
        }
        return true;
    }
    if (integral_gb_runtime_key_config_binding_matches_event(router->escape_key, event, &pressed)) {
        if (pressed) {
            router->escape_requested = true;
        }
        return true;
    }
    if (integral_gb_runtime_key_config_binding_matches_event(router->reset_key, event, &pressed)) {
        if (pressed) {
            router->reset_requested = true;
        }
        return true;
    }
    if (integral_gb_runtime_key_config_binding_matches_event(router->turbo_hold_key, event, &pressed)) {
        if (router->turbo_macro_control) {
            if (pressed) {
                router->turbo_macro_toggle_requested = true;
            }
            return true;
        }
        if (pressed) {
            if (router->turbo_hold_active && !router->turbo_capture_active) {
                router->turbo_hold_active = false;
                router->turbo_capture_active = false;
                router->slot1_turbo_buttons = 0;
                router->slot2_turbo_buttons = 0;
            }
            else {
                router->turbo_capture_active = true;
                router->turbo_phase_on = true;
                router->turbo_last_tick_ms = SDL_GetTicks();
            }
        }
        else {
            router->turbo_capture_active = false;
            if (router->slot1_turbo_buttons == 0 && router->slot2_turbo_buttons == 0) {
                router->turbo_hold_active = false;
            }
        }
        if (!router->turbo_hold_active) {
            router->slot1_turbo_buttons = 0;
            router->slot2_turbo_buttons = 0;
        }
        apply_router_buttons(router);
        return true;
    }

    uint8_t slot1_press = 0;
    uint8_t slot1_release = 0;
    uint8_t slot2_press = 0;
    uint8_t slot2_release = 0;
    integral_gb_runtime_key_config_buttons_for_event(&router->slot1_keys, event, &slot1_press, &slot1_release);
    integral_gb_runtime_key_config_buttons_for_event(&router->slot2_keys, event, &slot2_press, &slot2_release);
    bool handled = slot1_press != 0 || slot1_release != 0 || slot2_press != 0 || slot2_release != 0;
    if (handled) {
        if (router->game_input_blocked) {
            return true;
        }
        router->slot1_normal_buttons = (uint8_t)((router->slot1_normal_buttons | slot1_press) & (uint8_t)~slot1_release);
        router->slot2_normal_buttons = (uint8_t)((router->slot2_normal_buttons | slot2_press) & (uint8_t)~slot2_release);
        if (router->turbo_capture_active) {
            router->slot1_turbo_buttons |= slot1_press;
            router->slot2_turbo_buttons |= slot2_press;
            if (slot1_press != 0 || slot2_press != 0) {
                router->turbo_hold_active = true;
            }
            router->slot1_normal_buttons &= (uint8_t)~router->slot1_turbo_buttons;
            router->slot2_normal_buttons &= (uint8_t)~router->slot2_turbo_buttons;
        }
        apply_router_buttons(router);
    }

    if (handled) {
        return true;
    }
    return true;
}

void integral_gb_runtime_input_router_update_turbo(IntegralGBRuntimeInputRouter *router)
{
    if (!router || !router->turbo_hold_active ||
        (router->slot1_turbo_buttons == 0 && router->slot2_turbo_buttons == 0)) {
        return;
    }
    uint32_t now = SDL_GetTicks();
    if (now - router->turbo_last_tick_ms < 67u) {
        return;
    }
    router->turbo_last_tick_ms = now;
    router->turbo_phase_on = !router->turbo_phase_on;
    apply_router_buttons(router);
}

void integral_gb_runtime_input_router_release_all(IntegralGBRuntimeInputRouter *router)
{
    router->slot1_normal_buttons = 0;
    router->slot2_normal_buttons = 0;
    router->slot1_turbo_buttons = 0;
    router->slot2_turbo_buttons = 0;
    router->turbo_hold_active = false;
    router->turbo_capture_active = false;
    router->turbo_phase_on = true;
    apply_router_buttons(router);
    router->speed_multiplier = 1;
}

void integral_gb_runtime_input_router_set_turbo_macro_control(IntegralGBRuntimeInputRouter *router, bool enabled)
{
    if (!router) {
        return;
    }
    router->turbo_macro_control = enabled;
    router->turbo_macro_toggle_requested = false;
    if (enabled) {
        router->slot1_turbo_buttons = 0;
        router->slot2_turbo_buttons = 0;
        router->turbo_hold_active = false;
        router->turbo_capture_active = false;
        apply_router_buttons(router);
    }
}

bool integral_gb_runtime_input_router_take_turbo_macro_toggle(IntegralGBRuntimeInputRouter *router)
{
    if (!router || !router->turbo_macro_toggle_requested) {
        return false;
    }
    router->turbo_macro_toggle_requested = false;
    return true;
}

void integral_gb_runtime_input_router_set_game_input_blocked(IntegralGBRuntimeInputRouter *router, bool blocked)
{
    if (!router || router->game_input_blocked == blocked) {
        return;
    }
    router->game_input_blocked = blocked;
    if (blocked) {
        router->slot1_normal_buttons = 0;
        router->slot2_normal_buttons = 0;
        router->slot1_turbo_buttons = 0;
        router->slot2_turbo_buttons = 0;
        router->turbo_hold_active = false;
        router->turbo_capture_active = false;
        apply_router_buttons(router);
    }
}

unsigned integral_gb_runtime_input_router_speed_multiplier(const IntegralGBRuntimeInputRouter *router)
{
    return router && router->speed_multiplier >= 1u ? router->speed_multiplier : 1u;
}

bool integral_gb_runtime_input_router_take_speed_multiplier_changed(IntegralGBRuntimeInputRouter *router)
{
    if (!router || !router->speed_multiplier_changed) {
        return false;
    }
    router->speed_multiplier_changed = false;
    return true;
}

static bool append_turbo_button(char *dest, size_t dest_size, size_t *used, const char *name)
{
    size_t name_len = strlen(name);
    size_t extra = (*used > 0 ? 1u : 0u) + name_len;
    if (*used + extra + 1u > dest_size) {
        return false;
    }
    if (*used > 0) {
        dest[(*used)++] = ' ';
    }
    memcpy(dest + *used, name, name_len);
    *used += name_len;
    dest[*used] = '\0';
    return true;
}

bool integral_gb_runtime_input_router_describe_turbo(const IntegralGBRuntimeInputRouter *router,
                                           char *dest,
                                           size_t dest_size)
{
    if (!dest || dest_size == 0) {
        return false;
    }
    dest[0] = '\0';
    if (!router || !router->turbo_hold_active || router->slot1_turbo_buttons == 0) {
        return false;
    }

    size_t used = 0;
    if (!append_turbo_button(dest, dest_size, &used, "TURBO")) {
        return false;
    }
    static const struct {
        uint8_t button;
        const char *name;
    } buttons[] = {
        {INTEGRAL_GB_RUNTIME_BTN_A, "A"},
        {INTEGRAL_GB_RUNTIME_BTN_B, "B"},
        {INTEGRAL_GB_RUNTIME_BTN_SELECT, "SELECT"},
        {INTEGRAL_GB_RUNTIME_BTN_START, "START"},
        {INTEGRAL_GB_RUNTIME_BTN_UP, "UP"},
        {INTEGRAL_GB_RUNTIME_BTN_DOWN, "DOWN"},
        {INTEGRAL_GB_RUNTIME_BTN_LEFT, "LEFT"},
        {INTEGRAL_GB_RUNTIME_BTN_RIGHT, "RIGHT"},
    };
    for (size_t i = 0; i < sizeof(buttons) / sizeof(buttons[0]); i++) {
        if ((router->slot1_turbo_buttons & buttons[i].button) &&
            !append_turbo_button(dest, dest_size, &used, buttons[i].name)) {
            return false;
        }
    }
    return true;
}

bool integral_gb_runtime_input_router_take_screenshot_request(IntegralGBRuntimeInputRouter *router)
{
    if (!router || !router->screenshot_requested) {
        return false;
    }
    router->screenshot_requested = false;
    return true;
}

bool integral_gb_runtime_input_router_take_escape_request(IntegralGBRuntimeInputRouter *router)
{
    if (!router || !router->escape_requested) {
        return false;
    }
    router->escape_requested = false;
    return true;
}

bool integral_gb_runtime_input_router_take_reset_request(IntegralGBRuntimeInputRouter *router)
{
    if (!router || !router->reset_requested) {
        return false;
    }
    router->reset_requested = false;
    return true;
}

uint8_t integral_gb_runtime_input_router_slot1_buttons(
    const IntegralGBRuntimeInputRouter *router)
{
    return router != NULL ? router->slot1_applied_buttons : 0u;
}

void integral_gb_runtime_input_router_print_keymap(const IntegralGBRuntimeInputRouter *router)
{
    char slot1[256];
    char slot2[256];
    integral_gb_runtime_key_config_describe(&router->slot1_keys, slot1, sizeof(slot1));
    integral_gb_runtime_key_config_describe(&router->slot2_keys, slot2, sizeof(slot2));
    printf("  slot1 keys: %s\n", slot1);
    if (router->slot2) {
        printf("  slot2 keys: %s\n", slot2);
    }
    printf("  fast key: %s\n", integral_gb_runtime_key_config_key_name(router->fast_key));
    printf("  screenshot key: %s\n", integral_gb_runtime_key_config_key_name(router->screenshot_key));
    printf("  escape key: %s\n", integral_gb_runtime_key_config_key_name(router->escape_key));
    printf("  reset key: %s\n", integral_gb_runtime_key_config_key_name(router->reset_key));
    if (router->turbo_hold_key != SDLK_UNKNOWN) {
        printf("  turbo hold key: %s\n", integral_gb_runtime_key_config_key_name(router->turbo_hold_key));
    }
}

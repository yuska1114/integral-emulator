/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_input_alias.h"

#include <string.h>

#include "../runtimes/gb/src/common/key_config.h"

static const char *alias_name(const IntegralConfigKeys *keys, unsigned index)
{
    switch (index) {
        case 0u: return keys->client_alias_right;
        case 1u: return keys->client_alias_left;
        case 2u: return keys->client_alias_up;
        case 3u: return keys->client_alias_down;
        case 4u: return keys->client_alias_enter;
        default: return keys->client_alias_escape;
    }
}

static SDL_Keycode alias_operation_key(unsigned index)
{
    static const SDL_Keycode operations[INTEGRAL_CLIENT_ALIAS_KEYS] = {
        SDLK_RIGHT, SDLK_LEFT, SDLK_UP, SDLK_DOWN, SDLK_RETURN, SDLK_ESCAPE
    };
    return index < INTEGRAL_CLIENT_ALIAS_KEYS ? operations[index] : SDLK_UNKNOWN;
}

bool integral_client_alias_key_reserved(SDL_Keycode key)
{
    switch (key) {
        case SDLK_UP:
        case SDLK_DOWN:
        case SDLK_LEFT:
        case SDLK_RIGHT:
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
        case SDLK_ESCAPE:
        case SDLK_TAB:
        case SDLK_BACKSPACE:
        case SDLK_DELETE:
        case SDLK_F2:
        case SDLK_F3:
        case SDLK_F4:
        case SDLK_F5:
            return true;
        default:
            return false;
    }
}

bool integral_client_alias_keyboard_event(const IntegralConfigKeys *keys,
                                          const SDL_KeyboardEvent *event,
                                          SDL_KeyboardEvent *translated)
{
    if (!keys || !event || !translated || event->type != SDL_KEYDOWN) return false;
    for (unsigned i = 0; i < INTEGRAL_CLIENT_ALIAS_KEYS; i++) {
        const char *name = alias_name(keys, i);
        if (!name[0]) continue;
        SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(name);
        if (binding == SDLK_UNKNOWN ||
            integral_gb_runtime_key_config_is_controller_code(binding) ||
            integral_client_alias_key_reserved(binding)) {
            continue;
        }
        if (event->keysym.sym == binding) {
            *translated = *event;
            translated->keysym.sym = alias_operation_key(i);
            translated->keysym.scancode = SDL_GetScancodeFromKey(translated->keysym.sym);
            return true;
        }
    }
    return false;
}

bool integral_client_alias_controller_event(const IntegralConfigKeys *keys,
                                            bool held[INTEGRAL_CLIENT_ALIAS_KEYS],
                                            const SDL_Event *event,
                                            SDL_KeyboardEvent *translated)
{
    if (!keys || !held || !event || !translated) return false;
    int rising_index = -1;
    for (unsigned i = 0; i < INTEGRAL_CLIENT_ALIAS_KEYS; i++) {
        const char *name = alias_name(keys, i);
        if (!name[0]) continue;
        SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(name);
        if (binding == SDLK_UNKNOWN ||
            !integral_gb_runtime_key_config_is_controller_code(binding)) {
            continue;
        }
        bool pressed = false;
        if (!integral_gb_runtime_key_config_binding_matches_event(binding, event, &pressed)) {
            continue;
        }
        bool rising = pressed && !held[i];
        held[i] = pressed;
        if (rising && rising_index < 0) rising_index = (int)i;
    }
    if (rising_index < 0) return false;
    memset(translated, 0, sizeof(*translated));
    translated->type = SDL_KEYDOWN;
    translated->state = SDL_PRESSED;
    translated->keysym.sym = alias_operation_key((unsigned)rising_index);
    translated->keysym.scancode = SDL_GetScancodeFromKey(translated->keysym.sym);
    return true;
}

bool integral_client_alias_has_controller_binding(const IntegralConfigKeys *keys)
{
    if (!keys) return false;
    for (unsigned i = 0; i < INTEGRAL_CLIENT_ALIAS_KEYS; i++) {
        const char *name = alias_name(keys, i);
        if (!name[0]) continue;
        SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(name);
        if (binding != SDLK_UNKNOWN &&
            integral_gb_runtime_key_config_is_controller_code(binding)) {
            return true;
        }
    }
    return false;
}

void integral_client_alias_reset_state(bool held[INTEGRAL_CLIENT_ALIAS_KEYS])
{
    if (held) memset(held, 0, sizeof(bool) * INTEGRAL_CLIENT_ALIAS_KEYS);
}

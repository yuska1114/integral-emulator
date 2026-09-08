/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_KEY_CONFIG_H
#define INTEGRAL_GB_RUNTIME_KEY_CONFIG_H

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD 16000
#define INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_RELEASE_THRESHOLD 8000

typedef struct IntegralGBRuntimeKeyConfig {
    SDL_Keycode right;
    SDL_Keycode left;
    SDL_Keycode up;
    SDL_Keycode down;
    SDL_Keycode a;
    SDL_Keycode b;
    SDL_Keycode select;
    SDL_Keycode start;
} IntegralGBRuntimeKeyConfig;

void integral_gb_runtime_key_config_slot1_default(IntegralGBRuntimeKeyConfig *config);
void integral_gb_runtime_key_config_slot2_default(IntegralGBRuntimeKeyConfig *config);
SDL_Keycode integral_gb_runtime_key_config_fast_default(void);
SDL_Keycode integral_gb_runtime_key_config_screenshot_default(void);
SDL_Keycode integral_gb_runtime_key_config_escape_default(void);
SDL_Keycode integral_gb_runtime_key_config_turbo_hold_default(void);
SDL_Keycode integral_gb_runtime_key_config_reset_default(void);
SDL_Keycode integral_gb_runtime_key_config_key_from_name(const char *name);
const char *integral_gb_runtime_key_config_key_name(SDL_Keycode key);
SDL_Keycode integral_gb_runtime_key_config_code_from_controller_button(SDL_GameControllerButton button);
SDL_Keycode integral_gb_runtime_key_config_code_from_controller_axis(SDL_GameControllerAxis axis, int direction);
SDL_Keycode integral_gb_runtime_key_config_code_from_joystick_button(int button);
SDL_Keycode integral_gb_runtime_key_config_code_from_joystick_axis(int axis, int direction);
SDL_Keycode integral_gb_runtime_key_config_code_from_joystick_hat(int hat, int direction);
bool integral_gb_runtime_key_config_is_controller_code(SDL_Keycode key);
int integral_gb_runtime_key_config_open_game_controllers(void);
void integral_gb_runtime_key_config_close_game_controllers(void);
void integral_gb_runtime_key_config_handle_device_event(const SDL_Event *event);
SDL_Keycode integral_gb_runtime_key_config_code_from_event(const SDL_Event *event);
bool integral_gb_runtime_key_config_binding_pressed(SDL_Keycode binding);
bool integral_gb_runtime_key_config_binding_to_n64(SDL_Keycode binding,
                                                   int *device_index,
                                                   int *kind,
                                                   int *index,
                                                   int *direction);
int integral_gb_runtime_key_config_parse(IntegralGBRuntimeKeyConfig *config, const char *spec);
void integral_gb_runtime_key_config_describe(const IntegralGBRuntimeKeyConfig *config, char *out, size_t out_size);
uint8_t integral_gb_runtime_key_config_button_for_key(const IntegralGBRuntimeKeyConfig *config, SDL_Keycode key);
uint8_t integral_gb_runtime_key_config_button_for_event(const IntegralGBRuntimeKeyConfig *config,
                                              const SDL_Event *event,
                                              bool *pressed);
void integral_gb_runtime_key_config_buttons_for_event(const IntegralGBRuntimeKeyConfig *config,
                                            const SDL_Event *event,
                                            uint8_t *press_mask,
                                            uint8_t *release_mask);
bool integral_gb_runtime_key_config_binding_matches_event(SDL_Keycode binding,
                                                const SDL_Event *event,
                                                bool *pressed);

#endif

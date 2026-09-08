/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "key_config_menu.h"

#include <stdbool.h>
#include <stdio.h>

#include "key_config.h"
#include "menu_config.h"
#include "menu_draw.h"
#include "protocol.h"

static void reset_key_config_defaults(IntegralGBRuntimeMenu *menu)
{
    integral_gb_runtime_key_config_slot1_default(&menu->slot1_key_config);
    integral_gb_runtime_key_config_slot2_default(&menu->slot2_key_config);
    menu->fast_keycode = integral_gb_runtime_key_config_fast_default();
    menu->screenshot_keycode = integral_gb_runtime_key_config_screenshot_default();
    menu->escape_keycode = integral_gb_runtime_key_config_escape_default();
    menu->turbo_hold_keycode = integral_gb_runtime_key_config_turbo_hold_default();
    menu->reset_keycode = integral_gb_runtime_key_config_reset_default();
    integral_gb_runtime_menu_refresh_key_descriptions(menu);
    if (integral_gb_runtime_menu_save_config_file(menu) == 0) {
        snprintf(menu->status, sizeof(menu->status), "KEY CONFIG RESET");
    }
    else {
        snprintf(menu->status, sizeof(menu->status), "KEY CONFIG RESET SAVE FAILED");
    }
}

static void set_capture_key(IntegralGBRuntimeKeyConfig *config, unsigned step, SDL_Keycode key)
{
    switch (step) {
        case 0:
            config->a = key;
            break;
        case 1:
            config->b = key;
            break;
        case 2:
            config->select = key;
            break;
        case 3:
            config->start = key;
            break;
        case 4:
            config->right = key;
            break;
        case 5:
            config->left = key;
            break;
        case 6:
            config->up = key;
            break;
        case 7:
            config->down = key;
            break;
    }
}

static bool is_physical_arrow_key(SDL_Keycode key)
{
    return key == SDLK_RIGHT || key == SDLK_LEFT || key == SDLK_UP || key == SDLK_DOWN;
}

static bool is_physical_enter_key(SDL_Keycode key)
{
    return key == SDLK_RETURN || key == SDLK_KP_ENTER;
}

static bool is_direction_capture_step(unsigned step)
{
    return step >= 4 && step <= 7;
}

static bool key_allowed_for_capture(IntegralGBRuntimeMenu *menu, SDL_Keycode key)
{
    if (menu->key_config_target == KEY_CONFIG_UTILS) {
        if (is_physical_arrow_key(key)) {
            snprintf(menu->status, sizeof(menu->status), "ARROWS ARE DIRECTION ONLY");
            return false;
        }
        return true;
    }

    bool direction_step = is_direction_capture_step(menu->key_config_step);
    if (is_physical_arrow_key(key) && !direction_step) {
        snprintf(menu->status, sizeof(menu->status), "ARROWS ARE DIRECTION ONLY");
        return false;
    }
    if (is_physical_enter_key(key) && direction_step) {
        snprintf(menu->status, sizeof(menu->status), "ENTER CANNOT BE DIRECTION");
        return false;
    }
    return true;
}

static void begin_key_config(IntegralGBRuntimeMenu *menu, KeyConfigTarget target)
{
    menu->key_config_target = target;
    menu->key_config_step = 0;
    menu->controller_capture_wait_release = false;
    menu->controller_capture_release_key = SDLK_UNKNOWN;
    if (target == KEY_CONFIG_SLOT1) {
        menu->pending_key_config = menu->slot1_key_config;
        snprintf(menu->status, sizeof(menu->status), "CONFIG SLOT1");
    }
    else if (target == KEY_CONFIG_SLOT2) {
        menu->pending_key_config = menu->slot2_key_config;
        snprintf(menu->status, sizeof(menu->status), "CONFIG SLOT2");
    }
    else if (target == KEY_CONFIG_UTILS) {
        snprintf(menu->status, sizeof(menu->status), "CONFIG UTILS");
    }
}

static void finish_key_config(IntegralGBRuntimeMenu *menu)
{
    if (menu->key_config_target == KEY_CONFIG_SLOT1) {
        menu->slot1_key_config = menu->pending_key_config;
    }
    else if (menu->key_config_target == KEY_CONFIG_SLOT2) {
        menu->slot2_key_config = menu->pending_key_config;
    }
    integral_gb_runtime_menu_refresh_key_descriptions(menu);
    menu->key_config_target = KEY_CONFIG_NONE;
    menu->key_config_step = 0;
    menu->controller_capture_wait_release = false;
    menu->controller_capture_release_key = SDLK_UNKNOWN;
    if (integral_gb_runtime_menu_save_config_file(menu) == 0) {
        snprintf(menu->status, sizeof(menu->status), "KEY CONFIG SAVED");
    }
    else {
        snprintf(menu->status, sizeof(menu->status), "KEY CONFIG SAVE FAILED");
    }
}

static void handle_key_config_press(IntegralGBRuntimeMenu *menu, SDL_Keycode key)
{
    if (!key_allowed_for_capture(menu, key)) {
        return;
    }

    if (menu->key_config_target == KEY_CONFIG_UTILS) {
        if (menu->key_config_step == 0) {
            menu->fast_keycode = key;
            menu->key_config_step++;
        }
        else if (menu->key_config_step == 1) {
            menu->screenshot_keycode = key;
            menu->key_config_step++;
        }
        else if (menu->key_config_step == 2) {
            menu->escape_keycode = key;
            menu->key_config_step++;
        }
        else if (menu->key_config_step == 3) {
            menu->turbo_hold_keycode = key;
            menu->key_config_step++;
        }
        else {
            menu->reset_keycode = key;
            finish_key_config(menu);
        }
        return;
    }

    set_capture_key(&menu->pending_key_config, menu->key_config_step, key);
    menu->key_config_step++;
    if (menu->key_config_step >= 8) {
        finish_key_config(menu);
    }
}

static void start_selected_config(IntegralGBRuntimeMenu *menu)
{
    if (menu->selected_row == 0) {
        begin_key_config(menu, KEY_CONFIG_SLOT1);
    }
    else if (menu->selected_row == 1) {
        begin_key_config(menu, KEY_CONFIG_SLOT2);
    }
    else if (menu->selected_row == 2) {
        begin_key_config(menu, KEY_CONFIG_UTILS);
    }
}

static SDL_Keycode controller_binding_from_event(const SDL_Event *event)
{
    return integral_gb_runtime_key_config_code_from_event(event);
}

static bool controller_release_event(const SDL_Event *event, SDL_Keycode captured)
{
    bool pressed = true;
    return integral_gb_runtime_key_config_binding_matches_event(captured, event, &pressed) && !pressed;
}

void integral_gb_runtime_key_config_menu_run(SDL_Renderer *renderer, SDL_Window *window, IntegralGBRuntimeMenu *menu)
{
    (void)window;
    unsigned previous_main_row = menu->selected_row;
    unsigned selected_row = 0;
    bool running = true;
    menu->key_config_target = KEY_CONFIG_NONE;
    menu->key_config_step = 0;
    menu->controller_capture_wait_release = false;
    menu->controller_capture_release_key = SDLK_UNKNOWN;
    snprintf(menu->status, sizeof(menu->status), "KEY CONFIG");

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED) {
                integral_gb_runtime_key_config_handle_device_event(&event);
            }
            else if (event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
                integral_gb_runtime_key_config_handle_device_event(&event);
            }
            else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                bool escape_pressed = key == SDLK_ESCAPE;
                bool escape_binding_pressed = false;
                if (integral_gb_runtime_key_config_binding_matches_event(menu->escape_keycode, &event, &escape_binding_pressed) &&
                    escape_binding_pressed) {
                    escape_pressed = true;
                }
                if (menu->key_config_target != KEY_CONFIG_NONE) {
                    if (escape_pressed && menu->key_config_target != KEY_CONFIG_UTILS) {
                        menu->key_config_target = KEY_CONFIG_NONE;
                        menu->key_config_step = 0;
                        menu->controller_capture_wait_release = false;
                        menu->controller_capture_release_key = SDLK_UNKNOWN;
                        snprintf(menu->status, sizeof(menu->status), "KEY CONFIG CANCELED");
                    }
                    else {
                        handle_key_config_press(menu, key);
                    }
                }
                else {
                    uint8_t menu_button = integral_gb_runtime_key_config_button_for_key(&menu->slot1_key_config, key);
                    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_A;
                    }
                    else if (key == SDLK_UP) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_UP;
                    }
                    else if (key == SDLK_DOWN) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_DOWN;
                    }
                    if (escape_pressed) {
                        running = false;
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_UP) {
                        selected_row = (selected_row + INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS - 1) % INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS;
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_DOWN) {
                        selected_row = (selected_row + 1) % INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS;
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_A) {
                        if (selected_row == 3) {
                            reset_key_config_defaults(menu);
                        }
                        else if (selected_row == 4) {
                            running = false;
                        }
                        else {
                            menu->selected_row = selected_row;
                            start_selected_config(menu);
                        }
                    }
                }
            }
            else if ((event.type == SDL_CONTROLLERBUTTONDOWN ||
                      event.type == SDL_CONTROLLERBUTTONUP ||
                      event.type == SDL_CONTROLLERAXISMOTION ||
                      event.type == SDL_JOYBUTTONDOWN ||
                      event.type == SDL_JOYBUTTONUP ||
                      event.type == SDL_JOYAXISMOTION ||
                      event.type == SDL_JOYHATMOTION) &&
                     menu->key_config_target != KEY_CONFIG_NONE) {
                if (menu->controller_capture_wait_release) {
                    if (controller_release_event(&event, menu->controller_capture_release_key)) {
                        menu->controller_capture_wait_release = false;
                        menu->controller_capture_release_key = SDLK_UNKNOWN;
                    }
                    continue;
                }
                SDL_Keycode binding = controller_binding_from_event(&event);
                if (binding != SDLK_UNKNOWN) {
                    handle_key_config_press(menu, binding);
                    menu->controller_capture_wait_release = true;
                    menu->controller_capture_release_key = binding;
                }
            }
            else if ((event.type == SDL_CONTROLLERBUTTONDOWN ||
                      event.type == SDL_CONTROLLERBUTTONUP ||
                      event.type == SDL_CONTROLLERAXISMOTION ||
                      event.type == SDL_JOYBUTTONDOWN ||
                      event.type == SDL_JOYBUTTONUP ||
                     event.type == SDL_JOYAXISMOTION ||
                     event.type == SDL_JOYHATMOTION) &&
                     menu->key_config_target == KEY_CONFIG_NONE) {
                bool pressed = false;
                uint8_t menu_button = integral_gb_runtime_key_config_button_for_event(&menu->slot1_key_config, &event, &pressed);
                bool escape_pressed = false;
                if (integral_gb_runtime_key_config_binding_matches_event(menu->escape_keycode, &event, &escape_pressed) &&
                    escape_pressed) {
                    running = false;
                    continue;
                }
                if (!pressed) {
                    continue;
                }
                if (menu_button & INTEGRAL_GB_RUNTIME_BTN_UP) {
                    selected_row = (selected_row + INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS - 1) % INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS;
                }
                else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_DOWN) {
                    selected_row = (selected_row + 1) % INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS;
                }
                else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_A) {
                    if (selected_row == 3) {
                        reset_key_config_defaults(menu);
                    }
                    else if (selected_row == 4) {
                        running = false;
                    }
                    else {
                        menu->selected_row = selected_row;
                        start_selected_config(menu);
                    }
                }
            }
        }

        integral_gb_runtime_menu_draw_key_config_menu(renderer, menu, selected_row);
        SDL_Delay(16);
    }

    menu->key_config_target = KEY_CONFIG_NONE;
    menu->key_config_step = 0;
    menu->controller_capture_wait_release = false;
    menu->controller_capture_release_key = SDLK_UNKNOWN;
    menu->selected_row = previous_main_row;
    snprintf(menu->status, sizeof(menu->status), "READY");
}

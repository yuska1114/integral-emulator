/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_event.h"

#include "client_account.h"
#include "client_app.h"
#include "client_config.h"
#include "client_input_alias.h"
#include "client_local.h"
#include "client_log.h"
#include "client_room_common.h"
#include "client_room_n64.h"
#include "client_runtime_support.h"
#include "client_screenshots.h"
#include "../runtimes/gb/src/common/key_config.h"

#include <string.h>

static bool client_alias_enabled(const AppState *state)
{
    if (state->key_editor.key_capture_target != KEY_CAPTURE_NONE) return false;
    if (state->local.local_monitor > 0) return false;
    if (state->ui.screen == SCREEN_ROOM &&
        (state->room.link.room_client_pid > 0 ||
         state->room.link.room_gb_runtime_fixed_host_active)) return false;
    if (state->ui.screen == SCREEN_N64_ROOM && n64_room_runtime_active(&state->room)) return false;
    return true;
}

static void dispatch_client_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (state->ui.screen == SCREEN_LOGIN) {
        handle_login_key(state, key);
    }
    else if (state->ui.screen == SCREEN_PASSWORD_CHANGE) {
        handle_password_change_key(state, key);
    }
    else if (state->ui.screen == SCREEN_MAIN_MENU) {
        handle_main_key(state, key);
    }
    else if (state->ui.screen == SCREEN_LOCAL_MODE) {
        handle_local_mode_key(state, key);
    }
    else if (state->ui.screen == SCREEN_ROOM_MODE) {
        handle_room_mode_key(&state->room, key);
    }
    else if (state->ui.screen == SCREEN_LOCAL || state->ui.screen == SCREEN_GB_MOBILE) {
        handle_local_key(state, key);
    }
    else if (state->ui.screen == SCREEN_N64_RUNTIME) {
        handle_n64_runtime_key(state, key);
    }
    else if (state->ui.screen == SCREEN_JOIN_ROOM) {
        handle_join_room_key(&state->room, key);
    }
    else if (state->ui.screen == SCREEN_ROOM) {
        handle_room_key(&state->room, key);
    }
    else if (state->ui.screen == SCREEN_N64_ROOM) {
        handle_n64_room_key(&state->room, key);
    }
    else if (state->ui.screen == SCREEN_GB_KEY_CONFIG ||
             state->ui.screen == SCREEN_N64_KEY_CONFIG ||
             state->ui.screen == SCREEN_UTIL_KEY_CONFIG) {
        handle_key_config_key(state, key);
    }
    else if (state->ui.screen == SCREEN_SETTINGS) {
        handle_settings_key(state, key);
    }
    else if (state->ui.screen == SCREEN_OPTIONS) {
        handle_options_key(state, key);
    }
    else if (state->ui.screen == SCREEN_SCREENSHOTS) {
        if (integral_screenshots_key(state->screenshots, key)) {
            integral_screenshots_close(state->screenshots);
            state->screenshots = NULL;
            state->ui.screen = SCREEN_MAIN_MENU;
        }
    }
    else if (state->ui.screen == SCREEN_ROM_REGISTER) {
        handle_rom_key(state, key);
    }
}

void integral_client_handle_event(AppState *state, const SDL_Event *event)
{
    if (!state || !event) return;
    if (handle_n64_room_util_event(&state->room, event)) return;
    if (handle_runtime_exit_confirmation_event(&state->room, event)) return;

    if (event->type == SDL_QUIT && n64_room_runtime_active(&state->room)) {
        request_runtime_exit_confirmation(&state->room, true);
    }
    else if (event->type == SDL_QUIT) {
        client_log(state, "client_quit_event", "status=%s", state->login.status);
        leave_current_room(&state->room, false);
        state->ui.quit = true;
    }
    else if (event->type == SDL_KEYDOWN) {
        SDL_KeyboardEvent client_key = event->key;
        bool translated = client_alias_enabled(state) &&
            integral_client_alias_keyboard_event(&state->keys, &event->key, &client_key);
        if (translated && SDL_IsTextInputActive() &&
            event->key.keysym.sym >= SDLK_SPACE && event->key.keysym.sym < SDLK_DELETE) {
            state->ui.suppress_text_input_once = true;
        }
        dispatch_client_key(state, &client_key);
    }
    else if (state->ui.game_input_active &&
             (event->type == SDL_CONTROLLERDEVICEADDED || event->type == SDL_JOYDEVICEADDED ||
              event->type == SDL_CONTROLLERDEVICEREMOVED || event->type == SDL_JOYDEVICEREMOVED)) {
        integral_gb_runtime_key_config_handle_device_event(event);
        if (event->type == SDL_CONTROLLERDEVICEREMOVED || event->type == SDL_JOYDEVICEREMOVED) {
            state->room.n64.util_escape_held = state->room.n64.util_screenshot_held = false;
            state->key_editor.key_capture_wait_release = false;
            state->key_editor.key_capture_release_binding = SDLK_UNKNOWN;
            integral_client_alias_reset_state(state->ui.client_alias_held);
        }
        client_log(state, "controller_device_changed", "event=%u", event->type);
    }
    else if ((state->ui.screen == SCREEN_GB_KEY_CONFIG ||
              state->ui.screen == SCREEN_N64_KEY_CONFIG ||
              state->ui.screen == SCREEN_UTIL_KEY_CONFIG) &&
             state->key_editor.key_capture_target != KEY_CAPTURE_NONE &&
             game_controller_input_event(event->type)) {
        handle_key_config_controller_event(state, event);
    }
    else if (client_alias_enabled(state) && game_controller_input_event(event->type)) {
        SDL_KeyboardEvent client_key = {0};
        if (integral_client_alias_controller_event(&state->keys,
                                                   state->ui.client_alias_held,
                                                   event,
                                                   &client_key)) {
            dispatch_client_key(state, &client_key);
        }
    }
    else if (event->type == SDL_WINDOWEVENT &&
             event->window.event == SDL_WINDOWEVENT_CLOSE &&
             integral_n64_runtime_media_stream_is_video_window(state->room.n64.n64_runtime_media_stream,
                                                                 event->window.windowID)) {
        request_runtime_exit_confirmation(&state->room, false);
    }
    else if (event->type == SDL_WINDOWEVENT && state->ui.client_window &&
             event->window.windowID == SDL_GetWindowID(state->ui.client_window) &&
             event->window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
        clear_local_launch_notice(state);
    }
    else if (event->type == SDL_WINDOWEVENT && state->ui.client_window &&
             event->window.windowID == SDL_GetWindowID(state->ui.client_window) &&
             (event->window.event == SDL_WINDOWEVENT_RESIZED ||
              event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
        int width = 0;
        int height = 0;
        SDL_GetWindowSize(state->ui.client_window, &width, &height);
        if (width >= (int)INTEGRAL_CONFIG_WINDOW_MIN_WIDTH &&
            height >= (int)INTEGRAL_CONFIG_WINDOW_MIN_HEIGHT &&
            ((unsigned)width != state->ui.window_width ||
             (unsigned)height != state->ui.window_height)) {
            state->ui.window_width = (unsigned)width;
            state->ui.window_height = (unsigned)height;
            state->ui.window_size_dirty = true;
        }
    }
    else if (event->type == SDL_TEXTINPUT) {
        if (state->ui.suppress_text_input_once) {
            state->ui.suppress_text_input_once = false;
            return;
        }
        if (state->ui.screen == SCREEN_LOGIN && state->login.editing) {
            handle_text_input(&state->login, &event->text);
            if (state->login.selected == FIELD_SERVER &&
                save_login_form_config(&state->login, state->base_config_path) != 0) {
                copy_text(state->login.status, sizeof(state->login.status), "LOGIN PREF SAVE FAILED");
            }
        }
        else if (state->ui.screen == SCREEN_PASSWORD_CHANGE) {
            handle_password_change_text_input(state, &event->text);
        }
        else if (state->ui.screen == SCREEN_ROM_REGISTER &&
                 state->catalog.rom_editor.rom_edit_target != ROM_EDIT_NONE) {
            handle_rom_text_input(state, &event->text);
        }
        else if (state->ui.screen == SCREEN_JOIN_ROOM) {
            handle_join_room_text_input(&state->room, &event->text);
        }
        else if (state->ui.screen == SCREEN_ROOM) {
            handle_room_text_input(&state->room, &event->text);
        }
    }
    else if (event->type == SDL_TEXTEDITING && state->ui.screen == SCREEN_ROOM) {
        handle_room_text_editing(&state->room, &event->edit);
    }
}

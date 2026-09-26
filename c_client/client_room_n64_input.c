/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_room_common.h"
#include "client_room_n64.h"
#include "client_key_config.h"
#include "client_runtime_support.h"
#include "../runtimes/gb/src/common/key_config.h"
#include "../runtimes/n64/src/remote_media_ipc.h"

#include <string.h>

bool n64_room_runtime_active(const IntegralRoomContext *state)
{
    return (*state->screen) == SCREEN_N64_ROOM &&
           (state->n64.n64_runtime_media_paired || state->n64.n64_runtime_media_host_pid != 0);
}


void request_runtime_exit_confirmation(IntegralRoomContext *state, bool quit_client)
{
    state->n64.runtime_exit_confirming = true;
    state->n64.runtime_exit_confirm_yes = false;
    state->n64.runtime_exit_quit_client = quit_client;
    state->n64.n64_runtime_media_last_sent_buttons = UINT64_MAX;
    integral_n64_runtime_media_stream_set_exit_confirmation(
        state->n64.n64_runtime_media_stream, true, false);
}


static void cancel_runtime_exit_confirmation(IntegralRoomContext *state)
{
    state->n64.runtime_exit_confirming = false;
    state->n64.runtime_exit_confirm_yes = false;
    state->n64.runtime_exit_quit_client = false;
    integral_n64_runtime_media_stream_set_exit_confirmation(
        state->n64.n64_runtime_media_stream, false, false);
}


static void confirm_runtime_exit(IntegralRoomContext *state)
{
    bool quit_client = state->n64.runtime_exit_quit_client;
    cancel_runtime_exit_confirmation(state);
    leave_current_room(state, false);
    if (state->common.room_number) return;
    if (quit_client) {
        (*state->quit) = true;
    }
    else {
        (*state->screen) = SCREEN_MAIN_MENU;
        copy_text(state->login->status, sizeof(state->login->status), "MAIN MENU");
    }
}


static bool n64_confirmation_binding_pressed(const IntegralRoomContext *state,
                                             const SDL_Event *event,
                                             unsigned index)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    bool pressed = false;
    key_spec_to_names_count(state->keys->n64_p1, names,
                            INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    if (index >= INTEGRAL_N64_RUNTIME_KEY_BUTTONS || !names[index][0]) return false;
    SDL_Keycode binding = integral_gb_runtime_key_config_key_from_name(names[index]);
    return binding != SDLK_UNKNOWN &&
           integral_gb_runtime_key_config_binding_matches_event(binding, event, &pressed) &&
           pressed;
}


void capture_n64_room(const IntegralRoomContext *state)
{
    if (strcmp(state->n64.n64_runtime_media_role, "remote") == 0) {
        integral_n64_runtime_media_stream_save_screenshot(
            state->n64.n64_runtime_media_stream, "n64_room");
    } else if (state->n64.n64_runtime_media_host_pid &&
               integral_n64_runtime_remote_media_request_screenshot() == 0) {
        copy_text(state->login->status, sizeof(state->login->status), "SCREENSHOT REQUESTED");
    } else {
        copy_text(state->login->status, sizeof(state->login->status), "SCREENSHOT FAILED");
    }
}


bool handle_n64_room_util_event(IntegralRoomContext *state, const SDL_Event *event)
{
    if (!n64_room_runtime_active(state)) return false;
    bool escape = integral_gb_runtime_key_config_binding_rising(
        integral_gb_runtime_key_config_key_from_name(state->keys->escape), event,
        &state->n64.util_escape_held);
    bool screenshot = integral_gb_runtime_key_config_binding_rising(
        integral_gb_runtime_key_config_key_from_name(state->keys->screenshot), event,
        &state->n64.util_screenshot_held);
    /* The background Host parent owns capture, but game-window Escape stays with the child. */
    if (escape && game_controller_input_event(event->type) &&
        strcmp(state->n64.n64_runtime_media_role, "host") == 0 &&
        !SDL_GetKeyboardFocus()) escape = false;
    if (escape) {
        if (state->n64.runtime_exit_confirming) cancel_runtime_exit_confirmation(state);
        else request_runtime_exit_confirmation(state, false);
    }
    if (screenshot) capture_n64_room(state);
    return escape || screenshot;
}


bool handle_runtime_exit_confirmation_event(IntegralRoomContext *state,
                                             const SDL_Event *event)
{
    if (!state->n64.runtime_exit_confirming) return false;
    if (event->type == SDL_QUIT ||
        (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE)) {
        return true;
    }
    bool binding_right = n64_confirmation_binding_pressed(state, event, 0);
    bool binding_left = n64_confirmation_binding_pressed(state, event, 1);
    bool binding_a = n64_confirmation_binding_pressed(state, event, 7);
    if (binding_right || binding_left) {
        state->n64.runtime_exit_confirm_yes = !state->n64.runtime_exit_confirm_yes;
        integral_n64_runtime_media_stream_set_exit_confirmation(
            state->n64.n64_runtime_media_stream, true, state->n64.runtime_exit_confirm_yes);
        return true;
    }
    if (binding_a) {
        if (state->n64.runtime_exit_confirm_yes) confirm_runtime_exit(state);
        else cancel_runtime_exit_confirmation(state);
        return true;
    }
    if (event->type != SDL_KEYDOWN || event->key.repeat)
        return game_controller_input_event(event->type);
    if (event->key.keysym.sym == integral_gb_runtime_key_config_key_from_name(state->keys->escape)) {
        cancel_runtime_exit_confirmation(state);
        return true;
    }
    switch (event->key.keysym.sym) {
        case SDLK_LEFT:
        case SDLK_RIGHT:
            state->n64.runtime_exit_confirm_yes = !state->n64.runtime_exit_confirm_yes;
            integral_n64_runtime_media_stream_set_exit_confirmation(
                state->n64.n64_runtime_media_stream, true, state->n64.runtime_exit_confirm_yes);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->n64.runtime_exit_confirm_yes) confirm_runtime_exit(state);
            else cancel_runtime_exit_confirmation(state);
            break;
        default:
            break;
    }
    return true;
}

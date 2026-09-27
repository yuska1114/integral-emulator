/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_settings.h"

#include "client_config.h"
#include "client_key_config.h"
#include "client_state.h"
#include "client_runtime_support.h"

#include <stdio.h>

static void move_settings_selection(AppState *state, int delta)
{
    int selected = (int)state->settings.settings_selected + delta;
    if (selected < 0) selected = INTEGRAL_SETTINGS_ROWS - 1;
    if (selected >= INTEGRAL_SETTINGS_ROWS) selected = 0;
    state->settings.settings_selected = (unsigned)selected;
}

static void move_options_selection(AppState *state, int delta)
{
    int selected = (int)state->settings.options_selected + delta;
    if (selected < 0) selected = INTEGRAL_OPTIONS_ROWS - 1;
    if (selected >= INTEGRAL_OPTIONS_ROWS) selected = 0;
    state->settings.options_selected = (unsigned)selected;
}

static void enter_options(AppState *state)
{
    state->ui.screen = SCREEN_OPTIONS;
    state->settings.options_selected = 0u;
    state->settings.options_ir_off_delay_ticks = integral_config_ir_off_delay(state->config_path);
    state->settings.options_sgb_enabled = integral_config_sgb_enabled(state->config_path) != 0;
    copy_text(state->login.status, sizeof(state->login.status), "OPTIONS");
}

static void adjust_ir_release_delay(AppState *state, int delta)
{
    IntegralConfigLocal local;
    if (integral_config_load_local(state->config_path, &local) != 0) {
        copy_text(state->login.status, sizeof(state->login.status),
                  "IR RELEASE DELAY LOAD FAILED");
        return;
    }

    unsigned next = state->settings.options_ir_off_delay_ticks;
    if (delta < 0) {
        if (next == 0u) {
            copy_text(state->login.status, sizeof(state->login.status), "IR RELEASE DELAY MIN 0");
            return;
        }
        next--;
    }
    else if (delta > 0) {
        if (next >= 256u) {
            copy_text(state->login.status, sizeof(state->login.status), "IR RELEASE DELAY MAX 256");
            return;
        }
        next++;
    }
    else {
        return;
    }

    local.ir_off_delay_ticks = next;
    if (integral_config_save_local(state->config_path, &local) != 0) {
        copy_text(state->login.status, sizeof(state->login.status),
                  "IR RELEASE DELAY SAVE FAILED");
        return;
    }
    state->settings.options_ir_off_delay_ticks = next;
    snprintf(state->login.status, sizeof(state->login.status),
             "IR RELEASE DELAY %u TICK", next);
}

static void toggle_sgb(AppState *state)
{
    bool enabled = state->settings.options_sgb_enabled;
    if (integral_config_save_sgb(state->config_path, !enabled) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "SGB OPTION SAVE FAILED");
        return;
    }
    state->settings.options_sgb_enabled = !enabled;
    copy_text(state->login.status, sizeof(state->login.status),
              enabled ? "SGB DISABLED" : "SGB ENABLED");
}

void integral_client_settings_handle_settings_key(AppState *state,
                                                  const SDL_KeyboardEvent *key)
{
    if (key->repeat) return;
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->ui.screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_settings_selection(state, 1);
            break;
        case SDLK_UP:
            move_settings_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->settings.settings_selected == 3u) {
                enter_options(state);
            }
            else if (state->settings.settings_selected == INTEGRAL_SETTINGS_ROWS - 1u) {
                state->ui.screen = SCREEN_MAIN_MENU;
                copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            }
            else {
                if (state->settings.settings_selected == 0u) {
                    state->key_editor.page = KEY_CONFIG_PAGE_GB;
                    state->ui.screen = SCREEN_GB_KEY_CONFIG;
                    copy_text(state->login.status, sizeof(state->login.status), "GB KEYS CONFIG");
                }
                else if (state->settings.settings_selected == 1u) {
                    state->key_editor.page = KEY_CONFIG_PAGE_N64;
                    state->ui.screen = SCREEN_N64_KEY_CONFIG;
                    copy_text(state->login.status, sizeof(state->login.status), "N64 KEYS CONFIG");
                }
                else {
                    state->key_editor.page = KEY_CONFIG_PAGE_UTIL;
                    state->ui.screen = SCREEN_UTIL_KEY_CONFIG;
                    copy_text(state->login.status, sizeof(state->login.status), "UTIL KEYS");
                }
                state->key_editor.key_selected = 0;
                if (state->key_editor.page == KEY_CONFIG_PAGE_N64) {
                    state->key_editor.n64_controller_index = 0;
                }
                state->key_editor.key_capture_target = KEY_CAPTURE_NONE;
            }
            break;
        default:
            break;
    }
}

void integral_client_settings_handle_options_key(AppState *state,
                                                const SDL_KeyboardEvent *key)
{
    if (key->repeat) return;
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->ui.screen = SCREEN_SETTINGS;
            state->settings.settings_selected = 3u;
            copy_text(state->login.status, sizeof(state->login.status), "SETTINGS");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_options_selection(state, 1);
            break;
        case SDLK_UP:
            move_options_selection(state, -1);
            break;
        case SDLK_LEFT:
            if (state->settings.options_selected == 0u) adjust_ir_release_delay(state, -1);
            else if (state->settings.options_selected == 1u) toggle_sgb(state);
            break;
        case SDLK_RIGHT:
            if (state->settings.options_selected == 0u) adjust_ir_release_delay(state, 1);
            else if (state->settings.options_selected == 1u) toggle_sgb(state);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->settings.options_selected == 1u) {
                toggle_sgb(state);
            }
            else if (state->settings.options_selected == INTEGRAL_OPTIONS_ROWS - 1u) {
                state->ui.screen = SCREEN_SETTINGS;
                state->settings.settings_selected = 3u;
                copy_text(state->login.status, sizeof(state->login.status), "SETTINGS");
            }
            break;
        default:
            break;
    }
}

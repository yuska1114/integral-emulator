/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_app.h"
#include "client_view.h"
#include "client_local.h"
#include "client_runtime_support.h"
#include "client_log.h"
#include "client_diagnostics.h"
#include "client_version.h"
#include <stdio.h>
#include <string.h>
#include "../runtimes/gb/src/common/key_config.h"

#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#define INTEGRAL_WINDOW_HEIGHT 480

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

int main(int argc, char **argv)
{
    ClientDiagnostics diagnostics;
    int diagnostic_result = client_diagnostics_startup(argc, argv, &diagnostics);
    if (diagnostic_result >= 0) return diagnostic_result;
    AppState state;
    app_state_init(&state, diagnostics.config_path);
    client_log_open(diagnostics.log_path);
    client_log(NULL, "client_start", "version=%s config=%s log=%s", INTEGRAL_CLIENT_VERSION, diagnostics.config_path, diagnostics.log_path);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_init_failed", "error=%s", SDL_GetError());
        client_log_close();
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow("INTEGRAL EMULATOR Login",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          (int)state.ui.window_width,
                                          (int)state.ui.window_height,
                                          SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_window_failed", "error=%s", SDL_GetError());
        SDL_Quit();
        client_log_close();
        return 1;
    }
    state.ui.client_window = window;
    SDL_SetWindowMinimumSize(window,
                             (int)INTEGRAL_CONFIG_WINDOW_MIN_WIDTH,
                             (int)INTEGRAL_CONFIG_WINDOW_MIN_HEIGHT);
    set_application_window_icon(window);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_renderer_failed", "error=%s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    if (SDL_RenderSetLogicalSize(renderer, INTEGRAL_WINDOW_WIDTH, INTEGRAL_WINDOW_HEIGHT) != 0 ||
        SDL_RenderSetIntegerScale(renderer, SDL_FALSE) != 0) {
        fprintf(stderr, "SDL renderer scaling failed: %s\n", SDL_GetError());
        client_log(NULL, "sdl_scale_failed", "error=%s", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }

    if (!integral_room_create_media(&state.room, window, renderer)) {
        fprintf(stderr, "N64 Runtime media stream allocation failed\n");
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    client_diagnostics_configure_media(&state, &diagnostics);
    if (!integral_room_create_poll_worker(&state.room)) {
        fprintf(stderr, "N64 ROOM poll worker allocation failed: %s\n", SDL_GetError());
        integral_room_destroy_resources(&state.room);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    client_log(&state, "app_state_ready", "server=%s remembered=%d", state.login.server, state.login.remember_login ? 1 : 0);
    if (diagnostics.n64_auto.role != N64_ROOM_AUTO_NONE) {
        write_n64_room_auto_status(&diagnostics.n64_auto, "STARTING", &state, "login and room setup");
        if (!start_n64_room_auto(&state, &diagnostics.n64_auto,
                                 diagnostics.n64_auto_error, sizeof(diagnostics.n64_auto_error))) {
            client_log(&state, "n64_auto_failed", "stage=start error=%s", diagnostics.n64_auto_error);
            write_n64_room_auto_status(&diagnostics.n64_auto, "FAILED", &state, diagnostics.n64_auto_error);
            integral_room_destroy_resources(&state.room);
            SDL_DestroyRenderer(renderer);
            SDL_DestroyWindow(window);
            SDL_Quit();
            client_log_close();
            return 1;
        }
    }
    if (diagnostics.smoke_test) client_diagnostics_prepare_screen(&state, argc, argv);
    refresh_rom_metadata_cache(&state, true);
    draw_app(renderer, &state);
    if (diagnostics.screenshot_path && save_screenshot(renderer, diagnostics.screenshot_path) != 0) {
        client_log(&state, "screenshot_failed", "path=%s", diagnostics.screenshot_path);
        integral_screenshots_close(state.screenshots);
        integral_room_destroy_resources(&state.room);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 1;
    }
    if (diagnostics.smoke_test) {
        client_log(&state, "smoke_test_done", "");
        integral_screenshots_close(state.screenshots);
        integral_room_destroy_resources(&state.room);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        client_log_close();
        return 0;
    }

    diagnostics.n64_auto_started_ticks = SDL_GetTicks();
    while (!state.ui.quit) {
        /* The ROOM Host parent owns controller capture even while its child
         * has focus. Other screens retain foreground-only controller events. */
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,
            state.room.n64.n64_runtime_media_host_pid ? "1" : "0");
        (void)set_game_input_active(
            &state, client_game_input_required(&state) && !state.room.n64.runtime_exit_confirming);
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (handle_n64_room_util_event(&state.room, &event)) continue;
            if (handle_runtime_exit_confirmation_event(&state.room, &event)) {
                continue;
            }
            if (event.type == SDL_QUIT && n64_room_runtime_active(&state.room)) {
                request_runtime_exit_confirmation(&state.room, true);
            }
            else if (event.type == SDL_QUIT) {
                client_log(&state, "client_quit_event", "status=%s", state.login.status);
                leave_current_room(&state.room, false);
                state.ui.quit = true;
            }
            else if (event.type == SDL_KEYDOWN) {
                SDL_KeyboardEvent client_key = event.key;
                bool translated = client_alias_enabled(&state) &&
                    integral_client_alias_keyboard_event(&state.keys, &event.key, &client_key);
                if (translated && SDL_IsTextInputActive() &&
                    event.key.keysym.sym >= SDLK_SPACE && event.key.keysym.sym < SDLK_DELETE) {
                    state.ui.suppress_text_input_once = true;
                }
                dispatch_client_key(&state, &client_key);
            }
            else if (state.ui.game_input_active &&
                     (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
                      event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED)) {
                integral_gb_runtime_key_config_handle_device_event(&event);
                if (event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
                    state.room.n64.util_escape_held = state.room.n64.util_screenshot_held = false;
                    state.key_editor.key_capture_wait_release = false;
                    state.key_editor.key_capture_release_binding = SDLK_UNKNOWN;
                    memset(state.ui.client_alias_held, 0, sizeof(state.ui.client_alias_held));
                }
                client_log(&state, "controller_device_changed", "event=%u", event.type);
            }
            else if ((state.ui.screen == SCREEN_GB_KEY_CONFIG ||
                      state.ui.screen == SCREEN_N64_KEY_CONFIG ||
                      state.ui.screen == SCREEN_UTIL_KEY_CONFIG) &&
                     state.key_editor.key_capture_target != KEY_CAPTURE_NONE &&
                     game_controller_input_event(event.type)) {
                handle_key_config_controller_event(&state, &event);
            }
            else if (client_alias_enabled(&state) && game_controller_input_event(event.type)) {
                SDL_KeyboardEvent client_key = {0};
                if (integral_client_alias_controller_event(&state.keys,
                                                           state.ui.client_alias_held,
                                                           &event,
                                                           &client_key)) {
                    dispatch_client_key(&state, &client_key);
                }
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.event == SDL_WINDOWEVENT_CLOSE &&
                     integral_n64_runtime_media_stream_is_video_window(state.room.n64.n64_runtime_media_stream,
                                                                 event.window.windowID)) {
                request_runtime_exit_confirmation(&state.room, false);
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.windowID == SDL_GetWindowID(window) &&
                     event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                clear_local_launch_notice(&state);
            }
            else if (event.type == SDL_WINDOWEVENT &&
                     event.window.windowID == SDL_GetWindowID(window) &&
                     (event.window.event == SDL_WINDOWEVENT_RESIZED ||
                      event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
                int width = 0;
                int height = 0;
                SDL_GetWindowSize(window, &width, &height);
                if (width >= (int)INTEGRAL_CONFIG_WINDOW_MIN_WIDTH &&
                    height >= (int)INTEGRAL_CONFIG_WINDOW_MIN_HEIGHT &&
                    ((unsigned)width != state.ui.window_width ||
                     (unsigned)height != state.ui.window_height)) {
                    state.ui.window_width = (unsigned)width;
                    state.ui.window_height = (unsigned)height;
                    state.ui.window_size_dirty = true;
                }
            }
            else if (event.type == SDL_TEXTINPUT) {
                if (state.ui.suppress_text_input_once) {
                    state.ui.suppress_text_input_once = false;
                    continue;
                }
                if (state.ui.screen == SCREEN_LOGIN && state.login.editing) {
                    handle_text_input(&state.login, &event.text);
                    if (state.login.selected == FIELD_SERVER &&
                        save_login_form_config(&state.login, state.base_config_path) != 0) {
                        copy_text(state.login.status, sizeof(state.login.status), "LOGIN PREF SAVE FAILED");
                    }
                }
                else if (state.ui.screen == SCREEN_PASSWORD_CHANGE) {
                    handle_password_change_text_input(&state, &event.text);
                }
                else if (state.ui.screen == SCREEN_ROM_REGISTER && state.catalog.rom_editor.rom_edit_target != ROM_EDIT_NONE) {
                    handle_rom_text_input(&state, &event.text);
                }
                else if (state.ui.screen == SCREEN_JOIN_ROOM) {
                    handle_join_room_text_input(&state.room, &event.text);
                }
                else if (state.ui.screen == SCREEN_ROOM) {
                    handle_room_text_input(&state.room, &event.text);
                }
            }
            else if (event.type == SDL_TEXTEDITING) {
                if (state.ui.screen == SCREEN_ROOM) {
                    handle_room_text_editing(&state.room, &event.edit);
                }
            }
        }
        refresh_rom_metadata_cache(&state, false);
        poll_server_state(&state.room);
        poll_local_save_notice(&state);
        /* Focus may already be restored before queued events are consumed. */
        if (SDL_GetKeyboardFocus() == window) clear_local_launch_notice(&state);
        client_diagnostics_tick(&state, &diagnostics);
        draw_app(renderer, &state);
        draw_runtime_exit_confirmation(renderer, &state);
        Uint32 delay_ms = main_loop_delay_ms(&state.room);
        if (delay_ms > 0) SDL_Delay(delay_ms);
    }

    (void)set_game_input_active(&state, false);

    SDL_StopTextInput();
    if (state.ui.window_size_dirty) {
        IntegralConfigWindow window_config = {
            .width = state.ui.window_width,
            .height = state.ui.window_height,
        };
        if (integral_config_save_window(state.base_config_path, &window_config) != 0) {
            client_log(&state, "window_size_save_failed", "width=%u height=%u",
                       state.ui.window_width, state.ui.window_height);
        }
    }
    client_log(&state, "client_shutdown", "status=%s", state.login.status);
    leave_current_room(&state.room, false);
    integral_room_destroy_resources(&state.room);
    integral_screenshots_close(state.screenshots);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    client_log_close();
    return diagnostics.n64_auto_exit_code;
}

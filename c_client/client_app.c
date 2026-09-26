/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_app.h"
#include "client_input_alias.h"
#include "client_local.h"
#include "client_runtime_support.h"
#include "client_log.h"
#include "client_user_config.h"
#include "client_rom_registration.h"
#include "client_rom_catalog.h"
#include "client_file_io.h"
#include "client_ui_menu.h"
#include "credential_store.h"
#include "../runtimes/gb/src/common/key_config.h"
#include <stdbool.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#include <io.h>
#include <process.h>
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <SDL.h>


#define INTEGRAL_FIELD_COUNT 6
#define INTEGRAL_PASSWORD_CHANGE_ROWS 3
#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

static size_t field_capacity(LoginField field);
static char *field_value(LoginState *state, LoginField field);
static void activate_user_config(AppState *state);
static IntegralRomRegistration rom_registration_context(AppState *state);
static void enter_rom_register(AppState *state);
static void leave_rom_register(AppState *state);
static void submit_password_change(AppState *app);
static void move_selection(LoginState *state, int delta);
static void handle_key(LoginState *state, const SDL_KeyboardEvent *key);
static void move_password_change_selection(PasswordChangeState *state, int delta);
static char *password_change_value(PasswordChangeState *state);
static void move_main_selection(AppState *state, int delta);
static void apply_selected_rom_slot_to_server(AppState *state,
                                              bool confirm_delete_saves,
                                              bool confirm_initial_save_import);
static void enter_options(AppState *state);

static size_t field_capacity(LoginField field)
{
    switch (field) {
        case FIELD_SERVER:
            return sizeof(((LoginState *)0)->server);
        case FIELD_USERNAME:
            return sizeof(((LoginState *)0)->username);
        case FIELD_PASSWORD:
            return sizeof(((LoginState *)0)->password);
        default:
            return 0;
    }
}


static char *field_value(LoginState *state, LoginField field)
{
    switch (field) {
        case FIELD_SERVER:
            return state->server;
        case FIELD_USERNAME:
            return state->username;
        case FIELD_PASSWORD:
            return state->password;
        default:
            return NULL;
    }
}


void set_application_window_icon(SDL_Window *window)
{
    const char *path = getenv("INTEGRAL_EMULATOR_APP_ICON");
    SDL_Surface *icon;
    if (!window || !path || !path[0]) return;
    icon = SDL_LoadBMP(path);
    if (!icon) {
        client_log(NULL, "app_icon_load_failed", "error=%s", SDL_GetError());
        return;
    }
    SDL_SetWindowIcon(window, icon);
    SDL_FreeSurface(icon);
}


void handle_key_config_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (integral_client_key_editor_keyboard(&state->key_editor, &state->keys,
            state->config_path, state->login.status, sizeof(state->login.status), key)) {
        state->ui.screen = SCREEN_SETTINGS;
        state->ui.settings_selected = (unsigned)state->key_editor.page;
    }
}


void load_active_user_config(AppState *state)
{
    memset(state->catalog.rom_slots, 0, sizeof(state->catalog.rom_slots));
    memset(state->catalog.server_rom_slots, 0, sizeof(state->catalog.server_rom_slots));
    state->local.local_slot_indices[0] = -1;
    state->local.local_slot_indices[1] = -1;
    integral_room_reset_selection(&state->room);

    if (load_user_config(state->config_path, state->catalog.rom_slots, &state->keys,
                          state->local.local_slot_indices) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "CONFIG LOAD FAILED");
    }
    integral_room_validate_selection(&state->room, state->local.local_slot_indices);
    init_n64_runtime_selection(state);
    refresh_rom_metadata_cache(state, true);
}


static void activate_user_config(AppState *state)
{
    build_user_config_path(state->base_config_path,
                           state->login.username,
                           login_server_id_or_default(&state->login),
                           state->config_path,
                           sizeof(state->config_path));
    load_active_user_config(state);
}


void bind_room_context(AppState *state)
{
    state->room.login = &state->login;
    state->room.config_path = state->config_path;
    state->room.keys = &state->keys;
    state->room.rom_slots = state->catalog.rom_slots;
    state->room.server_rom_slots = state->catalog.server_rom_slots;
    state->room.local_slot_indices = state->local.local_slot_indices;
    state->room.screen = &state->ui.screen;
    state->room.quit = &state->ui.quit;
    state->room.ui = state;
}


void app_state_init(AppState *state, const char *config_path)
{
    memset(state, 0, sizeof(*state));
    bind_room_context(state);
    state->ui.screen = SCREEN_LOGIN;
    login_state_init(&state->login);
    password_change_state_init(&state->password_change);
    copy_text(state->base_config_path, sizeof(state->base_config_path), config_path);
    copy_text(state->config_path, sizeof(state->config_path), config_path);
    state->local.local_slot_indices[0] = -1;
    state->local.local_slot_indices[1] = -1;
    integral_room_reset_selection(&state->room);
    IntegralConfigWindow window_config;
    if (integral_config_load_window(state->base_config_path, &window_config) == 0) {
        state->ui.window_width = window_config.width;
        state->ui.window_height = window_config.height;
    }
    init_n64_runtime_selection(state);
    integral_keys_defaults(&state->keys);
    if (integral_config_load_keys(state->base_config_path, &state->keys) == 0) {
        integral_keys_apply_defaults_for_missing(&state->keys);
    }
    load_login_config(&state->login, state->base_config_path);
}


const IntegralConfigRomSlot *registered_rom_slot_at(const AppState *state, int index)
{
    if (index < 0 || index >= INTEGRAL_ROM_SLOTS) {
        return NULL;
    }
    if (state->catalog.rom_slots[index].rom_path[0] == '\0') {
        return NULL;
    }
    if (state->catalog.rom_slots[index].rom_id[0] == '\0' || state->catalog.rom_slots[index].save_id[0] == '\0') {
        return NULL;
    }
    return &state->catalog.rom_slots[index];
}


int read_supported_rom_header(const char *path, IntegralRomMetadata *info)
{
    return integral_rom_metadata_read(path, info);
}


void refresh_rom_metadata_cache(AppState *state, bool force)
{
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        IntegralClientRomMetadataCache *cached = &state->catalog.rom_metadata[i];
        const char *path = state->catalog.rom_slots[i].rom_path;
        if (!force && strcmp(cached->source_path, path) == 0) {
            continue;
        }

        copy_text(cached->source_path, sizeof(cached->source_path), path);
        memset(&cached->metadata, 0, sizeof(cached->metadata));
        cached->file_found = path[0] != '\0' && local_file_exists(path);
        cached->header_valid = path[0] != '\0' &&
                               read_supported_rom_header(path, &cached->metadata) == 0;
    }
}


const IntegralClientRomMetadataCache *client_rom_metadata_for_slot(const AppState *state, int slot_index)
{
    if (slot_index < 0 || slot_index >= INTEGRAL_ROM_SLOTS) return NULL;
    const IntegralConfigRomSlot *slot = &state->catalog.rom_slots[slot_index];
    const IntegralClientRomMetadataCache *cached = &state->catalog.rom_metadata[slot_index];
    return strcmp(cached->source_path, slot->rom_path) == 0 ? cached : NULL;
}


void client_operation_log(void *context, const char *event, const char *detail)
{
    client_log(context, event, "%s", detail);
}


static IntegralRomRegistration rom_registration_context(AppState *state)
{
    return (IntegralRomRegistration){
        .rom_slots = state->catalog.rom_slots, .server_rom_slots = state->catalog.server_rom_slots,
        .editor = &state->catalog.rom_editor, .server = state->login.server,
        .token = state->login.token, .server_id = login_server_id_or_default(&state->login),
        .config_path = state->config_path,
        .allow_user_initial_save_import = state->catalog.allow_user_initial_save_import,
        .status = state->login.status, .status_size = sizeof(state->login.status),
        .log = client_operation_log, .log_context = state,
    };
}


bool refresh_rom_slots_from_server(AppState *state)
{
    IntegralRomRegistration context = rom_registration_context(state);
    bool refreshed = integral_rom_registration_refresh(&context);
    if (refreshed) {
        refresh_rom_metadata_cache(state, true);
    }
    return refreshed;
}


static void enter_rom_register(AppState *state)
{
    IntegralRomRegistration context = rom_registration_context(state);
    integral_rom_registration_enter(&context);
    refresh_rom_metadata_cache(state, true);
    state->ui.screen = SCREEN_ROM_REGISTER;
    integral_client_rom_editor_reset(&state->catalog.rom_editor, true);
    copy_text(state->login.status, sizeof(state->login.status), "ROM REGISTER");
}


static void leave_rom_register(AppState *state)
{
    IntegralRomRegistration context = rom_registration_context(state);
    integral_rom_registration_leave(&context);
    integral_client_rom_editor_reset(&state->catalog.rom_editor, false);
    state->ui.screen = SCREEN_MAIN_MENU;
    copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
}


void submit_login(AppState *app)
{
    LoginState *state = &app->login;
    if (state->server[0] == '\0') {
        copy_text(state->status, sizeof(state->status), "SERVER URL REQUIRED");
        client_log(app, "login_blocked", "reason=missing_server_config");
        return;
    }
    if (state->username[0] == '\0' || state->password[0] == '\0') {
        copy_text(state->status, sizeof(state->status), "LOGIN NEEDS USERNAME PASSWORD");
        client_log(app, "login_blocked", "reason=missing_field server=%s username=%s", state->server, state->username);
        return;
    }
    client_log(app, "login_start", "server=%s server_id=%s username=%s", state->server, login_server_id_or_default(state), state->username);
    char error[160];
    int must_change_password = 0;
    int allow_user_initial_save_import = 0;
    if (!authenticate_login(state, &must_change_password, &allow_user_initial_save_import,
                             error, sizeof(error))) {
        client_log(app, "login_failed", "error=%s", error);
        return;
    }
    app->catalog.allow_user_initial_save_import = allow_user_initial_save_import != 0;
    client_log(app, "login_ok", "must_change_password=%d", must_change_password);
    save_authenticated_login(state, app->base_config_path, must_change_password != 0);
    if (must_change_password) {
        password_change_state_init(&app->password_change);
        copy_text(app->password_change.status, sizeof(app->password_change.status), "PASSWORD CHANGE REQUIRED");
        app->ui.screen = SCREEN_PASSWORD_CHANGE;
        SDL_StopTextInput();
        client_log(app, "password_change_required", "");
        return;
    }
    activate_user_config(app);
    app->ui.screen = SCREEN_MAIN_MENU;
    if (!refresh_rom_slots_from_server(app)) {
        client_log(app, "screen_change", "to=main config=%s rom_slot_sync=failed", app->config_path);
        return;
    }
    IntegralApiRoom current_room;
    int has_current_room = 0;
    if (integral_api_get_current_room(app->login.server,
                                app->login.token,
                                &current_room,
                                &has_current_room,
                                error,
                                sizeof(error)) == 0 && has_current_room) {
        activate_matched_room(&app->room, &current_room);
        copy_text(app->login.status, sizeof(app->login.status), "RETURNED TO ACTIVE ROOM");
        client_log(app, "room_restore_ok", "room=%u mode=%s",
                   current_room.room_number, current_room.room_type);
        return;
    }
    client_log(app, "screen_change", "to=main config=%s", app->config_path);
}


static void submit_password_change(AppState *app)
{
    PasswordChangeState *state = &app->password_change;
    if (!change_account_password(&app->login, state, app->base_config_path)) {
        return;
    }
    password_change_state_init(state);
    activate_user_config(app);
    (void)refresh_rom_slots_from_server(app);
    app->ui.screen = SCREEN_MAIN_MENU;
    SDL_StopTextInput();
}


static void move_selection(LoginState *state, int delta)
{
    int selected = (int)state->selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_FIELD_COUNT - 1;
    }
    if (selected >= INTEGRAL_FIELD_COUNT) {
        selected = 0;
    }
    state->selected = (LoginField)selected;
    state->editing = false;
}


void handle_text_input(LoginState *state, const SDL_TextInputEvent *text)
{
    char *value = field_value(state, state->selected);
    size_t capacity = field_capacity(state->selected);
    if (!value || capacity == 0) {
        return;
    }
    if (!append_ascii_text(value, capacity, text->text)) {
        copy_text(state->status, sizeof(state->status), "ASCII INPUT ONLY");
    }
}


static void handle_key(LoginState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            if (state->editing) {
                state->editing = false;
                SDL_StopTextInput();
                copy_text(state->status, sizeof(state->status), "EDIT CANCELLED");
            }
            else {
                state->quit = true;
            }
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_selection(state, 1);
            SDL_StopTextInput();
            break;
        case SDLK_UP:
            move_selection(state, -1);
            SDL_StopTextInput();
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
            if (state->selected == FIELD_ENV) {
                cycle_login_server(state);
            }
            else if (state->selected == FIELD_REMEMBER) {
                state->remember_login = !state->remember_login;
                copy_text(state->status, sizeof(state->status), state->remember_login ? "REMEMBER LOGIN ON" : "REMEMBER LOGIN OFF");
            }
            break;
        case SDLK_F2:
            if (state->selected == FIELD_REMEMBER) {
                state->remember_login = !state->remember_login;
                copy_text(state->status, sizeof(state->status), state->remember_login ? "REMEMBER LOGIN ON" : "REMEMBER LOGIN OFF");
            }
            else if (state->selected == FIELD_ENV) {
                cycle_login_server(state);
            }
            else if (state->selected != FIELD_ACTION) {
                state->editing = true;
                SDL_StartTextInput();
                copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
            }
            break;
        case SDLK_F3:
            state->password_visible = !state->password_visible;
            break;
        case SDLK_BACKSPACE:
            if (state->editing) {
                char *value = field_value(state, state->selected);
                if (value) {
                    remove_last_char(value);
                }
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->selected == FIELD_ACTION) {
                copy_text(state->status, sizeof(state->status), "PRESS ENTER TO LOGIN");
            }
            else if (state->selected == FIELD_REMEMBER) {
                state->remember_login = !state->remember_login;
                copy_text(state->status, sizeof(state->status), state->remember_login ? "REMEMBER LOGIN ON" : "REMEMBER LOGIN OFF");
            }
            else if (state->selected == FIELD_ENV) {
                cycle_login_server(state);
            }
            else {
                state->editing = !state->editing;
                if (state->editing) {
                    SDL_StartTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
                }
                else {
                    SDL_StopTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT SAVED");
                }
            }
            break;
        default:
            break;
    }
}


void handle_login_key(AppState *app, const SDL_KeyboardEvent *key)
{
    LoginState *state = &app->login;
    if (key->repeat) {
        return;
    }
    if ((key->keysym.sym == SDLK_RETURN || key->keysym.sym == SDLK_KP_ENTER) &&
        state->selected == FIELD_ACTION) {
        submit_login(app);
        return;
    }
    char before_server_id[sizeof(state->server_id)];
    char before_server[sizeof(state->server)];
    char before_username[sizeof(state->username)];
    bool before_remember = state->remember_login;
    copy_text(before_server_id, sizeof(before_server_id), login_server_id_or_default(state));
    copy_text(before_server, sizeof(before_server), state->server);
    copy_text(before_username, sizeof(before_username), state->username);
    handle_key(state, key);
    if (before_remember && !state->remember_login &&
        before_server[0] != '\0' && before_username[0] != '\0' &&
        integral_credential_store_delete(before_server, before_username) != 0) {
        state->remember_login = true;
        copy_text(state->status, sizeof(state->status), "CREDENTIAL DELETE FAILED");
    }
    if (strcasecmp(before_server_id, login_server_id_or_default(state)) != 0 ||
        strcmp(before_server, state->server) != 0 ||
        before_remember != state->remember_login) {
        if (save_login_form_config(&app->login, app->base_config_path) != 0) {
            copy_text(state->status, sizeof(state->status), "LOGIN PREF SAVE FAILED");
        }
    }
    app->ui.quit = state->quit;
}


static void move_password_change_selection(PasswordChangeState *state, int delta)
{
    int selected = (int)state->selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_PASSWORD_CHANGE_ROWS - 1;
    }
    if (selected >= INTEGRAL_PASSWORD_CHANGE_ROWS) {
        selected = 0;
    }
    state->selected = (PasswordChangeField)selected;
    state->editing = false;
}


static char *password_change_value(PasswordChangeState *state)
{
    switch (state->selected) {
        case PASSWORD_CHANGE_NEW:
            return state->new_password;
        case PASSWORD_CHANGE_CONFIRM:
            return state->confirm_password;
        default:
            return NULL;
    }
}


void handle_password_change_key(AppState *app, const SDL_KeyboardEvent *key)
{
    PasswordChangeState *state = &app->password_change;
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_TAB:
        case SDLK_DOWN:
            move_password_change_selection(state, 1);
            SDL_StopTextInput();
            break;
        case SDLK_UP:
            move_password_change_selection(state, -1);
            SDL_StopTextInput();
            break;
        case SDLK_F2:
            if (state->selected != PASSWORD_CHANGE_SAVE) {
                state->editing = true;
                SDL_StartTextInput();
                copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
            }
            break;
        case SDLK_F3:
            state->password_visible = !state->password_visible;
            break;
        case SDLK_BACKSPACE:
            if (state->editing) {
                char *value = password_change_value(state);
                if (value) {
                    remove_last_char(value);
                }
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->selected == PASSWORD_CHANGE_SAVE) {
                submit_password_change(app);
            }
            else {
                state->editing = !state->editing;
                if (state->editing) {
                    SDL_StartTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT ACTIVE");
                }
                else {
                    SDL_StopTextInput();
                    copy_text(state->status, sizeof(state->status), "TEXT INPUT SAVED");
                }
            }
            break;
        default:
            break;
    }
}


void handle_password_change_text_input(AppState *app, const SDL_TextInputEvent *text)
{
    PasswordChangeState *state = &app->password_change;
    if (!state->editing) {
        return;
    }
    char *value = password_change_value(state);
    if (!value) {
        return;
    }
    if (!append_alnum_text(value, sizeof(state->new_password), text->text)) {
        copy_text(state->status, sizeof(state->status), "ALNUM INPUT ONLY");
    }
}


static void move_main_selection(AppState *state, int delta)
{
    int selected = (int)state->ui.main_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_MAIN_ROWS - 1;
    }
    if (selected >= INTEGRAL_MAIN_ROWS) {
        selected = 0;
    }
    state->ui.main_selected = (unsigned)selected;
}


static void move_settings_selection(AppState *state, int delta)
{
    int selected = (int)state->ui.settings_selected + delta;
    if (selected < 0) selected = INTEGRAL_SETTINGS_ROWS - 1;
    if (selected >= INTEGRAL_SETTINGS_ROWS) selected = 0;
    state->ui.settings_selected = (unsigned)selected;
}


void handle_settings_key(AppState *state, const SDL_KeyboardEvent *key)
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
            if (state->ui.settings_selected == 3u) {
                enter_options(state);
            }
            else if (state->ui.settings_selected == INTEGRAL_SETTINGS_ROWS - 1u) {
                state->ui.screen = SCREEN_MAIN_MENU;
                copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            }
            else {
                if (state->ui.settings_selected == 0u) {
                    state->key_editor.page = KEY_CONFIG_PAGE_GB;
                    state->ui.screen = SCREEN_GB_KEY_CONFIG;
                    copy_text(state->login.status, sizeof(state->login.status), "GB KEYS CONFIG");
                }
                else if (state->ui.settings_selected == 1u) {
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


static void move_options_selection(AppState *state, int delta)
{
    int selected = (int)state->ui.options_selected + delta;
    if (selected < 0) selected = INTEGRAL_OPTIONS_ROWS - 1;
    if (selected >= INTEGRAL_OPTIONS_ROWS) selected = 0;
    state->ui.options_selected = (unsigned)selected;
}


static void enter_options(AppState *state)
{
    state->ui.screen = SCREEN_OPTIONS;
    state->ui.options_selected = 0u;
    state->ui.options_ir_off_delay_ticks = integral_config_ir_off_delay(state->config_path);
    state->ui.options_sgb_enabled = integral_config_sgb_enabled(state->config_path) != 0;
    copy_text(state->login.status, sizeof(state->login.status), "OPTIONS");
}


static void adjust_ir_release_delay_option(AppState *state, int delta)
{
    IntegralConfigLocal local;
    if (integral_config_load_local(state->config_path, &local) != 0) {
        copy_text(state->login.status, sizeof(state->login.status),
                  "IR RELEASE DELAY LOAD FAILED");
        return;
    }

    unsigned next = state->ui.options_ir_off_delay_ticks;
    if (delta < 0) {
        if (next == 0u) {
            copy_text(state->login.status, sizeof(state->login.status),
                      "IR RELEASE DELAY MIN 0");
            return;
        }
        next--;
    }
    else if (delta > 0) {
        if (next >= 256u) {
            copy_text(state->login.status, sizeof(state->login.status),
                      "IR RELEASE DELAY MAX 256");
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
    state->ui.options_ir_off_delay_ticks = next;
    snprintf(state->login.status, sizeof(state->login.status),
             "IR RELEASE DELAY %u TICK", next);
}


static void toggle_sgb_option(AppState *state)
{
    bool enabled = state->ui.options_sgb_enabled;
    if (integral_config_save_sgb(state->config_path, !enabled) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "SGB OPTION SAVE FAILED");
        return;
    }
    state->ui.options_sgb_enabled = !enabled;
    copy_text(state->login.status, sizeof(state->login.status),
              enabled ? "SGB DISABLED" : "SGB ENABLED");
}


void handle_options_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) return;
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->ui.screen = SCREEN_SETTINGS;
            state->ui.settings_selected = 3u;
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
            if (state->ui.options_selected == 0u) {
                adjust_ir_release_delay_option(state, -1);
            }
            else if (state->ui.options_selected == 1u) {
                toggle_sgb_option(state);
            }
            break;
        case SDLK_RIGHT:
            if (state->ui.options_selected == 0u) {
                adjust_ir_release_delay_option(state, 1);
            }
            else if (state->ui.options_selected == 1u) {
                toggle_sgb_option(state);
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->ui.options_selected == 1u) {
                toggle_sgb_option(state);
            }
            else if (state->ui.options_selected == INTEGRAL_OPTIONS_ROWS - 1u) {
                state->ui.screen = SCREEN_SETTINGS;
                state->ui.settings_selected = 3u;
                copy_text(state->login.status, sizeof(state->login.status), "SETTINGS");
            }
            break;
        default:
            break;
    }
}


void handle_main_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            leave_current_room(&state->room, false);
            state->ui.screen = SCREEN_LOGIN;
            state->login.selected = FIELD_ACTION;
            copy_text(state->login.status, sizeof(state->login.status), "LOGGED OUT LOCAL UI");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_main_selection(state, 1);
            break;
        case SDLK_UP:
            move_main_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->ui.main_selected == 0) {
                state->ui.screen = SCREEN_LOCAL_MODE;
                state->local.local_mode_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            }
            else if (state->ui.main_selected == 1) {
                state->ui.screen = SCREEN_ROOM_MODE;
                state->room.common.room_mode_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "SELECT ROOM MODE");
            }
            else if (state->ui.main_selected == 2) {
                state->ui.screen = SCREEN_JOIN_ROOM;
                state->room.common.room_code_input[0] = '\0';
                state->room.common.room_code_editing = true;
                SDL_StartTextInput();
                copy_text(state->login.status, sizeof(state->login.status), "ENTER ROOM CODE");
            }
            else if (state->ui.main_selected == 3) {
                enter_rom_register(state);
            }
            else if (state->ui.main_selected == 4) {
                state->ui.screen = SCREEN_SETTINGS;
                state->ui.settings_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "SETTINGS");
            }
            else {
                integral_screenshots_close(state->screenshots);
                state->screenshots = integral_screenshots_open(".");
                if (state->screenshots) state->ui.screen = SCREEN_SCREENSHOTS;
                else copy_text(state->login.status, sizeof(state->login.status), "SCREENSHOTS OPEN FAILED");
            }
            break;
        default:
            break;
    }
}


void handle_key_config_controller_event(AppState *state, const SDL_Event *event)
{
    integral_client_key_editor_controller(&state->key_editor, &state->keys,
        state->config_path, state->login.status, sizeof(state->login.status), event);
}


bool set_game_input_active(AppState *state, bool active)
{
    if (state->ui.game_input_active == active) return true;
    if (active) {
        if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
            client_log(state, "controller_init_failed", "error=%s", SDL_GetError());
            copy_text(state->login.status, sizeof(state->login.status), "CONTROLLER INIT FAILED");
            return false;
        }
        SDL_GameControllerEventState(SDL_ENABLE);
        SDL_JoystickEventState(SDL_ENABLE);
        int opened = integral_gb_runtime_key_config_open_game_controllers();
        state->ui.game_input_active = true;
        client_log(state, "controller_input_enabled", "opened=%d scope=%s", opened,
                   (state->ui.screen == SCREEN_GB_KEY_CONFIG ||
                    state->ui.screen == SCREEN_N64_KEY_CONFIG ||
                    state->ui.screen == SCREEN_UTIL_KEY_CONFIG) ? "key_config" : "game");
        return true;
    }
    integral_gb_runtime_key_config_close_game_controllers();
    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER | SDL_INIT_JOYSTICK);
    state->ui.game_input_active = false;
    client_log(state, "controller_input_disabled", "");
    return true;
}


bool client_game_input_required(const AppState *state)
{
    return state->ui.screen == SCREEN_GB_KEY_CONFIG ||
           state->ui.screen == SCREEN_N64_KEY_CONFIG ||
           state->ui.screen == SCREEN_UTIL_KEY_CONFIG ||
           integral_client_alias_has_controller_binding(&state->keys) ||
           (state->ui.screen == SCREEN_N64_ROOM && state->room.n64.n64_runtime_media_authenticated);
}


static void apply_selected_rom_slot_to_server(AppState *state,
                                              bool confirm_delete_saves,
                                              bool confirm_initial_save_import)
{
    IntegralRomRegistration context = rom_registration_context(state);
    integral_rom_registration_apply(&context, confirm_delete_saves, confirm_initial_save_import);
}


void handle_rom_key(AppState *state, const SDL_KeyboardEvent *key)
{
    IntegralRomEditorAction action = integral_client_rom_editor_key(&state->catalog.rom_editor,
        state->catalog.rom_slots, state->config_path, state->catalog.allow_user_initial_save_import,
        state->login.status, sizeof(state->login.status), key);
    switch (action) {
        case ROM_ACTION_LEAVE: leave_rom_register(state); break;
        case ROM_ACTION_MAIN: state->ui.screen = SCREEN_MAIN_MENU; break;
        case ROM_ACTION_EXPORT: export_registered_saves(state); break;
        case ROM_ACTION_REGISTER: apply_selected_rom_slot_to_server(state, false, false); break;
        case ROM_ACTION_REGISTER_IMPORT: apply_selected_rom_slot_to_server(state, false, true); break;
        case ROM_ACTION_REGISTER_DELETE: apply_selected_rom_slot_to_server(state, true, true); break;
        case ROM_ACTION_NONE: break;
    }
}


void handle_rom_text_input(AppState *state, const SDL_TextInputEvent *text)
{
    integral_client_rom_editor_text(&state->catalog.rom_editor, state->catalog.rom_slots, text);
}


void client_room_log(const IntegralRoomContext *room, const char *event, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    client_log_v(room ? room->ui : NULL, event, fmt, args);
    va_end(args);
}


void client_room_window_size(const IntegralRoomContext *room, unsigned *width, unsigned *height)
{
    gb_launch_window_size(room->ui, width, height);
}


bool client_room_recover(IntegralRoomContext *room, const char *save_id)
{
    return resolve_save_upload_outbox(room->ui, save_id);
}

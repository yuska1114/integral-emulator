/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_APP_H
#define INTEGRAL_CLIENT_APP_H
#include "client_state.h"
#include "rom_metadata.h"

void set_application_window_icon(SDL_Window *window);
void handle_key_config_key(AppState *state, const SDL_KeyboardEvent *key);
void load_active_user_config(AppState *state);
void bind_room_context(AppState *state);
void app_state_init(AppState *state, const char *config_path);
const IntegralConfigRomSlot *registered_rom_slot_at(const AppState *state, int index);
int read_supported_rom_header(const char *path, IntegralRomMetadata *info);
void client_operation_log(void *context, const char *event, const char *detail);
bool refresh_rom_slots_from_server(AppState *state);
void submit_login(AppState *app);
void handle_text_input(LoginState *state, const SDL_TextInputEvent *text);
void handle_login_key(AppState *app, const SDL_KeyboardEvent *key);
void handle_password_change_key(AppState *app, const SDL_KeyboardEvent *key);
void handle_password_change_text_input(AppState *app, const SDL_TextInputEvent *text);
void handle_main_key(AppState *state, const SDL_KeyboardEvent *key);
void handle_settings_key(AppState *state, const SDL_KeyboardEvent *key);
void handle_options_key(AppState *state, const SDL_KeyboardEvent *key);
void handle_key_config_controller_event(AppState *state, const SDL_Event *event);
bool set_game_input_active(AppState *state, bool active);
bool client_game_input_required(const AppState *state);
void handle_rom_key(AppState *state, const SDL_KeyboardEvent *key);
void handle_rom_text_input(AppState *state, const SDL_TextInputEvent *text);
void client_room_log(const IntegralRoomContext *room, const char *event, const char *fmt, ...);
void client_room_window_size(const IntegralRoomContext *room, unsigned *width, unsigned *height);
bool client_room_recover(IntegralRoomContext *room, const char *save_id);
#endif

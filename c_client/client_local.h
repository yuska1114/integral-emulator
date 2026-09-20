/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_LOCAL_H
#define INTEGRAL_CLIENT_LOCAL_H
#include "client_state.h"

void init_n64_runtime_selection(AppState *state);
void clear_local_launch_notice(AppState *state);
void poll_local_save_notice(AppState *state);
const IntegralConfigRomSlot *local_selected_rom_slot(const AppState *state, unsigned local_slot);
const IntegralConfigRomSlot *selected_n64_rom_slot(const AppState *state);
const IntegralConfigRomSlot *selected_transfer_rom_slot(const AppState *state, unsigned transfer_slot);
void gb_launch_window_size(const AppState *state,
                                  unsigned *width_out,
                                  unsigned *height_out);
void handle_local_mode_key(AppState *state, const SDL_KeyboardEvent *key);
void handle_local_key(AppState *state, const SDL_KeyboardEvent *key);
void handle_n64_runtime_key(AppState *state, const SDL_KeyboardEvent *key);
void export_registered_saves(AppState *state);
bool resolve_save_upload_outbox(AppState *state, const char *save_id);
#endif

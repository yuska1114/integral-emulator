/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_SETTINGS_H
#define INTEGRAL_CLIENT_SETTINGS_H

#include <SDL.h>
#include <stdbool.h>

#define INTEGRAL_SETTINGS_ROWS 5
#define INTEGRAL_OPTIONS_ROWS 3

typedef struct IntegralClientSettingsState {
    unsigned settings_selected;
    unsigned options_selected;
    unsigned options_ir_off_delay_ticks;
    bool options_sgb_enabled;
} IntegralClientSettingsState;

struct AppState;

void integral_client_settings_handle_settings_key(struct AppState *state,
                                                  const SDL_KeyboardEvent *key);
void integral_client_settings_handle_options_key(struct AppState *state,
                                                const SDL_KeyboardEvent *key);

#endif

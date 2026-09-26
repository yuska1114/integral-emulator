/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_STATE_H
#define INTEGRAL_CLIENT_STATE_H
#include "client_account.h"
#include "client_key_config.h"
#include "client_rom_editor.h"
#include "client_room_common.h"
#include "client_screenshots.h"

typedef struct {
    AppScreen screen;
    SDL_Window *client_window;
    unsigned window_width;
    unsigned window_height;
    bool window_size_dirty;
    unsigned main_selected;
    unsigned settings_selected;
    unsigned options_selected;
    bool game_input_active;
    bool client_alias_held[INTEGRAL_CLIENT_ALIAS_KEYS];
    bool suppress_text_input_once;
    bool quit;
} IntegralClientUiState;

typedef struct {
    IntegralConfigRomSlot rom_slots[INTEGRAL_ROM_SLOTS];
    IntegralApiRomSlot server_rom_slots[INTEGRAL_ROM_SLOTS];
    IntegralClientRomEditor rom_editor;
    bool allow_user_initial_save_import;
} IntegralClientCatalogState;

typedef struct {
    unsigned local_mode_selected;
    IntegralChildProcess local_monitor;
    bool local_starting;
    Uint32 outbox_checked_ticks;
    char save_sync_notice[64];
    char save_sync_error[160];
    char save_notice_server[160], save_notice_account[64];
    unsigned local_selected;
    int local_slot_indices[2];
    IntegralApiMobileScenario mobile_scenarios[INTEGRAL_API_MOBILE_SCENARIOS_MAX];
    unsigned mobile_scenario_count;
    unsigned mobile_scenario_selected;
    char mobile_scenario_rom_id[96];
    char mobile_scenario_save_id[96];
    unsigned integral_n64_runtime_selected;
    int integral_n64_runtime_n64_slot_index;
    int integral_n64_runtime_transfer_slot_indices[4];
} IntegralClientLocalState;

/* Owns the application state; ROOM borrows members for this object's lifetime. */
typedef struct AppState {
    IntegralScreenshots *screenshots;
    IntegralClientUiState ui;
    IntegralClientCatalogState catalog;
    IntegralClientLocalState local;
    LoginState login;
    PasswordChangeState password_change;
    IntegralConfigKeys keys;
    char base_config_path[160];
    char config_path[160];
    IntegralClientKeyEditor key_editor;
    IntegralRoomContext room;
} AppState;
#endif

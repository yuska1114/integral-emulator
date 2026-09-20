/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_DIAGNOSTICS_H
#define INTEGRAL_CLIENT_DIAGNOSTICS_H
#include "client_state.h"

typedef enum N64RoomAutoRole {
    N64_ROOM_AUTO_NONE,
    N64_ROOM_AUTO_HOST,
    N64_ROOM_AUTO_REMOTE,
} N64RoomAutoRole;

typedef struct N64RoomAutoOptions {
    N64RoomAutoRole role;
    const char *server;
    const char *server_id;
    const char *username;
    const char *password_env;
    const char *room_code;
    const char *status_file;
    unsigned duration_seconds;
    unsigned timeout_seconds;
    bool remote_vsync_enabled;
    const char *remote_renderer_driver;
    bool remote_renderer_driver_set;
} N64RoomAutoOptions;

typedef struct {
    bool smoke_test;
    const char *screenshot_path;
    const char *config_path;
    const char *log_path;
    N64RoomAutoOptions n64_auto;
    char n64_auto_error[160];
    Uint32 n64_auto_started_ticks;
    Uint32 n64_auto_both_present_ticks;
    Uint32 n64_auto_paired_ticks;
    int n64_auto_exit_code;
} ClientDiagnostics;

void write_n64_room_auto_status(const N64RoomAutoOptions *options,
                                       const char *state_name,
                                       const AppState *state,
                                       const char *detail);
bool start_n64_room_auto(AppState *state,
                                const N64RoomAutoOptions *options,
                                char *error,
                                size_t error_size);
int save_screenshot(SDL_Renderer *renderer, const char *path);
int client_diagnostics_startup(int argc, char **argv, ClientDiagnostics *diag);
void client_diagnostics_prepare_screen(AppState *state, int argc, char **argv);
void client_diagnostics_tick(AppState *state, ClientDiagnostics *diag);
void client_diagnostics_configure_media(AppState *state, const ClientDiagnostics *diag);
#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_local.h"
#include "client_ui_menu.h"
#include "client_app.h"
#include "client_log.h"
#include "client_runtime_support.h"
#include "client_launch_gb_local.h"
#include "client_launch_gb_mobile.h"
#include "client_launch_n64_local.h"
#include "client_file_io.h"
#include "client_rom_catalog.h"
#include "client_save_outbox.h"
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

void clear_local_launch_notice(AppState *state)
{
    const char *status = state->login.status;
    if (strcmp(status, "GB_RUNTIME LOCAL STARTED") == 0 ||
        strcmp(status, "GB_RUNTIME SERVER2 STARTED") == 0 ||
        strcmp(status, "MOBILE MODE STARTED") == 0 ||
        strncmp(status, "N64_RUNTIME STARTED TP:", 22) == 0) {
        state->login.status[0] = '\0';
    }
}


#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#define INTEGRAL_WINDOW_HEIGHT 480
#define INTEGRAL_GB_RUNTIME_MODE_ROWS 3
#define INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS 3
#define INTEGRAL_EXPORT_FOLDER "export"
#define INTEGRAL_GB_RUNTIME_PORT "25100"
#define INTEGRAL_MAX_ROM_BYTES (16u * 1024u * 1024u)
#ifdef _WIN32
#ifndef X_OK
#define X_OK 0
#endif
#define getpid _getpid
#define WNOHANG 1
#endif

static void refresh_mobile_scenarios(AppState *state);
static void cycle_mobile_scenario(AppState *state, int delta);
static void cycle_local_rom_slot(AppState *state, unsigned slot_index, int delta);
static void cycle_n64_runtime_n64_slot(AppState *state, int delta);
static void cycle_n64_runtime_transfer_slot(AppState *state, unsigned transfer_slot, int delta);
static void move_local_selection(AppState *state, int delta);
static void move_local_mode_selection(AppState *state, int delta);
static void clear_local_slot(AppState *state);
static int sync_slot_from_server(AppState *state,
                                 const IntegralConfigRomSlot *slot,
                                 const char *session_save_path,
                                 LocalSyncSlot *sync_slot);
static bool server_rtc_offset_text(AppState *state, char *out, size_t out_size);
static int prepare_n64_runtime_transfer_slot(AppState *state,
                                           unsigned transfer_slot,
                                           const char *transfer_dir,
                                           IntegralConfigRomSlot *slot,
                                           LocalSyncSlot *sync_slot);
static int prepare_n64_runtime_n64_save(AppState *state,
                                       const IntegralConfigRomSlot *selected,
                                       const char *n64_save_dir,
                                       LocalSyncSlot *sync_slot);
static bool n64_local_paths(IntegralN64LocalPaths *paths);
static int n64_local_prepare_save(void *context, const IntegralConfigRomSlot *slot,
                                   const char *directory, LocalSyncSlot *sync);
static int n64_local_prepare_transfer(void *context, unsigned index, const char *directory,
                                       IntegralConfigRomSlot *slot, LocalSyncSlot *sync);
static void start_local_n64_runtime(AppState *state);
static void start_local_gb_mobile(AppState *state);
static bool gb_local_recover(void *context, const char *save_id);
static int gb_local_download(void *context, const IntegralConfigRomSlot *slot,
                              const char *path, LocalSyncSlot *sync);
static bool gb_local_rtc(void *context, char *out, size_t size);
static void gb_local_window(void *context, unsigned *width, unsigned *height);
static void start_local_gb_runtime(AppState *state);

void init_n64_runtime_selection(AppState *state)
{
    state->local.integral_n64_runtime_selected = 0;
    state->local.integral_n64_runtime_n64_slot_index = -1;
    for (unsigned i = 0; i < 4; i++) {
        state->local.integral_n64_runtime_transfer_slot_indices[i] = -1;
    }
    for (int i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (slot_is_supported_n64(&state->catalog.rom_slots[i])) {
            state->local.integral_n64_runtime_n64_slot_index = i;
            break;
        }
    }
    for (unsigned i = 0; i < 2; i++) {
        int index = state->local.local_slot_indices[i];
        if (index >= 0 && index < INTEGRAL_ROM_SLOTS && slot_is_supported_gb(&state->catalog.rom_slots[index])) {
            state->local.integral_n64_runtime_transfer_slot_indices[i] = index;
        }
    }
}


const IntegralConfigRomSlot *local_selected_rom_slot(const AppState *state, unsigned local_slot)
{
    if (local_slot >= 2) {
        return NULL;
    }
    int index = state->local.local_slot_indices[local_slot];
    if (index < 0 || index >= INTEGRAL_ROM_SLOTS || !slot_is_supported_gb(&state->catalog.rom_slots[index])) {
        return NULL;
    }
    return registered_rom_slot_at(state, index);
}


const IntegralConfigRomSlot *selected_n64_rom_slot(const AppState *state)
{
    int index = state->local.integral_n64_runtime_n64_slot_index;
    if (index >= 0 && index < INTEGRAL_ROM_SLOTS && slot_is_supported_n64(&state->catalog.rom_slots[index])) {
        return &state->catalog.rom_slots[index];
    }
    return NULL;
}


const IntegralConfigRomSlot *selected_transfer_rom_slot(const AppState *state, unsigned transfer_slot)
{
    if (transfer_slot >= 4) {
        return NULL;
    }
    int index = state->local.integral_n64_runtime_transfer_slot_indices[transfer_slot];
    for (unsigned i = 0; i < 4; i++) {
        if (i != transfer_slot && state->local.integral_n64_runtime_transfer_slot_indices[i] == index) {
            return NULL;
        }
    }
    if (index >= 0 && index < INTEGRAL_ROM_SLOTS && registered_rom_slot_at(state, index) &&
        slot_is_supported_gb(&state->catalog.rom_slots[index])) {
        return &state->catalog.rom_slots[index];
    }
    return NULL;
}


static void refresh_mobile_scenarios(AppState *state)
{
    const IntegralConfigRomSlot *slot = local_selected_rom_slot(state, 0);
    state->local.mobile_scenario_count = 0;
    state->local.mobile_scenario_selected = 0;
    state->local.mobile_scenario_rom_id[0] = '\0';
    state->local.mobile_scenario_save_id[0] = '\0';
    if (!slot || !slot->rom_id[0] || !slot->save_id[0] || !state->login.token[0]) {
        copy_text(state->login.status, sizeof(state->login.status), "SELECT A REGISTERED MOBILE ROM");
        return;
    }
    char error[160];
    if (integral_api_list_mobile_scenarios(
            state->login.server, state->login.token, slot->save_id, slot->rom_id,
            state->local.mobile_scenarios, &state->local.mobile_scenario_count,
            error, sizeof(error)) != 0) {
        client_log(state, "mobile_scenarios_unavailable", "error=%s", error);
        copy_text(state->login.status, sizeof(state->login.status), "NO SCENARIO FOR THIS ROM - SELECT ANOTHER ROM");
        return;
    }
    for (unsigned i = 0; i < state->local.mobile_scenario_count; i++) {
        if (state->local.mobile_scenarios[i].is_default) {
            state->local.mobile_scenario_selected = i;
            break;
        }
    }
    copy_text(state->local.mobile_scenario_rom_id, sizeof(state->local.mobile_scenario_rom_id), slot->rom_id);
    copy_text(state->local.mobile_scenario_save_id, sizeof(state->local.mobile_scenario_save_id), slot->save_id);
    snprintf(state->login.status, sizeof(state->login.status), "SCENARIO %s",
             state->local.mobile_scenarios[state->local.mobile_scenario_selected].display_name);
}


static void cycle_mobile_scenario(AppState *state, int delta)
{
    if (state->local.mobile_scenario_count == 0u) {
        refresh_mobile_scenarios(state);
        return;
    }
    int next = (int)state->local.mobile_scenario_selected + delta;
    if (next < 0) next = (int)state->local.mobile_scenario_count - 1;
    if (next >= (int)state->local.mobile_scenario_count) next = 0;
    state->local.mobile_scenario_selected = (unsigned)next;
    snprintf(state->login.status, sizeof(state->login.status), "SCENARIO %s",
             state->local.mobile_scenarios[state->local.mobile_scenario_selected].display_name);
}


static void cycle_local_rom_slot(AppState *state, unsigned slot_index, int delta)
{
    integral_rom_cycle_local(state->catalog.rom_slots, state->local.local_slot_indices, state->config_path,
        state->login.status, sizeof(state->login.status), slot_index, delta);
}


static void cycle_n64_runtime_n64_slot(AppState *state, int delta)
{
    integral_rom_cycle_n64(state->catalog.rom_slots, &state->local.integral_n64_runtime_n64_slot_index,
        state->login.status, sizeof(state->login.status), delta);
}


static void cycle_n64_runtime_transfer_slot(AppState *state, unsigned transfer_slot, int delta)
{
    integral_rom_cycle_transfer(state->catalog.rom_slots, state->local.integral_n64_runtime_transfer_slot_indices,
        state->login.status, sizeof(state->login.status), transfer_slot, delta);
}


void gb_launch_window_size(const AppState *state,
                                  unsigned *width_out,
                                  unsigned *height_out)
{
    int width = INTEGRAL_WINDOW_WIDTH;
    int height = INTEGRAL_WINDOW_HEIGHT;
    if (state && state->ui.client_window) {
        SDL_GetWindowSize(state->ui.client_window, &width, &height);
    }
    if (width <= 0 || height <= 0) {
        width = INTEGRAL_WINDOW_WIDTH;
        height = INTEGRAL_WINDOW_HEIGHT;
    }
    *width_out = (unsigned)width;
    *height_out = (unsigned)height;
}


static void move_local_selection(AppState *state, int delta)
{
    unsigned row_count = state->ui.screen == SCREEN_GB_MOBILE ? INTEGRAL_GB_RUNTIME_MOBILE_MODE_ROWS
                                                           : INTEGRAL_GB_RUNTIME_MODE_ROWS;
    int selected = (int)state->local.local_selected + delta;
    if (selected < 0) {
        selected = (int)row_count - 1;
    }
    if (selected >= (int)row_count) {
        selected = 0;
    }
    state->local.local_selected = (unsigned)selected;
}


static void move_local_mode_selection(AppState *state, int delta)
{
    int selected = (int)state->local.local_mode_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_LOCAL_MODE_ROWS - 1;
    }
    if (selected >= INTEGRAL_LOCAL_MODE_ROWS) {
        selected = 0;
    }
    state->local.local_mode_selected = (unsigned)selected;
}


void handle_local_mode_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->ui.screen = SCREEN_MAIN_MENU;
            copy_text(state->login.status, sizeof(state->login.status), "MAIN MENU");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_local_mode_selection(state, 1);
            break;
        case SDLK_UP:
            move_local_mode_selection(state, -1);
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->local.local_mode_selected == 0) {
                state->ui.screen = SCREEN_LOCAL;
                state->local.local_selected = 0;
                copy_text(state->login.status, sizeof(state->login.status), "GB MODE");
            }
            else if (state->local.local_mode_selected == 1) {
                state->ui.screen = SCREEN_GB_MOBILE;
                state->local.local_selected = 0;
                refresh_mobile_scenarios(state);
            }
            else {
                init_n64_runtime_selection(state);
                state->ui.screen = SCREEN_N64_RUNTIME;
                copy_text(state->login.status, sizeof(state->login.status), "N64 MODE");
            }
            break;
        default:
            break;
    }
}


static void clear_local_slot(AppState *state)
{
    if (state->local.local_selected < 1 || state->local.local_selected > 2) {
        return;
    }
    unsigned slot_index = state->local.local_selected - 1;
    state->local.local_slot_indices[slot_index] = -1;
    IntegralConfigLocal local = {
        .slot1_index = state->local.local_slot_indices[0],
        .slot2_index = state->local.local_slot_indices[1],
        .ir_off_delay_ticks = integral_config_ir_off_delay(state->config_path),
    };
    if (integral_config_save_local(state->config_path, &local) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "SLOT CLEAR SAVE FAILED");
        return;
    }
    snprintf(state->login.status, sizeof(state->login.status), "SLOT %u CLEARED", slot_index + 1);
}


void handle_local_key(AppState *state, const SDL_KeyboardEvent *key)
{
    bool mobile_mode = state->ui.screen == SCREEN_GB_MOBILE;
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->ui.screen = SCREEN_LOCAL_MODE;
            copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_local_selection(state, 1);
            break;
        case SDLK_UP:
            move_local_selection(state, -1);
            break;
        case SDLK_RIGHT:
            if (state->local.local_selected == 1 || (!mobile_mode && state->local.local_selected == 2)) {
                cycle_local_rom_slot(state, state->local.local_selected - 1, 1);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            else if (mobile_mode && state->local.local_selected == 2) cycle_mobile_scenario(state, 1);
            break;
        case SDLK_LEFT:
            if (state->local.local_selected == 1 || (!mobile_mode && state->local.local_selected == 2)) {
                cycle_local_rom_slot(state, state->local.local_selected - 1, -1);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            else if (mobile_mode && state->local.local_selected == 2) cycle_mobile_scenario(state, -1);
            break;
        case SDLK_BACKSPACE:
            if (!mobile_mode || state->local.local_selected == 1) {
                clear_local_slot(state);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->local.local_selected == 0) {
                if (mobile_mode) {
                    start_local_gb_mobile(state);
                }
                else {
                    start_local_gb_runtime(state);
                }
            }
            else if (state->local.local_selected == 1 || (!mobile_mode && state->local.local_selected == 2)) {
                cycle_local_rom_slot(state, state->local.local_selected - 1, 1);
                if (mobile_mode) refresh_mobile_scenarios(state);
            }
            else if (mobile_mode && state->local.local_selected == 2) cycle_mobile_scenario(state, 1);
            break;
        default:
            break;
    }
}


void handle_n64_runtime_key(AppState *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return;
    }
    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            state->ui.screen = SCREEN_LOCAL_MODE;
            copy_text(state->login.status, sizeof(state->login.status), "SELECT LOCAL MODE");
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            state->local.integral_n64_runtime_selected = (state->local.integral_n64_runtime_selected + 1) % 6;
            break;
        case SDLK_UP:
            state->local.integral_n64_runtime_selected = state->local.integral_n64_runtime_selected == 0 ? 5 : state->local.integral_n64_runtime_selected - 1;
            break;
        case SDLK_RIGHT:
            if (state->local.integral_n64_runtime_selected == 1) {
                cycle_n64_runtime_n64_slot(state, 1);
            }
            else if (state->local.integral_n64_runtime_selected >= 2 && state->local.integral_n64_runtime_selected <= 5) {
                cycle_n64_runtime_transfer_slot(state, state->local.integral_n64_runtime_selected - 2, 1);
            }
            break;
        case SDLK_LEFT:
            if (state->local.integral_n64_runtime_selected == 1) {
                cycle_n64_runtime_n64_slot(state, -1);
            }
            else if (state->local.integral_n64_runtime_selected >= 2 && state->local.integral_n64_runtime_selected <= 5) {
                cycle_n64_runtime_transfer_slot(state, state->local.integral_n64_runtime_selected - 2, -1);
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->local.integral_n64_runtime_selected == 0) {
                start_local_n64_runtime(state);
            }
            else if (state->local.integral_n64_runtime_selected == 1) {
                cycle_n64_runtime_n64_slot(state, 1);
            }
            else if (state->local.integral_n64_runtime_selected >= 2 && state->local.integral_n64_runtime_selected <= 5) {
                cycle_n64_runtime_transfer_slot(state, state->local.integral_n64_runtime_selected - 2, 1);
            }
            break;
        default:
            break;
    }
}


static int sync_slot_from_server(AppState *state,
                                 const IntegralConfigRomSlot *slot,
                                 const char *session_save_path,
                                 LocalSyncSlot *sync_slot)
{
    if (slot->save_id[0] == '\0' || !session_save_path || session_save_path[0] == '\0') {
        return -1;
    }
    if (!resolve_save_upload_outbox(state, slot->save_id)) {
        return -1;
    }
    copy_text(sync_slot->account, sizeof(sync_slot->account), state->login.username);
    return integral_save_download(state->login.server, state->login.token, slot,
                                   session_save_path, sync_slot, state->login.status,
                                   sizeof(state->login.status));
}


void export_registered_saves(AppState *state)
{
    if (state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        return;
    }
    if (ensure_directory(INTEGRAL_EXPORT_FOLDER) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "EXPORT FOLDER CREATE FAILED");
        return;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S", &tm_now);

    char export_dir[INTEGRAL_CONFIG_PATH_MAX];
    if (create_export_directory(INTEGRAL_EXPORT_FOLDER, timestamp, export_dir, sizeof(export_dir)) != 0) {
        copy_text(state->login.status, sizeof(state->login.status), "EXPORT FOLDER CREATE FAILED");
        return;
    }

    unsigned exported = 0;
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        const IntegralApiRomSlot *slot = &state->catalog.server_rom_slots[i];
        if (slot->save_id[0] == '\0') {
            continue;
        }

        unsigned char save_data[INTEGRAL_MAX_SAVE_BYTES];
        size_t save_size = 0;
        int revision = 0;
        char error[160];
        if (integral_api_download_save(state->login.server,
                                  state->login.token,
                                  slot->save_id,
                                  save_data,
                                  sizeof(save_data),
                                  &save_size,
                                  &revision,
                                  error,
                                  sizeof(error)) != 0) {
            snprintf(state->login.status, sizeof(state->login.status), "EXPORT FAILED ROM%u %s", i + 1, error);
            client_log(state, "sav_export_failed", "slot=%u save_id=%s error=%s", i + 1, slot->save_id, error);
            return;
        }

        char filename[128];
        export_save_filename(slot->filename, i, filename, sizeof(filename));
        char path[INTEGRAL_CONFIG_PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", export_dir, filename);
        if (write_binary_file(path, save_data, save_size) != 0) {
            snprintf(state->login.status, sizeof(state->login.status), "EXPORT WRITE FAILED ROM%u", i + 1);
            client_log(state, "sav_export_write_failed", "slot=%u path=%s", i + 1, path);
            return;
        }
        exported++;
        client_log(state, "sav_exported", "slot=%u revision=%d path=%s", i + 1, revision, path);
    }

    if (exported == 0) {
        copy_text(state->login.status, sizeof(state->login.status), "NO REGISTERED SAVS");
        return;
    }
    snprintf(state->login.status, sizeof(state->login.status), "EXPORTED %u SAVS %s", exported, export_dir);
}


bool resolve_save_upload_outbox(AppState *state, const char *save_id)
{
    unsigned pending = 0;
    unsigned replayed = replay_save_upload_outbox(state->login.server,
                                                   state->login.token,
                                                   save_id,
                                                   &pending, state->login.username);
    if (pending > 0) {
        snprintf(state->login.status,
                 sizeof(state->login.status),
                 "OUTBOX RECOVERY REQUIRED %u",
                 pending);
        client_log(state,
                   "save_upload_outbox_blocked",
                   "save_id=%s pending=%u replayed=%u",
                   save_id,
                   pending,
                   replayed);
        return false;
    }
    if (replayed > 0) {
        copy_text(state->local.save_notice_server, sizeof(state->local.save_notice_server), state->login.server);
        copy_text(state->local.save_notice_account, sizeof(state->local.save_notice_account), state->login.username);
        copy_text(state->local.save_sync_notice, sizeof(state->local.save_sync_notice), "SAV SYNC RECOVERED");
        state->local.save_sync_error[0] = '\0';
        snprintf(state->login.status, sizeof(state->login.status), "OUTBOX REPLAYED %u", replayed);
    }
    return true;
}

void poll_local_runtime_monitor(AppState *state)
{
    if (!state->local.local_monitor) return;
#ifdef _WIN32
    DWORD wait_result = WaitForSingleObject((HANDLE)state->local.local_monitor, 0);
    if (wait_result == WAIT_OBJECT_0 || wait_result == WAIT_FAILED) {
        CloseHandle((HANDLE)state->local.local_monitor);
        state->local.local_monitor = 0;
    }
#else
    int status = 0;
    IntegralChildProcess result = waitpid(state->local.local_monitor, &status, WNOHANG);
    if (result == state->local.local_monitor || (result < 0 && errno == ECHILD)) {
        state->local.local_monitor = 0;
    }
#endif
}

void poll_local_save_notice(AppState *state)
{
    if (!state->login.token[0] || strcmp(state->local.save_notice_server, state->login.server) ||
        strcmp(state->local.save_notice_account, state->login.username)) {
        state->local.save_sync_notice[0] = state->local.save_sync_error[0] = '\0';
        copy_text(state->local.save_notice_server, sizeof(state->local.save_notice_server), state->login.server);
        copy_text(state->local.save_notice_account, sizeof(state->local.save_notice_account), state->login.username);
        state->local.outbox_checked_ticks = 0;
        if (!state->login.token[0]) return;
    }
    Uint32 now = SDL_GetTicks();
    if (state->local.outbox_checked_ticks && now - state->local.outbox_checked_ticks < 1000u) return;
    state->local.outbox_checked_ticks = now;
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; ++i) {
        if (save_upload_outbox_pending(state->login.server, state->catalog.server_rom_slots[i].save_id,
                state->login.username, state->local.save_sync_error, sizeof(state->local.save_sync_error))) {
            copy_text(state->local.save_sync_notice, sizeof(state->local.save_sync_notice), "SAV UNSENT - RETRY ON NEXT START");
            return;
        }
    }
    if (strcmp(state->local.save_sync_notice, "SAV UNSENT - RETRY ON NEXT START") == 0)
        state->local.save_sync_notice[0] = '\0';
    state->local.save_sync_error[0] = '\0';
}


static bool server_rtc_offset_text(AppState *state, char *out, size_t out_size)
{
    long long server_time = 0;
    char error[160];
    if (integral_api_get_server_time(state->login.server, &server_time, error, sizeof(error)) != 0) {
        copy_text(out, out_size, "0");
        snprintf(state->login.status, sizeof(state->login.status), "SERVER RTC LOCAL FALLBACK: %.80s", error);
        client_log(state, "server_rtc_failed", "error=%s", error);
        return true;
    }
    long long local_time = (long long)time(NULL);
    long long offset_seconds = server_time - local_time;
    snprintf(out, out_size, "%lld", offset_seconds);
    client_log(state,
               "server_rtc_offset",
               "server=%lld local=%lld offset_seconds=%lld",
               server_time,
               local_time,
               offset_seconds);
    return true;
}


static int prepare_n64_runtime_transfer_slot(AppState *state,
                                           unsigned transfer_slot,
                                           const char *transfer_dir,
                                           IntegralConfigRomSlot *slot,
                                           LocalSyncSlot *sync_slot)
{
    char rom_path[INTEGRAL_CONFIG_PATH_MAX];
    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(rom_path, sizeof(rom_path), "%s/slot%u.gbc", transfer_dir, transfer_slot + 1);
    snprintf(save_path, sizeof(save_path), "%s/slot%u.sav", transfer_dir, transfer_slot + 1);
    if (sync_slot_from_server(state, slot, save_path, sync_slot) != 0) {
        return -1;
    }
    if (copy_binary_file_limited(slot->rom_path, rom_path, INTEGRAL_MAX_ROM_BYTES) != 0) {
        snprintf(state->login.status, sizeof(state->login.status), "TRANSFER SLOT%u PREP FAILED", transfer_slot + 1);
        return -1;
    }
    return 0;
}


static int prepare_n64_runtime_n64_save(AppState *state,
                                       const IntegralConfigRomSlot *selected,
                                       const char *n64_save_dir,
                                       LocalSyncSlot *sync_slot)
{
    if (!selected || selected->save_id[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER N64 ROM FIRST");
        return -1;
    }

    char save_name[96];
    make_safe_n64_runtime_save_name(selected->save_id, save_name, sizeof(save_name));

    char save_path[INTEGRAL_CONFIG_PATH_MAX];
    snprintf(save_path, sizeof(save_path), "%s/%s.sav", n64_save_dir, save_name);
    if (sync_slot_from_server(state, selected, save_path, sync_slot) != 0) {
        return -1;
    }
    return 0;
}


static bool n64_local_paths(IntegralN64LocalPaths *paths)
{
    return integral_n64_runtime_paths(paths->frontend, sizeof(paths->frontend),
        paths->core, sizeof(paths->core), paths->video, sizeof(paths->video),
        paths->audio, sizeof(paths->audio), paths->input, sizeof(paths->input),
        paths->rsp, sizeof(paths->rsp), paths->data, sizeof(paths->data)) &&
        integral_runtime_frontend_access(paths->frontend) == 0;
}


static int n64_local_prepare_save(void *context, const IntegralConfigRomSlot *slot,
                                   const char *directory, LocalSyncSlot *sync)
{
    return prepare_n64_runtime_n64_save(context, slot, directory, sync);
}


static int n64_local_prepare_transfer(void *context, unsigned index, const char *directory,
                                       IntegralConfigRomSlot *slot, LocalSyncSlot *sync)
{
    return prepare_n64_runtime_transfer_slot(context, index, directory, slot, sync);
}


static void start_local_n64_runtime(AppState *state)
{
    if (state->local.local_monitor) {
        int status = 0;
        IntegralChildProcess result = waitpid(state->local.local_monitor, &status, WNOHANG);
        if (result == state->local.local_monitor || (result < 0 && errno == ECHILD)) state->local.local_monitor = 0;
    }
    if (state->local.local_starting || state->local.local_monitor) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ALREADY RUNNING");
        return;
    }
    if (state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "LOGIN TOKEN REQUIRED");
        return;
    }
    if (!refresh_rom_slots_from_server(state)) {
        return;
    }
    char hotkeys[640];
    if (!make_n64_runtime_util_hotkeys(&state->keys, true, hotkeys, sizeof(hotkeys))) {
        copy_text(state->login.status, sizeof(state->login.status), "N64 UTIL KEYS INVALID");
        return;
    }
    const IntegralN64LocalRequest request = {
        .monitor_out = &state->local.local_monitor,
        .hotkeys = hotkeys,
        .slot = selected_n64_rom_slot(state),
        .transfer = {selected_transfer_rom_slot(state, 0), selected_transfer_rom_slot(state, 1),
                     selected_transfer_rom_slot(state, 2), selected_transfer_rom_slot(state, 3)},
        .server = state->login.server, .token = state->login.token, .keys = state->keys.n64_p1,
        .extra_keys = {state->keys.n64_p2, state->keys.n64_p3, state->keys.n64_p4},
        .status = state->login.status, .status_size = sizeof(state->login.status),
        .context = state, .paths = n64_local_paths, .recover = gb_local_recover,
        .prepare_save = n64_local_prepare_save, .prepare_transfer = n64_local_prepare_transfer,
        .keymap = make_n64_runtime_keymap_spec,
        .redirect_output = redirect_child_output_to_client_log, .log = client_operation_log,
    };
    state->local.local_starting = true;
    integral_n64_local_run(&request);
    state->local.local_starting = false;
}


static void start_local_gb_mobile(AppState *state)
{
    if (state->local.local_monitor) {
        int status = 0;
        IntegralChildProcess result = waitpid(state->local.local_monitor, &status, WNOHANG);
        if (result == state->local.local_monitor || (result < 0 && errno == ECHILD)) state->local.local_monitor = 0;
    }
    if (state->local.local_starting || state->local.local_monitor) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ALREADY RUNNING");
        return;
    }
    const IntegralConfigRomSlot *selected = local_selected_rom_slot(state, 0);
    if (!selected) {
        copy_text(state->login.status, sizeof(state->login.status), "SLOT1 ROM REQUIRED");
        return;
    }
    if (selected->save_id[0] == '\0' || state->login.token[0] == '\0') {
        copy_text(state->login.status, sizeof(state->login.status), "REGISTER SLOT1 AND LOGIN FIRST");
        return;
    }
    if (state->local.mobile_scenario_count == 0u ||
        state->local.mobile_scenario_selected >= state->local.mobile_scenario_count ||
        strcmp(state->local.mobile_scenario_rom_id, selected->rom_id) != 0 ||
        strcmp(state->local.mobile_scenario_save_id, selected->save_id) != 0) {
        refresh_mobile_scenarios(state);
        if (state->local.mobile_scenario_count == 0u) {
            return;
        }
    }
    const IntegralApiMobileScenario *scenario =
        &state->local.mobile_scenarios[state->local.mobile_scenario_selected];
    const IntegralGbMobileRequest request = {
        .monitor_out = &state->local.local_monitor,
        .slot = selected, .scenario_id = scenario->scenario_id,
        .runtime = integral_gb_runtime_mobile_runtime_path(),
        .server = state->login.server, .token = state->login.token,
        .keys = &state->keys, .status = state->login.status,
        .status_size = sizeof(state->login.status), .context = state,
        .recover = gb_local_recover, .download = gb_local_download,
        .rtc = gb_local_rtc, .window_size = gb_local_window,
        .cleanup = cleanup_mobile_runtime_directory,
        .redirect_output = redirect_child_output_to_client_log,
        .log = client_operation_log,
    };
    state->local.local_starting = true;
    integral_gb_mobile_run(&request);
    state->local.local_starting = false;
}


static bool gb_local_recover(void *context, const char *save_id)
{
    return resolve_save_upload_outbox(context, save_id);
}


static int gb_local_download(void *context, const IntegralConfigRomSlot *slot,
                              const char *path, LocalSyncSlot *sync)
{
    return sync_slot_from_server(context, slot, path, sync);
}


static bool gb_local_rtc(void *context, char *out, size_t size)
{
    return server_rtc_offset_text(context, out, size);
}


static void gb_local_window(void *context, unsigned *width, unsigned *height)
{
    gb_launch_window_size(context, width, height);
}


static void gb_local_game_window(void *context, unsigned *width, unsigned *height)
{
    AppState *state = context;
    gb_launch_window_size(state, width, height);
    if (!local_selected_rom_slot(state, 1)) return;
    SDL_Rect usable;
    unsigned available_width = 0u, available_height = 0u;
    /* GB Runtime currently creates its window centered on display 0. */
    if (SDL_GetDisplayUsableBounds(0, &usable) == 0) {
        int top = 0, left = 0, bottom = 0, right = 0;
        if (state->ui.client_window) {
            (void)SDL_GetWindowBordersSize(state->ui.client_window, &top, &left, &bottom, &right);
        }
        if (usable.w - left - right >= 320 && usable.h - top - bottom >= 144) {
            available_width = (unsigned)(usable.w - left - right);
            available_height = (unsigned)(usable.h - top - bottom);
        }
    }
    integral_gb_local_pair_window_size(*width, *height, available_width, available_height,
                                       width, height);
}

static void start_local_gb_runtime(AppState *state)
{
    poll_local_runtime_monitor(state);
    if (state->local.local_starting || state->local.local_monitor) {
        copy_text(state->login.status, sizeof(state->login.status), "GAME ALREADY RUNNING");
        return;
    }
    const IntegralGbLocalRequest request = {
        .monitor_out = &state->local.local_monitor,
        .config_path = state->config_path,
        .slot1 = local_selected_rom_slot(state, 0),
        .slot2 = local_selected_rom_slot(state, 1),
        .runtime = integral_gb_runtime_server_path(), .port = INTEGRAL_GB_RUNTIME_PORT,
        .server = state->login.server, .token = state->login.token,
        .keys = &state->keys, .status = state->login.status,
        .status_size = sizeof(state->login.status), .context = state,
        .recover = gb_local_recover, .download = gb_local_download,
        .rtc = gb_local_rtc, .window_size = gb_local_game_window, .log = client_operation_log,
    };
    state->local.local_starting = true;
    integral_gb_local_run(&request);
    state->local.local_starting = false;
}

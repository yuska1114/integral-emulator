/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_rom_registration.h"
#include "client_rom_catalog.h"
#include "client_file_io.h"
#include "rom_metadata.h"
#include "../runtimes/gb/src/server/slot.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

static const char *path_file_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *backslash = strrchr(path, '\\');
    const char *separator = slash;
    if (backslash && (!separator || backslash > separator)) {
        separator = backslash;
    }
    return separator ? separator + 1 : path;
}

static void registration_log(IntegralRomRegistration *state, const char *event, const char *format, ...)
{
    if (!state->log) return;
    char detail[INTEGRAL_CONFIG_PATH_MAX + 256];
    va_list args;
    va_start(args, format);
    vsnprintf(detail, sizeof(detail), format, args);
    va_end(args);
    state->log(state->log_context, event, detail);
}

bool integral_rom_registration_refresh(IntegralRomRegistration *state)
{
    if (state->token[0] == '\0') {
        return false;
    }
    IntegralApiRomSlot server_slots[INTEGRAL_ROM_SLOTS];
    char error[160];
    if (integral_api_get_rom_slots(state->server,
                              state->token,
                              server_slots,
                              INTEGRAL_ROM_SLOTS,
                              error,
                              sizeof(error)) != 0) {
        snprintf(state->status, state->status_size, "ROM SLOT SYNC FAILED %s", error);
        registration_log(state, "rom_slots_sync_failed", "error=%s", error);
        return false;
    }
    memcpy(state->server_rom_slots, server_slots, sizeof(IntegralApiRomSlot) * INTEGRAL_ROM_SLOTS);
    integral_client_rom_editor_discard(state->editor, state->rom_slots);
    merge_server_rom_slots(state->rom_slots, server_slots, INTEGRAL_ROM_FOLDER);
    IntegralConfigRomSlot confirmed[INTEGRAL_ROM_SLOTS];
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++)
        confirmed[i] = state->editor->pending_paths[i] ? state->editor->confirmed_slots[i] : state->rom_slots[i];
    if (integral_config_save_rom_slots(state->config_path, confirmed, INTEGRAL_ROM_SLOTS) != 0) {
        copy_text(state->status, state->status_size, "ROM SLOT SYNC SAVE FAILED");
        return false;
    }
    registration_log(state, "rom_slots_synced", "server=%s", state->server_id);
    return true;
}

void integral_rom_registration_enter(IntegralRomRegistration *state)
{
    integral_client_rom_editor_discard(state->editor, state->rom_slots);
    (void)discard_pending_rom_slots(state->rom_slots);
    (void)integral_rom_registration_refresh(state);
}

void integral_rom_registration_leave(IntegralRomRegistration *state)
{
    integral_client_rom_editor_discard(state->editor, state->rom_slots);
    bool changed = discard_pending_rom_slots(state->rom_slots);
    merge_server_rom_slots(state->rom_slots, state->server_rom_slots, INTEGRAL_ROM_FOLDER);
    if (changed) {
        (void)integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS);
    }
}

void integral_rom_registration_apply(IntegralRomRegistration *state,
                                              bool confirm_delete_saves,
                                              bool confirm_initial_save_import)
{
    unsigned slot_index = state->editor->rom_selected;
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
            if (state->rom_slots[i].rom_path[0] != '\0' &&
                (state->editor->pending_paths[i] || state->rom_slots[i].rom_id[0] == '\0')) {
                slot_index = i;
                break;
            }
        }
    }
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        copy_text(state->status, state->status_size, "NO ROM CHANGES");
        return;
    }
    IntegralConfigRomSlot *slot = &state->rom_slots[slot_index];
    if (slot->rom_path[0] == '\0') {
        copy_text(state->status, state->status_size, "ROM REQUIRED");
        return;
    }
    IntegralRomMetadata header;
    if (!local_file_exists(slot->rom_path)) {
        copy_text(state->status, state->status_size, "FILE NOT FOUND - EDIT ROM PATH");
        return;
    }
    if (integral_rom_metadata_read(slot->rom_path, &header) != 0) {
        copy_text(state->status, state->status_size, "SUPPORTED ROM REQUIRED");
        return;
    }
    if (state->token[0] == '\0') {
        copy_text(state->status, state->status_size, "LOGIN TOKEN REQUIRED");
        return;
    }
    char sha256[65];
    if (sha256_file_hex(slot->rom_path, sha256, sizeof(sha256)) != 0) {
        copy_text(state->status, state->status_size, "ROM FILE READ FAILED");
        return;
    }
    char sha1[41];
    if (sha1_file_hex(slot->rom_path, sha1, sizeof(sha1)) != 0) {
        copy_text(state->status, state->status_size, "ROM FILE READ FAILED");
        return;
    }
    char error[160];
    char registered_rom_id[INTEGRAL_CONFIG_ID_MAX] = {0};
    char registered_save_id[INTEGRAL_CONFIG_ID_MAX] = {0};
    int requires_confirmation = 0;
    unsigned char initial_save_data[INTEGRAL_MAX_SAVE_BYTES];
    unsigned char *initial_save_ptr = NULL;
    size_t initial_save_size = 0;
    bool initial_save_generated = false;
    bool import_selected = state->allow_user_initial_save_import &&
                           state->editor->rom_initial_save_import_slot == (int)slot_index;
    const IntegralApiRomSlot *server_slot = &state->server_rom_slots[slot_index];
    if (import_selected && server_slot->save_id[0] != '\0' &&
        strcmp(server_slot->sha256, sha256) == 0) {
        state->editor->rom_confirm_initial_save_import = false;
        copy_text(state->status, state->status_size,
                  "INITIAL SAV REJECTED USE ADMIN SAV REPLACE");
        return;
    }
    if (import_selected && !confirm_initial_save_import) {
        state->editor->rom_confirm_initial_save_import = true;
        copy_text(state->status, state->status_size, "INITIAL SAV SEND CONFIRM REQUIRED");
        return;
    }
    if (import_selected) {
        if (state->editor->rom_initial_save_import_path[0] == '\0' ||
            read_binary_file(state->editor->rom_initial_save_import_path, initial_save_data,
                             sizeof(initial_save_data), &initial_save_size) != 0 ||
            initial_save_size == 0) {
            state->editor->rom_confirm_initial_save_import = false;
            copy_text(state->status, state->status_size, "SELECTED INITIAL SAV READ FAILED");
            return;
        }
        initial_save_ptr = initial_save_data;
        registration_log(state, "rom_slot_initial_save_import_confirmed",
                   "slot=%u path=%s bytes=%zu", slot_index + 1,
                   state->editor->rom_initial_save_import_path, initial_save_size);
    }
    if (!import_selected &&
        strcmp(header.platform, "gb") == 0 &&
        !(server_slot->save_id[0] != '\0' && strcmp(server_slot->sha256, sha256) == 0)) {
        if (integral_gb_runtime_initial_battery_for_rom(
                slot->rom_path,
                initial_save_data,
                sizeof(initial_save_data),
                &initial_save_size) != 0) {
            copy_text(state->status, state->status_size, "INITIAL SAV GENERATION FAILED");
            return;
        }
        initial_save_ptr = initial_save_data;
        initial_save_generated = true;
        registration_log(state, "rom_slot_initial_save_generated",
                         "slot=%u bytes=%zu", slot_index + 1, initial_save_size);
    }
    if (integral_api_apply_rom_slot(state->server,
                               state->token,
                               slot_index + 1,
                               path_file_name(slot->rom_path),
                               sha256,
                               sha1,
                               header.platform,
                               header.region,
                               header.header_title,
                               initial_save_ptr,
                               initial_save_size,
                               initial_save_generated ? 1 : 0,
                               confirm_delete_saves ? 1 : 0,
                               registered_rom_id,
                               sizeof(registered_rom_id),
                               registered_save_id,
                               sizeof(registered_save_id),
                               &requires_confirmation,
                               error,
                               sizeof(error)) != 0) {
        snprintf(state->status, state->status_size, "REGISTER FAILED %s", error);
        return;
    }
    if (requires_confirmation) {
        state->editor->rom_selected = slot_index;
        state->editor->rom_confirm_initial_save_import = false;
        state->editor->rom_confirm_delete = true;
        copy_text(state->status, state->status_size, "SAV DELETE CONFIRM REQUIRED");
        return;
    }
    state->editor->rom_confirm_delete = false;
    copy_text(slot->rom_id, sizeof(slot->rom_id), registered_rom_id);
    copy_text(slot->save_id, sizeof(slot->save_id), registered_save_id);
    state->editor->pending_paths[slot_index] = false;
    integral_client_rom_editor_discard(state->editor, state->rom_slots);
    state->editor->rom_confirm_initial_save_import = false;
    state->editor->rom_initial_save_import_slot = -1;
    state->editor->rom_initial_save_import_path[0] = '\0';
    if (integral_config_save_rom_slots(state->config_path, state->rom_slots, INTEGRAL_ROM_SLOTS) != 0) {
        copy_text(state->status, state->status_size, "REGISTERED CONFIG SAVE FAILED");
        return;
    }
    if (!integral_rom_registration_refresh(state)) {
        copy_text(state->status, state->status_size, "REGISTERED BUT SYNC FAILED");
        return;
    }
    snprintf(state->status, state->status_size, "ROM%u REGISTERED", slot_index + 1);
}

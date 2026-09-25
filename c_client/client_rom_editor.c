/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_rom_editor.h"
#include "../runtimes/gb/src/common/utf8_file.h"
#include "client_rom_catalog.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

typedef struct RomEditContext {
    IntegralClientRomEditor *editor;
    IntegralConfigRomSlot *rom_slots;
    const char *config_path;
    bool allow_user_initial_save_import;
    char *status;
    size_t status_size;
} RomEditContext;

static void preserve_registration(RomEditContext *state, unsigned index)
{
    if (!state->editor->pending_paths[index]) {
        state->editor->confirmed_slots[index] = state->rom_slots[index];
        state->editor->pending_paths[index] = true;
    }
}

void integral_client_rom_editor_discard(IntegralClientRomEditor *editor, IntegralConfigRomSlot *slots)
{
    for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++) {
        if (editor->pending_paths[i]) slots[i] = editor->confirmed_slots[i];
        editor->pending_paths[i] = false;
    }
    editor->rom_initial_save_import_slot = -1;
    editor->rom_initial_save_import_path[0] = '\0';
}

void integral_client_rom_editor_reset(IntegralClientRomEditor *editor, bool select_first)
{
    if (select_first) editor->rom_selected = 0;
    editor->rom_edit_target = ROM_EDIT_NONE;
    editor->rom_browser_active = false;
    editor->rom_confirm_delete = false;
    editor->rom_confirm_initial_save_import = false;
    editor->rom_initial_save_import_slot = -1;
    editor->rom_initial_save_import_path[0] = '\0';
}

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

static void append_text(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(dest);
    if (len >= dest_size) {
        return;
    }
    copy_text(dest + len, dest_size - len, src);
}

static void remove_last_char(char *text)
{
    size_t len = strlen(text);
    if (len > 0) {
        do { len--; } while (len && ((unsigned char)text[len] & 0xc0) == 0x80);
        text[len] = '\0';
    }
}

static bool local_regular_file(const char *path)
{
    return path && path[0] && integral_file_regular(path);
}

static int scan_rom_folder(RomEditContext *state)
{
    state->editor->rom_browser_count = 0;
    state->editor->rom_browser_selected = 0;
    int count = scan_rom_paths(INTEGRAL_ROM_FOLDER, state->editor->rom_browser_entries,
                               INTEGRAL_ROM_BROWSER_MAX);
    if (count < 0) {
        copy_text(state->status, state->status_size, "PUT ROMS IN ./roms THEN PRESS F4");
        return -1;
    }
    state->editor->rom_browser_count = (unsigned)count;
    if (state->editor->rom_browser_count == 0) {
        copy_text(state->status, state->status_size, "NO ROM FILES IN ./roms");
        return -1;
    }
    state->editor->rom_browser_active = true;
    copy_text(state->status, state->status_size, "SELECT ROM FROM FOLDER");
    return 0;
}

static void apply_rom_path_to_slot(RomEditContext *state, const char *path)
{
    if (state->editor->rom_selected >= INTEGRAL_ROM_SLOTS || state->editor->rom_browser_count == 0) {
        return;
    }
    IntegralConfigRomSlot *slot = &state->rom_slots[state->editor->rom_selected];
    preserve_registration(state, state->editor->rom_selected);
    copy_text(slot->rom_path, sizeof(slot->rom_path), path);
    if (state->editor->rom_initial_save_import_slot == (int)state->editor->rom_selected) {
        state->editor->rom_initial_save_import_slot = -1;
        state->editor->rom_initial_save_import_path[0] = '\0';
    }
    snprintf(state->status, state->status_size, "ROM%u LOCAL READY", state->editor->rom_selected + 1);
}

static void apply_rom_path_to_slot_index(RomEditContext *state, unsigned slot_index, const char *path)
{
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        return;
    }
    IntegralConfigRomSlot *slot = &state->rom_slots[slot_index];
    preserve_registration(state, slot_index);
    copy_text(slot->rom_path, sizeof(slot->rom_path), path);
    if (state->editor->rom_initial_save_import_slot == (int)slot_index) {
        state->editor->rom_initial_save_import_slot = -1;
        state->editor->rom_initial_save_import_path[0] = '\0';
    }
    snprintf(state->status, state->status_size, "ROM%u LOCAL READY", slot_index + 1);
}

static void apply_browser_rom(RomEditContext *state)
{
    if (state->editor->rom_browser_count == 0) {
        return;
    }
    const char *path = state->editor->rom_browser_entries[state->editor->rom_browser_selected];
    state->editor->rom_browser_active = false;
    apply_rom_path_to_slot(state, path);
}

static void cycle_rom_for_selected_slot(RomEditContext *state, int delta)
{
    unsigned slot_index = state->editor->rom_selected;
    if (slot_index >= INTEGRAL_ROM_SLOTS) {
        return;
    }
    bool was_active = state->editor->rom_browser_active;
    if (scan_rom_folder(state) != 0 || state->editor->rom_browser_count == 0) {
        return;
    }
    state->editor->rom_browser_active = was_active;

    int current = -1;
    const char *slot_path = state->rom_slots[slot_index].rom_path;
    for (unsigned i = 0; i < state->editor->rom_browser_count; i++) {
        if (strcmp(slot_path, state->editor->rom_browser_entries[i]) == 0) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)state->editor->rom_browser_count - 1 : 0;
    }
    if (next < 0) {
        next = (int)state->editor->rom_browser_count - 1;
    }
    if (next >= (int)state->editor->rom_browser_count) {
        next = 0;
    }
    apply_rom_path_to_slot_index(state, slot_index, state->editor->rom_browser_entries[next]);
}

static void move_rom_selection(RomEditContext *state, int delta)
{
    int selected = (int)state->editor->rom_selected + delta;
    if (selected < 0) {
        selected = INTEGRAL_ROM_ROWS - 1;
    }
    if (selected >= INTEGRAL_ROM_ROWS) {
        selected = 0;
    }
    state->editor->rom_selected = (unsigned)selected;
}

static char *rom_edit_value(RomEditContext *state)
{
    if (state->editor->rom_selected >= INTEGRAL_ROM_SLOTS) {
        return NULL;
    }
    if (state->editor->rom_edit_target == ROM_EDIT_ROM) {
        return state->rom_slots[state->editor->rom_selected].rom_path;
    }
    if (state->editor->rom_edit_target == ROM_EDIT_INITIAL_SAVE) {
        return state->editor->rom_initial_save_import_path;
    }
    return NULL;
}

static size_t rom_edit_capacity(RomEditContext *state)
{
    if (state->editor->rom_edit_target == ROM_EDIT_ROM) {
        return sizeof(state->rom_slots[state->editor->rom_selected].rom_path);
    }
    if (state->editor->rom_edit_target == ROM_EDIT_INITIAL_SAVE) {
        return sizeof(state->editor->rom_initial_save_import_path);
    }
    return 0;
}

static bool begin_rom_edit(RomEditContext *state, RomEditTarget target)
{
    if (state->editor->rom_selected >= INTEGRAL_ROM_SLOTS) {
        state->editor->rom_edit_target = ROM_EDIT_NONE;
        SDL_StopTextInput();
        return false;
    }
    if (target == ROM_EDIT_ROM &&
        state->editor->rom_initial_save_import_slot == (int)state->editor->rom_selected) {
        state->editor->rom_initial_save_import_slot = -1;
        state->editor->rom_initial_save_import_path[0] = '\0';
    }
    state->editor->rom_edit_target = target;
    if (target == ROM_EDIT_ROM) preserve_registration(state, state->editor->rom_selected);
    copy_text(state->editor->rom_edit_original, sizeof(state->editor->rom_edit_original), rom_edit_value(state));
    SDL_StartTextInput();
    copy_text(state->status,
              state->status_size,
              target == ROM_EDIT_INITIAL_SAVE ? "EDITING INITIAL SAV PATH" : "EDITING ROM PATH");
    return true;
}

static void finish_rom_edit(RomEditContext *state)
{
    unsigned index = state->editor->rom_selected;
    if (state->editor->rom_edit_target == ROM_EDIT_ROM && index < INTEGRAL_ROM_SLOTS &&
        state->editor->pending_paths[index] &&
        !strcmp(state->rom_slots[index].rom_path, state->editor->confirmed_slots[index].rom_path))
        state->editor->pending_paths[index] = false;
    if (state->editor->rom_edit_target == ROM_EDIT_INITIAL_SAVE) {
        if (state->editor->rom_initial_save_import_path[0] == '\0' ||
            !local_regular_file(state->editor->rom_initial_save_import_path)) {
            state->editor->rom_initial_save_import_slot = -1;
            state->editor->rom_initial_save_import_path[0] = '\0';
            copy_text(state->status, state->status_size, "INITIAL SAV FILE REQUIRED");
        }
        else {
            state->editor->rom_initial_save_import_slot = (int)state->editor->rom_selected;
            copy_text(state->status, state->status_size, "INITIAL SAV SELECTED  REGISTER TO CONFIRM");
        }
    }
    else if (state->editor->rom_selected < INTEGRAL_ROM_SLOTS &&
             strcmp(state->editor->rom_edit_original, state->rom_slots[state->editor->rom_selected].rom_path) != 0) {
        IntegralConfigRomSlot *slot = &state->rom_slots[state->editor->rom_selected];
        copy_text(state->status, state->status_size, local_regular_file(slot->rom_path)
                  ? "PATH EDITED - REGISTER TO APPLY" : "FILE NOT FOUND - EDIT ROM PATH");
    }
    else if (state->editor->rom_selected < INTEGRAL_ROM_SLOTS && slot_has_server_registration(&state->rom_slots[state->editor->rom_selected])) {
        IntegralConfigRomSlot confirmed[INTEGRAL_ROM_SLOTS];
        for (unsigned i = 0; i < INTEGRAL_ROM_SLOTS; i++)
            confirmed[i] = state->editor->pending_paths[i] ? state->editor->confirmed_slots[i] : state->rom_slots[i];
        if (integral_config_save_rom_slots(state->config_path, confirmed, INTEGRAL_ROM_SLOTS) != 0) {
            copy_text(state->status, state->status_size, "LOCAL ROM PATH SAVE FAILED");
        }
        else {
            copy_text(state->status, state->status_size, "LOCAL ROM PATH SAVED");
        }
    }
    else {
        copy_text(state->status, state->status_size, "LOCAL READY");
    }
    state->editor->rom_edit_target = ROM_EDIT_NONE;
    SDL_StopTextInput();
}

static IntegralRomEditorAction handle_rom_key(RomEditContext *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return ROM_ACTION_NONE;
    }
    if (state->editor->rom_confirm_initial_save_import) {
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                state->editor->rom_confirm_initial_save_import = false;
                copy_text(state->status, state->status_size, "REGISTER CANCELED");
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                state->editor->rom_confirm_initial_save_import = false;
                return ROM_ACTION_REGISTER_IMPORT;
                break;
            default:
                break;
        }
        return ROM_ACTION_NONE;
    }
    if (state->editor->rom_confirm_delete) {
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                state->editor->rom_confirm_delete = false;
                integral_client_rom_editor_discard(state->editor, state->rom_slots);
                copy_text(state->status, state->status_size, "REGISTER CANCELED");
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                return ROM_ACTION_REGISTER_DELETE;
                break;
            default:
                break;
        }
        return ROM_ACTION_NONE;
    }
    if (state->editor->rom_edit_target != ROM_EDIT_NONE) {
        char *value = rom_edit_value(state);
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                if (value) copy_text(value, rom_edit_capacity(state), state->editor->rom_edit_original);
                if (state->editor->rom_edit_target == ROM_EDIT_ROM &&
                    state->editor->rom_selected < INTEGRAL_ROM_SLOTS &&
                    state->editor->pending_paths[state->editor->rom_selected] &&
                    !strcmp(state->rom_slots[state->editor->rom_selected].rom_path,
                            state->editor->confirmed_slots[state->editor->rom_selected].rom_path)) {
                    state->editor->pending_paths[state->editor->rom_selected] = false;
                }
                state->editor->rom_edit_target = ROM_EDIT_NONE;
                SDL_StopTextInput();
                copy_text(state->status, state->status_size, "EDIT CANCELED");
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                finish_rom_edit(state);
                break;
            case SDLK_BACKSPACE:
                if (value) {
                    remove_last_char(value);
                }
                break;
            default:
                break;
        }
        return ROM_ACTION_NONE;
    }
    if (state->editor->rom_browser_active) {
        switch (key->keysym.sym) {
            case SDLK_ESCAPE:
                state->editor->rom_browser_active = false;
                copy_text(state->status, state->status_size, "ROM REGISTER");
                break;
            case SDLK_TAB:
            case SDLK_DOWN:
            case SDLK_RIGHT:
                if (state->editor->rom_browser_count > 0) {
                    state->editor->rom_browser_selected = (state->editor->rom_browser_selected + 1) % state->editor->rom_browser_count;
                }
                break;
            case SDLK_UP:
            case SDLK_LEFT:
                if (state->editor->rom_browser_count > 0) {
                    state->editor->rom_browser_selected =
                        state->editor->rom_browser_selected == 0 ? state->editor->rom_browser_count - 1 : state->editor->rom_browser_selected - 1;
                }
                break;
            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                apply_browser_rom(state);
                break;
            default:
                break;
        }
        return ROM_ACTION_NONE;
    }

    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            return ROM_ACTION_LEAVE;
            break;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_rom_selection(state, 1);
            break;
        case SDLK_UP:
            move_rom_selection(state, -1);
            break;
        case SDLK_RIGHT:
            cycle_rom_for_selected_slot(state, 1);
            break;
        case SDLK_LEFT:
            cycle_rom_for_selected_slot(state, -1);
            break;
        case SDLK_F2:
            if (state->editor->rom_selected < INTEGRAL_ROM_SLOTS) {
                if (!begin_rom_edit(state, ROM_EDIT_ROM)) return ROM_ACTION_MAIN;
            }
            break;
        case SDLK_F4:
            scan_rom_folder(state);
            break;
        case SDLK_F5:
            if (!state->allow_user_initial_save_import) {
                break;
            }
            if (state->editor->rom_selected >= INTEGRAL_ROM_SLOTS ||
                state->rom_slots[state->editor->rom_selected].rom_path[0] == '\0') {
                copy_text(state->status, state->status_size, "SELECT A ROM SLOT FIRST");
                break;
            }
            if (state->editor->rom_initial_save_import_slot == (int)state->editor->rom_selected) {
                state->editor->rom_initial_save_import_slot = -1;
                state->editor->rom_initial_save_import_path[0] = '\0';
                copy_text(state->status, state->status_size, "INITIAL SAV IMPORT CLEARED");
            }
            else {
                state->editor->rom_initial_save_import_path[0] = '\0';
                if (!begin_rom_edit(state, ROM_EDIT_INITIAL_SAVE)) return ROM_ACTION_MAIN;
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->editor->rom_selected == INTEGRAL_ROM_EXPORT_ROW) {
                return ROM_ACTION_EXPORT;
            }
            else if (state->editor->rom_selected == INTEGRAL_ROM_BACK_ROW) {
                return ROM_ACTION_LEAVE;
            }
            else if (state->editor->rom_selected < INTEGRAL_ROM_SLOTS) {
                unsigned index = state->editor->rom_selected;
                IntegralConfigRomSlot *slot = &state->rom_slots[index];
                if (slot->rom_path[0] == '\0') {
                    break;
                }
                bool import_selected =
                    state->allow_user_initial_save_import &&
                    state->editor->rom_initial_save_import_slot == (int)index;
                if (slot_has_server_registration(slot) &&
                    !state->editor->pending_paths[index] &&
                    !import_selected) {
                    break;
                }
                return ROM_ACTION_REGISTER;
            }
            break;
        default:
            break;
    }
    return ROM_ACTION_NONE;
}

static void handle_rom_text_input(RomEditContext *state, const SDL_TextInputEvent *text)
{
    char *value = rom_edit_value(state);
    size_t capacity = rom_edit_capacity(state);
    if (value && capacity > 0) {
        append_text(value, capacity, text->text);
    }
}

IntegralRomEditorAction integral_client_rom_editor_key(IntegralClientRomEditor *editor,
    IntegralConfigRomSlot *slots, const char *path, bool allow_import,
    char *status, size_t status_size, const SDL_KeyboardEvent *event)
{
    RomEditContext context = {editor, slots, path, allow_import, status, status_size};
    return handle_rom_key(&context, event);
}
void integral_client_rom_editor_text(IntegralClientRomEditor *editor,
    IntegralConfigRomSlot *slots, const SDL_TextInputEvent *event)
{
    RomEditContext context = {.editor = editor, .rom_slots = slots};
    handle_rom_text_input(&context, event);
}

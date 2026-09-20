/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ROM_EDITOR_H
#define INTEGRAL_CLIENT_ROM_EDITOR_H
#include "client_config.h"
#include <SDL.h>
#include <stdbool.h>

#define INTEGRAL_ROM_SLOTS 8
#define INTEGRAL_ROM_REGISTER_ROW INTEGRAL_ROM_SLOTS
#define INTEGRAL_ROM_EXPORT_ROW (INTEGRAL_ROM_SLOTS + 1)
#define INTEGRAL_ROM_BACK_ROW (INTEGRAL_ROM_SLOTS + 2)
#define INTEGRAL_ROM_ROWS (INTEGRAL_ROM_SLOTS + 3)
#define INTEGRAL_ROM_BROWSER_MAX 24
#define INTEGRAL_ROM_FOLDER "roms"

typedef enum RomEditTarget {
    ROM_EDIT_NONE,
    ROM_EDIT_ROM,
    ROM_EDIT_INITIAL_SAVE,
} RomEditTarget;
typedef struct IntegralClientRomEditor {
    /* Preserve the confirmed registration while rom_slots holds editable paths. */
    IntegralConfigRomSlot confirmed_slots[INTEGRAL_ROM_SLOTS];
    bool pending_paths[INTEGRAL_ROM_SLOTS];
    unsigned rom_selected;
    RomEditTarget rom_edit_target;
    char rom_edit_original[INTEGRAL_CONFIG_PATH_MAX];
    bool rom_confirm_delete;
    int rom_initial_save_import_slot;
    char rom_initial_save_import_path[INTEGRAL_CONFIG_PATH_MAX];
    bool rom_confirm_initial_save_import;
    bool rom_browser_active;
    char rom_browser_entries[INTEGRAL_ROM_BROWSER_MAX][INTEGRAL_CONFIG_PATH_MAX];
    unsigned rom_browser_count;
    unsigned rom_browser_selected;
} IntegralClientRomEditor;
typedef enum IntegralRomEditorAction {
    ROM_ACTION_NONE, ROM_ACTION_LEAVE, ROM_ACTION_MAIN, ROM_ACTION_EXPORT,
    ROM_ACTION_REGISTER, ROM_ACTION_REGISTER_IMPORT, ROM_ACTION_REGISTER_DELETE,
} IntegralRomEditorAction;
/* The caller retains API, SAV transfer and screen-transition ownership. */
IntegralRomEditorAction integral_client_rom_editor_key(IntegralClientRomEditor *,
    IntegralConfigRomSlot *, const char *config_path, bool allow_import,
    char *status, size_t status_size, const SDL_KeyboardEvent *);
void integral_client_rom_editor_text(IntegralClientRomEditor *,
    IntegralConfigRomSlot *, const SDL_TextInputEvent *);
void integral_client_rom_editor_reset(IntegralClientRomEditor *, bool select_first);
void integral_client_rom_editor_discard(IntegralClientRomEditor *, IntegralConfigRomSlot *);
#endif

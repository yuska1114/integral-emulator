/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_rom_editor.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define chdir _chdir
#define mkdir_private(path) _mkdir(path)
#else
#include <unistd.h>
#include <sys/stat.h>
#define mkdir_private(path) mkdir(path, 0700)
#endif
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "ROM editor line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static IntegralClientRomEditor editor;
static IntegralConfigRomSlot slots[INTEGRAL_ROM_SLOTS];
static char status[192];
static bool allow_import;
static IntegralRomEditorAction press(SDL_Keycode sym, unsigned repeat)
{
    SDL_KeyboardEvent event = {0};
    event.keysym.sym = sym; event.repeat = repeat;
    return integral_client_rom_editor_key(&editor, slots, "config.conf", allow_import,
        status, sizeof(status), &event);
}
static void text(const char *value)
{
    SDL_TextInputEvent event = {0};
    snprintf(event.text, sizeof(event.text), "%s", value);
    integral_client_rom_editor_text(&editor, slots, &event);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 && chdir(argv[1]) == 0);
    CHECK(SDL_Init(0) == 0);
    editor.rom_selected = 5; editor.rom_browser_count = 2;
    editor.rom_confirm_delete = true; editor.rom_confirm_initial_save_import = true;
    editor.rom_browser_active = true; editor.rom_edit_target = ROM_EDIT_INITIAL_SAVE;
    strcpy(editor.rom_initial_save_import_path, "previous.sav");
    integral_client_rom_editor_reset(&editor, false);
    CHECK(editor.rom_selected == 5 && editor.rom_browser_count == 2);
    CHECK(!editor.rom_confirm_delete && !editor.rom_confirm_initial_save_import && !editor.rom_browser_active);
    CHECK(editor.rom_edit_target == ROM_EDIT_NONE && editor.rom_initial_save_import_slot == -1 && !editor.rom_initial_save_import_path[0]);
    integral_client_rom_editor_reset(&editor, true);
    CHECK(editor.rom_selected == 0);
    press(SDLK_UP, 0); CHECK(editor.rom_selected == INTEGRAL_ROM_BACK_ROW);
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_LEAVE);
    press(SDLK_DOWN, 0); CHECK(editor.rom_selected == 0);
    press(SDLK_DOWN, 1); CHECK(editor.rom_selected == 0);
    press(SDLK_F5, 0); CHECK(editor.rom_edit_target == ROM_EDIT_NONE);
    allow_import = true; press(SDLK_F5, 0);
    CHECK(strcmp(status, "SELECT A ROM SLOT FIRST") == 0);
    press(SDLK_RETURN, 0); text("roms/sample.gbc");
    CHECK(press(SDLK_ESCAPE, 0) == ROM_ACTION_NONE);
    CHECK(slots[0].rom_path[0] == 0 && strcmp(status, "EDIT CANCELED") == 0);
    press(SDLK_RETURN, 0); text("roms/sample.gbc"); press(SDLK_RETURN, 0);
    CHECK(strcmp(slots[0].rom_path, "roms/sample.gbc") == 0 && editor.rom_edit_target == ROM_EDIT_NONE);
    press(SDLK_F5, 0); text("missing.sav"); press(SDLK_RETURN, 0);
    CHECK(editor.rom_initial_save_import_slot == -1 && strcmp(status, "INITIAL SAV FILE REQUIRED") == 0);
    FILE *file = fopen("sample.sav", "wb"); CHECK(file != NULL);
    CHECK(fputs("synthetic", file) >= 0 && fclose(file) == 0);
    press(SDLK_F5, 0); text("sample.sav"); press(SDLK_RETURN, 0);
    CHECK(editor.rom_initial_save_import_slot == 0);
    editor.rom_selected = INTEGRAL_ROM_REGISTER_ROW;
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_REGISTER);
    editor.rom_confirm_initial_save_import = true;
    CHECK(press(SDLK_ESCAPE, 0) == ROM_ACTION_NONE && !editor.rom_confirm_initial_save_import);
    editor.rom_confirm_initial_save_import = true;
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_REGISTER_IMPORT && !editor.rom_confirm_initial_save_import);
    editor.rom_confirm_delete = true;
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_REGISTER_DELETE);
    CHECK(press(SDLK_ESCAPE, 0) == ROM_ACTION_NONE && !editor.rom_confirm_delete);
    editor.rom_selected = INTEGRAL_ROM_EXPORT_ROW;
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_EXPORT);
    editor.rom_selected = 0; press(SDLK_F5, 0);
    CHECK(editor.rom_initial_save_import_slot == -1 && editor.rom_initial_save_import_path[0] == 0);

    CHECK(mkdir_private("roms") == 0);
    file = fopen("roms/sample.gbc", "wb"); CHECK(file && fclose(file) == 0);
    press(SDLK_F4, 0); CHECK(editor.rom_browser_active && editor.rom_browser_count == 1);
    press(SDLK_DOWN, 0); CHECK(editor.rom_browser_selected == 0);
    press(SDLK_RETURN, 0); CHECK(!editor.rom_browser_active && slots[0].rom_id[0] == 0);
    strcpy(slots[0].rom_id, "same-rom"); strcpy(slots[0].save_id, "save-one");
    editor.pending_paths[0] = false;
    slots[1] = slots[0]; strcpy(slots[1].save_id, "save-two");
    press(SDLK_RETURN, 0); text("x"); press(SDLK_BACKSPACE, 0); press(SDLK_RETURN, 0);
    CHECK(strcmp(status, "LOCAL ROM PATH SAVED") == 0);
    press(SDLK_RETURN, 0); text(".missing"); press(SDLK_RETURN, 0);
    CHECK(strcmp(status, "FILE NOT FOUND - EDIT ROM PATH") == 0);
    CHECK(!strcmp(slots[0].rom_id, "same-rom") && !strcmp(slots[0].save_id, "save-one"));
    editor.rom_confirm_delete = true;
    press(SDLK_ESCAPE, 0);
    CHECK(!strcmp(slots[0].rom_path, "roms/sample.gbc") && !editor.pending_paths[0]);
    CHECK(!strcmp(slots[0].save_id, "save-one"));
    strcpy(slots[0].rom_path, "roms/sample.gbc");
    strcpy(slots[0].rom_id, "same-rom"); strcpy(slots[0].save_id, "save-one");
    IntegralConfigRomSlot loaded[INTEGRAL_ROM_SLOTS] = {0};
    CHECK(integral_config_load_rom_slots("config.conf", loaded, INTEGRAL_ROM_SLOTS) == 0);
    CHECK(strcmp(loaded[0].save_id, "save-one") == 0 && strcmp(loaded[1].save_id, "save-two") == 0);
    press(SDLK_BACKSPACE, 0); CHECK(!strcmp(slots[0].save_id, "save-one") && strcmp(slots[1].save_id, "save-two") == 0);
    CHECK(integral_config_load_rom_slots("config.conf", loaded, INTEGRAL_ROM_SLOTS) == 0);
    CHECK(!strcmp(loaded[0].save_id, "save-one") && strcmp(loaded[1].save_id, "save-two") == 0);
    press(SDLK_RIGHT, 0); CHECK(strcmp(slots[0].rom_path, "roms/sample.gbc") == 0);
    CHECK(strcmp(slots[1].save_id, "save-two") == 0);
    CHECK(press(SDLK_ESCAPE, 0) == ROM_ACTION_LEAVE);
    CHECK(remove("sample.sav") == 0 && remove("roms/sample.gbc") == 0 && remove("config.conf") == 0);
    SDL_Quit(); puts("ROM editor test passed"); return 0;
}

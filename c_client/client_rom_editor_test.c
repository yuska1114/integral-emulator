/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_rom_editor.h"
#include "client_rom_catalog.h"
#include "rom_metadata.h"
#include "../runtimes/gb/src/common/utf8_file.h"
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
#ifdef _WIN32
static int unicode_browser_test(void)
{
    const char *paths[] = {"roms/日本語 GB.gb", "roms/銀 試験.gbc", "roms/スタジアム 試験.z64"};
    CHECK(CreateDirectoryW(L"一覧 日本語", NULL));
    CHECK(_wchdir(L"一覧 日本語") == 0 && _mkdir("roms") == 0);
    CHECK(CreateDirectoryW(L"roms/対象外.gbc", NULL));
    memset(&editor, 0, sizeof(editor));
    memset(slots, 0, sizeof(slots));
    for (unsigned i = 0; i < 3; i++) {
        unsigned char bytes[0x150] = {0};
        if (i == 2) {
            bytes[0] = 0x80; bytes[1] = 0x37; bytes[2] = 0x12; bytes[3] = 0x40;
            memcpy(bytes + 0x20, "UNICODE N64", 11);
            bytes[0x3e] = 'J';
        } else {
            memcpy(bytes + 0x134, "UNICODE GB", 10);
            if (i == 1) bytes[0x143] = 0x80;
            for (unsigned j = 0x134; j <= 0x14c; j++) bytes[0x14d] -= bytes[j] + 1;
        }
        FILE *file = integral_fopen(paths[i], "wb");
        CHECK(file && fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
        CHECK(fclose(file) == 0);
    }
    for (unsigned i = 0; i < 3; i++) {
        editor.rom_selected = i;
        press(SDLK_F4, 0);
        CHECK(editor.rom_browser_active && editor.rom_browser_count == 3);
        unsigned index = 0;
        while (index < 3 && strcmp(editor.rom_browser_entries[index], paths[i])) index++;
        CHECK(index < 3); /* These UTF-8 entries are the displayed F4 list. */
        for (unsigned j = 0; j < index; j++) press(SDLK_DOWN, 0);
        press(SDLK_RETURN, 0);
        CHECK(!editor.rom_browser_active && !strcmp(slots[i].rom_path, paths[i]));
        IntegralRomMetadata metadata;
        CHECK(integral_rom_metadata_read(slots[i].rom_path, &metadata) == 0);
        CHECK(!strcmp(metadata.platform, i == 2 ? "n64" : "gb"));
        CHECK(!strcmp(metadata.header_title, i == 2 ? "UNICODE N64" : "UNICODE GB"));
    }
    char entries[3][INTEGRAL_CONFIG_PATH_MAX];
    CHECK(scan_rom_paths("\xff", entries, 3) == -1);
    CHECK(scan_rom_paths("roms", entries, 1) == 1);
    CHECK(_wchdir(L"..") == 0);
    CHECK(scan_rom_paths("一覧 日本語/roms", entries, 3) == 3);
    CHECK(_wchdir(L"一覧 日本語") == 0);
    for (unsigned i = 0; i < 3; i++) {
        wchar_t *wide = integral_utf8_wide(paths[i]);
        CHECK(wide && _wremove(wide) == 0);
        free(wide);
    }
    CHECK(RemoveDirectoryW(L"roms/対象外.gbc"));
    CHECK(scan_rom_paths("roms", entries, 3) == 0);
    CHECK(_rmdir("roms") == 0);
    CHECK(_wchdir(L"..") == 0);
    CHECK(RemoveDirectoryW(L"一覧 日本語"));
    puts("PASS Windows F4 UTF-8 GB/GBC/N64 list, selection and metadata");
    return 0;
}
#endif
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
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_NONE);
    CHECK(slots[0].rom_path[0] == 0 && editor.rom_edit_target == ROM_EDIT_NONE);
    press(SDLK_F2, 0); text("roms/sample.gbc");
    CHECK(press(SDLK_ESCAPE, 0) == ROM_ACTION_NONE);
    CHECK(slots[0].rom_path[0] == 0 && !editor.pending_paths[0] && strcmp(status, "EDIT CANCELED") == 0);
    press(SDLK_F2, 0); text("roms/sample.gbc"); press(SDLK_RETURN, 0);
    CHECK(strcmp(slots[0].rom_path, "roms/sample.gbc") == 0 && editor.rom_edit_target == ROM_EDIT_NONE);
    press(SDLK_F5, 0); text("missing.sav"); press(SDLK_RETURN, 0);
    CHECK(editor.rom_initial_save_import_slot == -1 && strcmp(status, "INITIAL SAV FILE REQUIRED") == 0);
    FILE *file = fopen("sample.sav", "wb"); CHECK(file != NULL);
    CHECK(fputs("synthetic", file) >= 0 && fclose(file) == 0);
    press(SDLK_F5, 0); text("sample.sav"); press(SDLK_RETURN, 0);
    CHECK(editor.rom_initial_save_import_slot == 0);
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
    strcpy(status, "UNCHANGED");
    CHECK(press(SDLK_RETURN, 0) == ROM_ACTION_NONE);
    CHECK(strcmp(status, "UNCHANGED") == 0 && !editor.pending_paths[0]);
    press(SDLK_F2, 0); text("x"); press(SDLK_BACKSPACE, 0); press(SDLK_RETURN, 0);
    CHECK(strcmp(status, "LOCAL ROM PATH SAVED") == 0 && !editor.pending_paths[0]);
    press(SDLK_F2, 0); text(".missing"); press(SDLK_RETURN, 0);
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
    press(SDLK_BACKSPACE, 0);
    CHECK(!strcmp(slots[0].rom_path, "roms/sample.gbc") &&
          !strcmp(slots[0].save_id, "save-one") && strcmp(slots[1].save_id, "save-two") == 0);
    CHECK(integral_config_load_rom_slots("config.conf", loaded, INTEGRAL_ROM_SLOTS) == 0);
    CHECK(!strcmp(loaded[0].save_id, "save-one") && strcmp(loaded[1].save_id, "save-two") == 0);
    press(SDLK_RIGHT, 0); CHECK(strcmp(slots[0].rom_path, "roms/sample.gbc") == 0);
    CHECK(strcmp(slots[1].save_id, "save-two") == 0);
    CHECK(press(SDLK_ESCAPE, 0) == ROM_ACTION_LEAVE);
    CHECK(remove("sample.sav") == 0 && remove("roms/sample.gbc") == 0 && remove("config.conf") == 0);
#ifdef _WIN32
    CHECK(unicode_browser_test() == 0);
#endif
    SDL_Quit(); puts("ROM editor test passed"); return 0;
}

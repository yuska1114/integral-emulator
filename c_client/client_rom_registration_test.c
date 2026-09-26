/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_rom_registration.h"
#include "client_file_io.h"
#include "../runtimes/gb/src/server/slot.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "registration line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
static IntegralApiRomSlot response[INTEGRAL_ROM_SLOTS];
static unsigned apply_calls, get_calls, import_logs, sync_logs;
static int api_failure, get_failure, confirmation, expected_delete;
static size_t expected_bytes = 4;
static int expected_generated = 1;
static unsigned generated_save_calls;
static char hash256[65], hash1[41];

int integral_gb_runtime_initial_battery_for_rom(const char *rom_path,
                                                uint8_t *buffer,
                                                size_t buffer_capacity,
                                                size_t *buffer_size)
{
    static const uint8_t generated_save[] = {0xFF, 0xFF, 0xFF, 0xFF};
    CHECK(rom_path && strstr(rom_path, "sample.gbc") != NULL);
    CHECK(buffer && buffer_size && buffer_capacity >= sizeof(generated_save));
    memcpy(buffer, generated_save, sizeof(generated_save));
    *buffer_size = sizeof(generated_save);
    generated_save_calls++;
    return 0;
}

int integral_api_get_rom_slots(const char *server, const char *token, IntegralApiRomSlot *out,
                              size_t count, char *error, size_t capacity)
{
    CHECK(strcmp(server, "https://test.invalid") == 0 && strcmp(token, "test-token") == 0);
    CHECK(count == INTEGRAL_ROM_SLOTS); get_calls++;
    if (get_failure) { snprintf(error, capacity, "get-test-error"); return -1; }
    memcpy(out, response, sizeof(response)); return 0;
}
int integral_api_apply_rom_slot(const char *server, const char *token, unsigned slot,
    const char *filename, const char *sha256, const char *sha1, const char *platform,
    const char *region, const char *title, const unsigned char *data, size_t bytes,
    int initial_save_generated, int confirm_delete, char *rom, size_t rom_capacity,
    char *save, size_t save_capacity,
    int *requires_confirmation, char *error, size_t error_capacity)
{
    CHECK(strcmp(server, "https://test.invalid") == 0 && strcmp(token, "test-token") == 0);
    CHECK(slot >= 1 && slot <= INTEGRAL_ROM_SLOTS);
    CHECK(strcmp(filename, "sample.gbc") == 0 && strcmp(platform, "gb") == 0);
    CHECK(strcmp(title, "GENERIC SAMPLE") == 0 && region[0]);
    CHECK(strcmp(sha256, hash256) == 0 && strcmp(sha1, hash1) == 0);
    CHECK(confirm_delete == expected_delete && bytes == expected_bytes);
    CHECK(initial_save_generated == expected_generated);
    if (initial_save_generated) {
        static const unsigned char generated_save[] = {0xFF, 0xFF, 0xFF, 0xFF};
        CHECK(data && bytes == sizeof(generated_save));
        CHECK(memcmp(data, generated_save, sizeof(generated_save)) == 0);
    }
    else if (bytes) {
        CHECK(data && bytes == 3 && memcmp(data, "abc", 3) == 0 && import_logs > 0);
    }
    else CHECK(data == NULL);
    apply_calls++;
    if (api_failure) {
        /* Output buffers are not committed on failure, even if partially filled. */
        snprintf(rom, rom_capacity, "uncommitted-rom");
        snprintf(save, save_capacity, "uncommitted-save");
        snprintf(error, error_capacity, "apply-test-error"); return -1;
    }
    *requires_confirmation = confirmation;
    if (confirmation) return 0;
    snprintf(rom, rom_capacity, "rom-%u", slot); snprintf(save, save_capacity, "save-%u", slot);
    snprintf(response[slot-1].rom_id, sizeof(response[slot-1].rom_id), "%s", rom);
    snprintf(response[slot-1].save_id, sizeof(response[slot-1].save_id), "%s", save);
    snprintf(response[slot-1].filename, sizeof(response[slot-1].filename), "%s", filename);
    snprintf(response[slot-1].sha256, sizeof(response[slot-1].sha256), "%s", sha256);
    return 0;
}
static void observe(void *context, const char *event, const char *detail)
{
    (void)context;
    CHECK(strstr(detail, "test-token") == NULL);
    if (strcmp(event, "rom_slot_initial_save_import_confirmed") == 0) import_logs++;
    if (strcmp(event, "rom_slots_synced") == 0) sync_logs++;
}
static void write_file(const char *path, const void *data, size_t size)
{
    FILE *f = fopen(path, "wb"); CHECK(f); CHECK(fwrite(data, 1, size, f) == size); CHECK(fclose(f) == 0);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    char rom_path[1024], sav_path[1024], config[1024], failed_config[1100], status[160];
    snprintf(rom_path, sizeof(rom_path), "%s/roms/sample.gbc", argv[1]);
    snprintf(sav_path, sizeof(sav_path), "%s/initial.sav", argv[1]);
    snprintf(config, sizeof(config), "%s/config.conf", argv[1]);
    snprintf(failed_config, sizeof(failed_config), "%s/blocked.conf", sav_path);
    unsigned char gb[0x150] = {0}; memcpy(gb + 0x134, "GENERIC SAMPLE", 14);
    for (unsigned i=0x134; i<=0x14c; i++) gb[0x14d] = (unsigned char)(gb[0x14d]-gb[i]-1);
    write_file(rom_path, gb, sizeof(gb)); write_file(sav_path, "abc", 3);
    char digest[65]; CHECK(sha256_file_hex(sav_path, digest, sizeof(digest)) == 0);
    CHECK(strcmp(digest, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);
    CHECK(sha1_file_hex(sav_path, digest, sizeof(digest)) == 0);
    CHECK(strcmp(digest, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0);
    unsigned char buffer[4]; size_t bytes = 99;
    CHECK(read_binary_file(sav_path, buffer, 2, &bytes) != 0 && bytes == 0);
    CHECK(read_binary_file(sav_path, buffer, 3, &bytes) == 0 && bytes == 3);
    CHECK(sha256_file_hex(rom_path, hash256, sizeof(hash256)) == 0);
    CHECK(sha1_file_hex(rom_path, hash1, sizeof(hash1)) == 0);
    IntegralConfigRomSlot slots[INTEGRAL_ROM_SLOTS] = {0};
    IntegralApiRomSlot cached[INTEGRAL_ROM_SLOTS] = {0};
    IntegralClientRomEditor editor = {.rom_initial_save_import_slot = -1};
    IntegralRomRegistration state = {.rom_slots=slots, .server_rom_slots=cached, .editor=&editor,
        .server="https://test.invalid", .token="test-token", .server_id="primary", .config_path=config,
        .status=status, .status_size=sizeof(status), .log=observe};
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 0 && strcmp(status, "ROM REQUIRED") == 0);
    strcpy(slots[0].rom_path, rom_path); state.token = "";
    integral_rom_registration_apply(&state, false, false); CHECK(apply_calls == 0);
    CHECK(!integral_rom_registration_refresh(&state) && get_calls == 0); state.token="test-token";
    editor.rom_initial_save_import_slot=0; strcpy(editor.rom_initial_save_import_path, sav_path);
    /* Disabled capability must never send the selected SAV, even with confirmation. */
    expected_delete=1; integral_rom_registration_apply(&state, true, true);
    CHECK(apply_calls == 1 && get_calls == 1 && import_logs == 0 && sync_logs == 1);
    CHECK(generated_save_calls == 1);
    CHECK(strcmp(slots[0].save_id, "save-1") == 0 && strcmp(status, "ROM1 REGISTERED") == 0);
    state.allow_user_initial_save_import=true; editor.rom_initial_save_import_slot=0;
    strcpy(editor.rom_initial_save_import_path, sav_path);
    integral_rom_registration_apply(&state, false, true);
    CHECK(apply_calls == 1 && strcmp(status, "INITIAL SAV REJECTED USE ADMIN SAV REPLACE") == 0);
    editor.rom_selected=1; strcpy(slots[1].rom_path, rom_path); editor.rom_initial_save_import_slot=1;
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 1 && editor.rom_confirm_initial_save_import);
    expected_delete=0; expected_bytes=3; expected_generated=0; confirmation=1;
    integral_rom_registration_apply(&state, false, true);
    CHECK(apply_calls == 2 && editor.rom_confirm_delete && !editor.rom_confirm_initial_save_import);
    CHECK(get_calls == 1 && slots[1].save_id[0] == 0);
    confirmation=0; expected_delete=1;
    integral_rom_registration_apply(&state, true, true);
    CHECK(apply_calls == 3 && get_calls == 2 && !editor.rom_confirm_delete);
    CHECK(strcmp(slots[0].save_id, "save-1") == 0 && strcmp(slots[1].save_id, "save-2") == 0);
    CHECK(editor.rom_initial_save_import_slot == -1 && editor.rom_initial_save_import_path[0] == 0);
    IntegralConfigRomSlot loaded[INTEGRAL_ROM_SLOTS] = {0};
    CHECK(integral_config_load_rom_slots(config, loaded, INTEGRAL_ROM_SLOTS) == 0);
    CHECK(strcmp(loaded[0].save_id, "save-1") == 0 && strcmp(loaded[1].save_id, "save-2") == 0);
    editor.rom_selected=INTEGRAL_ROM_EXPORT_ROW;
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 3 && strcmp(status, "NO ROM CHANGES") == 0);
    strcpy(slots[2].rom_path, "missing.gbc");
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 3 && strcmp(status, "FILE NOT FOUND - EDIT ROM PATH") == 0);
    strcpy(slots[2].rom_path, rom_path); api_failure=1; expected_bytes=4;
    expected_generated=1; expected_delete=0;
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 4 && get_calls == 2 && strcmp(status, "REGISTER FAILED apply-test-error") == 0);
    CHECK(!slots[2].save_id[0] && !slots[2].rom_id[0]);
    api_failure=0; state.config_path=failed_config;
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 5 && get_calls == 2 && strcmp(status, "REGISTERED CONFIG SAVE FAILED") == 0);
    state.config_path=config; editor.rom_selected=2; get_failure=1;
    integral_rom_registration_apply(&state, false, false);
    CHECK(apply_calls == 6 && get_calls == 3 && strcmp(status, "REGISTERED BUT SYNC FAILED") == 0);
    get_failure=0; state.config_path=failed_config;
    CHECK(!integral_rom_registration_refresh(&state));
    CHECK(strcmp(status, "ROM SLOT SYNC SAVE FAILED") == 0);
    state.config_path=config; editor.rom_selected=3; strcpy(slots[3].rom_path, rom_path);
    editor.rom_initial_save_import_slot=3; strcpy(editor.rom_initial_save_import_path, "missing.sav");
    integral_rom_registration_apply(&state, false, true);
    CHECK(apply_calls == 6 && strcmp(status, "SELECTED INITIAL SAV READ FAILED") == 0);
    get_failure=1;
    unsigned previous_gets=get_calls;
    integral_rom_registration_enter(&state);
    CHECK(get_calls == previous_gets+1 && slots[3].rom_path[0] == 0);
    CHECK(strcmp(status, "ROM SLOT SYNC FAILED get-test-error") == 0);
    get_failure=0;
    integral_rom_registration_enter(&state);
    CHECK(get_calls == previous_gets+2 && strcmp(slots[0].save_id, "save-1") == 0);
    strcpy(cached[0].filename, "sample.gbc");
    slots[0].rom_id[0]=0; slots[0].save_id[0]=0;
    previous_gets=get_calls;
    integral_rom_registration_leave(&state);
    CHECK(get_calls == previous_gets && strcmp(slots[0].save_id, "save-1") == 0);
    CHECK(strcmp(slots[0].rom_path, "roms/sample.gbc") == 0 && strcmp(slots[1].save_id, "save-2") == 0);
    CHECK(integral_config_load_rom_slots(config, loaded, INTEGRAL_ROM_SLOTS) == 0);
    CHECK(strcmp(loaded[0].save_id, "save-1") == 0);
    CHECK(remove(rom_path) == 0 && remove(sav_path) == 0 && remove(config) == 0);
    puts("ROM registration test passed");
    return 0;
}

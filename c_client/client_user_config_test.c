/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_user_config.h"
#include "client_rom_catalog.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed at line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    char config[1024], other[1024], gb_path[1024], n64_path[1024];
    snprintf(config, sizeof(config), "%s/user1.conf", argv[1]);
    snprintf(other, sizeof(other), "%s/user2.conf", argv[1]);
    snprintf(gb_path, sizeof(gb_path), "%s/sample.gbc", argv[1]);
    snprintf(n64_path, sizeof(n64_path), "%s/sample.z64", argv[1]);
    unsigned char gb[0x150] = {0}, n64[0x40] = {0x80, 0x37, 0x12, 0x40};
    memcpy(gb + 0x134, "GENERIC SAMPLE", 14);
    for (unsigned i = 0x134; i <= 0x14c; i++) gb[0x14d] = (unsigned char)(gb[0x14d] - gb[i] - 1);
    memcpy(n64 + 0x20, "GENERIC N64", 11);
    n64[0x3e] = 'E';
    FILE *file = fopen(gb_path, "wb");
    CHECK(file && fwrite(gb, 1, sizeof(gb), file) == sizeof(gb)); CHECK(fclose(file) == 0);
    file = fopen(n64_path, "wb");
    CHECK(file && fwrite(n64, 1, sizeof(n64), file) == sizeof(n64)); CHECK(fclose(file) == 0);

    IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    strcpy(slots[0].rom_path, gb_path); strcpy(slots[0].rom_id, "rom1"); strcpy(slots[0].save_id, "save1");
    slots[1] = slots[0]; strcpy(slots[1].save_id, "save2");
    strcpy(slots[2].rom_path, gb_path); /* unregistered GB is discarded */
    strcpy(slots[3].rom_path, n64_path); /* local N64 is retained */
    CHECK(slot_is_supported_gb(&slots[0]) && slot_is_supported_n64(&slots[3]));
    CHECK(integral_config_save_rom_slots(config, slots, INTEGRAL_CONFIG_ROM_SLOTS) == 0);
    IntegralConfigLocal local = {.slot1_index = 0, .slot2_index = 1, .ir_off_delay_ticks = 32};
    CHECK(integral_config_save_local(config, &local) == 0);
    IntegralConfigKeys keys;
    integral_keys_defaults(&keys);
    strcpy(keys.slot1, "D,A,W,S,J,K,Q,E");
    CHECK(integral_config_save_keys(config, &keys) == 0);
    memset(slots, 0, sizeof(slots));
    int indices[2], room = 0;
    CHECK(load_user_config(config, slots, &keys, indices) == 0);
    CHECK(strcmp(keys.slot1, "D,A,W,S,J,K,Q,E") == 0 && strcmp(keys.escape, "ESCAPE") == 0);
    CHECK(slots[2].rom_path[0] == 0 && slot_is_supported_n64(&slots[3]));
    CHECK(strcmp(slots[0].save_id, "save1") == 0 && strcmp(slots[1].save_id, "save2") == 0);
    validate_local_indices(slots, indices, &room);
    CHECK(indices[0] == 0 && indices[1] == 1 && room == 0);
    indices[1] = 0; room = 99;
    validate_local_indices(slots, indices, &room);
    CHECK(indices[0] == 0 && indices[1] == -1 && room == -1);
    IntegralConfigRomSlot restored[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    CHECK(integral_config_load_rom_slots(config, restored, INTEGRAL_CONFIG_ROM_SLOTS) == 0);
    CHECK(restored[2].rom_path[0] == 0 && strcmp(restored[1].save_id, "save2") == 0);

    IntegralApiRomSlot server[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    strcpy(server[1].rom_id, "new-rom"); strcpy(server[1].save_id, "new-save"); strcpy(server[1].filename, "new.gbc");
    merge_server_rom_slots(slots, server, "roms");
    CHECK(slots[0].rom_path[0] == 0 && slot_is_supported_n64(&slots[3]));
    CHECK(strcmp(slots[1].rom_path, "roms/new.gbc") == 0 && strcmp(slots[1].save_id, "new-save") == 0);
    char entries[4][INTEGRAL_CONFIG_PATH_MAX];
    CHECK(scan_rom_paths(argv[1], entries, 4) == 2);
    CHECK(scan_rom_paths(argv[1], entries, 1) == 1);
    CHECK(scan_rom_paths(argv[1], entries, 0) == 0);
    CHECK(scan_rom_paths(config, entries, 4) == -1);
    memset(slots, 0, sizeof(slots));
    CHECK(load_user_config(other, slots, &keys, indices) == 0);
    CHECK(strcmp(keys.slot1, "RIGHT,LEFT,UP,DOWN,Z,X,RSHIFT,RETURN") == 0);
    CHECK(indices[0] == -1 && indices[1] == -1 && slots[0].save_id[0] == 0);
    CHECK(load_user_config(config, slots, &keys, indices) == 0);
    CHECK(strcmp(slots[0].save_id, "save1") == 0 && strcmp(keys.slot1, "D,A,W,S,J,K,Q,E") == 0);
    char status[160] = "unchanged";
    indices[0] = 0; indices[1] = -1;
    integral_rom_cycle_local(slots, indices, config, status, sizeof(status), 0, -1);
    CHECK(indices[0] == 1 && strcmp(status, "SLOT1 SELECTED ROM2") == 0);
    integral_rom_cycle_local(slots, indices, config, status, sizeof(status), 1, 1);
    CHECK(indices[1] == 0 && strcmp(slots[0].save_id, "save1") == 0 && strcmp(slots[1].save_id, "save2") == 0);
    CHECK(integral_config_load_local(config, &local) == 0 && local.slot1_index == 1 && local.slot2_index == 0);
    integral_rom_cycle_local(slots, indices, config, status, sizeof(status), 2, 1);
    CHECK(indices[0] == 1 && indices[1] == 0);
    int n64_index = -1;
    integral_rom_cycle_n64(slots, &n64_index, status, sizeof(status), 1);
    CHECK(n64_index == 3 && strcmp(status, "N64 SELECTED ROM4") == 0);
    int transfer[4] = {-1, -1, -1, -1};
    integral_rom_cycle_transfer(slots, transfer, status, sizeof(status), 0, 1);
    CHECK(transfer[0] == 0);
    integral_rom_cycle_transfer(slots, transfer, status, sizeof(status), 1, 1);
    CHECK(transfer[1] == 1);
    integral_rom_cycle_transfer(slots, transfer, status, sizeof(status), 2, 1);
    CHECK(transfer[2] == -1 && strcmp(status, "NO GB/GBC ROM SET") == 0);
    integral_rom_cycle_transfer(slots, transfer, status, sizeof(status), 0, 1);
    CHECK(transfer[0] == -1 && strcmp(status, "SLOT1 CLEARED") == 0);
    room = -1;
    integral_rom_cycle_room(slots, &room, status, sizeof(status), -1);
    CHECK(room == 1);
    integral_rom_cycle_room(slots, &room, status, sizeof(status), 1);
    CHECK(room == 0 && strcmp(status, "ROOM SLOT ROM1") == 0);
    char invalid_config[1100];
    snprintf(invalid_config, sizeof(invalid_config), "%s/blocked.conf", gb_path);
    integral_rom_cycle_local(slots, indices, invalid_config, status, sizeof(status), 0, 1);
    CHECK(strcmp(status, "LOCAL CONFIG SAVE FAILED") == 0 && indices[0] == 1);
    memset(slots, 0, sizeof(slots));
    integral_rom_cycle_local(slots, indices, config, status, sizeof(status), 0, 1);
    CHECK(strcmp(status, "NO ROM1-8 SET") == 0 && indices[0] == 1);
    integral_rom_cycle_n64(slots, &n64_index, status, sizeof(status), 1);
    CHECK(strcmp(status, "NO N64 ROM SET") == 0 && n64_index == 3);
    integral_rom_cycle_room(slots, &room, status, sizeof(status), 1);
    CHECK(strcmp(status, "NO ROM1-8 SET") == 0 && room == 0);
    CHECK(remove(config) == 0 && remove(gb_path) == 0 && remove(n64_path) == 0);
    puts("user config/catalog test passed");
    return 0;
}

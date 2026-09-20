/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ROM_CATALOG_H
#define INTEGRAL_CLIENT_ROM_CATALOG_H

#include "client_config.h"
#include "http_client.h"
#include <stdbool.h>

bool slot_is_supported_n64(const IntegralConfigRomSlot *slot);
bool slot_is_supported_gb(const IntegralConfigRomSlot *slot);
bool slot_has_server_registration(const IntegralConfigRomSlot *slot);
bool slot_has_pending_local_rom(const IntegralConfigRomSlot *slot);
bool discard_pending_rom_slots(IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS]);
void merge_server_rom_slots(IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS],
                            const IntegralApiRomSlot *server_slots, const char *rom_folder);
void validate_local_indices(const IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS],
                             int local_indices[2], int *room_index);
int scan_rom_paths(const char *folder, char entries[][INTEGRAL_CONFIG_PATH_MAX], unsigned capacity);

void integral_rom_cycle_local(const IntegralConfigRomSlot *slots, int local_indices[2],
    const char *config_path, char *status, size_t status_size, unsigned slot_index, int delta);
void integral_rom_cycle_n64(const IntegralConfigRomSlot *slots, int *n64_index,
    char *status, size_t status_size, int delta);
void integral_rom_cycle_transfer(const IntegralConfigRomSlot *slots, int transfer_indices[4],
    char *status, size_t status_size, unsigned transfer_slot, int delta);
void integral_rom_cycle_room(const IntegralConfigRomSlot *slots, int *room_index,
    char *status, size_t status_size, int delta);

#endif

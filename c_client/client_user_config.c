/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_user_config.h"
#include "client_rom_catalog.h"

int load_user_config(const char *path, IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS],
                      IntegralConfigKeys *keys, int local_indices[2])
{
    local_indices[0] = -1;
    local_indices[1] = -1;
    IntegralConfigLocal local;
    if (integral_config_load_local(path, &local) == 0) {
        local_indices[0] = local.slot1_index;
        local_indices[1] = local.slot2_index;
    }
    integral_keys_defaults(keys);
    if (integral_config_load_keys(path, keys) == 0) {
        integral_keys_apply_defaults_for_missing(keys);
    }
    int result = integral_config_load_rom_slots(path, slots, INTEGRAL_CONFIG_ROM_SLOTS);
    if (discard_pending_rom_slots(slots)) {
        (void)integral_config_save_rom_slots(path, slots, INTEGRAL_CONFIG_ROM_SLOTS);
    }
    return result;
}

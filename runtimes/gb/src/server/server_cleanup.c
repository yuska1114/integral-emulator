/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "server_cleanup.h"

#include "file_util.h"
#include "net_compat.h"

#define INTEGRAL_GB_RUNTIME_PERIODIC_SAVE_INTERVAL_US 1000000ULL

static bool periodic_save_due(const IntegralGBRuntimeSlot *slot)
{
    if (!slot) {
        return false;
    }
    uint64_t now_us = integral_gb_runtime_now_us();
    return slot->last_battery_save_us == 0 ||
           now_us - slot->last_battery_save_us >= INTEGRAL_GB_RUNTIME_PERIODIC_SAVE_INTERVAL_US;
}

void integral_gb_runtime_server_send_slot2_save_if_dirty(IntegralGBRuntimeSlot *slot,
                                               IntegralGBRuntimeStreamServer *stream_server,
                                               const char *save_path)
{
    if (!slot || !stream_server || !save_path || !integral_gb_runtime_slot_battery_dirty(slot)) {
        return;
    }
    if (!periodic_save_due(slot)) {
        return;
    }
    if (integral_gb_runtime_slot_save_battery(slot) == 0 &&
        integral_gb_runtime_stream_server_send_save_file(stream_server, save_path) == 0) {
        integral_gb_runtime_slot_clear_battery_dirty(slot);
    }
}

void integral_gb_runtime_server_send_slot2_save_on_shutdown(IntegralGBRuntimeSlot *slot,
                                                  IntegralGBRuntimeStreamServer *stream_server,
                                                  const char *save_path)
{
    if (!slot || !stream_server || !save_path) {
        return;
    }
    if (!integral_gb_runtime_slot_battery_dirty(slot) && stream_server->save_returns_sent != 0) {
        return;
    }
    if (integral_gb_runtime_slot_save_battery(slot) == 0 &&
        integral_gb_runtime_stream_server_send_save_file(stream_server, save_path) == 0) {
        integral_gb_runtime_slot_clear_battery_dirty(slot);
    }
}

void integral_gb_runtime_server_save_local_slot_if_dirty(IntegralGBRuntimeSlot *slot)
{
    if (!slot || !integral_gb_runtime_slot_battery_dirty(slot)) {
        return;
    }
    if (!periodic_save_due(slot)) {
        return;
    }
    if (integral_gb_runtime_slot_save_battery(slot) == 0) {
        integral_gb_runtime_slot_clear_battery_dirty(slot);
    }
}

void integral_gb_runtime_server_remove_temp_slot2_uploads(bool slot2_from_client,
                                                const char *rom_path,
                                                const char *save_path)
{
    if (!slot2_from_client) {
        return;
    }
    if (rom_path && rom_path[0] != '\0') {
        integral_gb_runtime_unlink(rom_path);
    }
    if (save_path && save_path[0] != '\0') {
        integral_gb_runtime_unlink(save_path);
    }
}

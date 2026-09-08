/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SERVER_CLEANUP_H
#define INTEGRAL_GB_RUNTIME_SERVER_CLEANUP_H

#include <stdbool.h>

#include "slot.h"
#include "stream_server.h"

void integral_gb_runtime_server_send_slot2_save_if_dirty(IntegralGBRuntimeSlot *slot,
                                               IntegralGBRuntimeStreamServer *stream_server,
                                               const char *save_path);
void integral_gb_runtime_server_send_slot2_save_on_shutdown(IntegralGBRuntimeSlot *slot,
                                                  IntegralGBRuntimeStreamServer *stream_server,
                                                  const char *save_path);
void integral_gb_runtime_server_save_local_slot_if_dirty(IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_server_remove_temp_slot2_uploads(bool slot2_from_client,
                                                const char *rom_path,
                                                const char *save_path);

#endif

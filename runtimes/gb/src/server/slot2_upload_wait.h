/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SLOT2_UPLOAD_WAIT_H
#define INTEGRAL_GB_RUNTIME_SLOT2_UPLOAD_WAIT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <SDL.h>

#include "server_options.h"
#include "stream_server.h"

typedef struct IntegralGBRuntimeSlot2UploadWaitResult {
    bool return_to_menu_requested;
} IntegralGBRuntimeSlot2UploadWaitResult;

void integral_gb_runtime_slot2_upload_advertise_mdns_if_due(unsigned port, Uint32 *next_mdns_ms);

int integral_gb_runtime_slot2_upload_wait(IntegralGBRuntimeStreamServer *stream_server,
                                const ServerOptions *options,
                                char *rom_out,
                                size_t rom_out_size,
                                char *save_out,
                                size_t save_out_size,
                                uint32_t *client_unix_time,
                                IntegralGBRuntimeSlot2UploadWaitResult *result);

#endif

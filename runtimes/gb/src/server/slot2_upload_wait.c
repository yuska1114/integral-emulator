/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slot2_upload_wait.h"

#include <string.h>

#include "mdns.h"
#include "net_compat.h"
#include "string_util.h"
#include "wait_window.h"

typedef struct Slot2WaitContext {
    IntegralGBRuntimeWaitWindow *window;
    unsigned port;
    Uint32 next_mdns_ms;
} Slot2WaitContext;

static void wait_display_host(const ServerOptions *options, char *out, size_t out_size)
{
    if (strcmp(options->bind, "0.0.0.0") == 0 ||
        strcmp(options->bind, "127.0.0.1") == 0) {
        if (integral_gb_runtime_detect_local_ipv4(out, out_size)) {
            return;
        }
    }
    if (!integral_gb_runtime_copy_text(out, out_size, options->bind)) {
        (void)integral_gb_runtime_copy_text(out, out_size, "127.0.0.1");
    }
}

void integral_gb_runtime_slot2_upload_advertise_mdns_if_due(unsigned port, Uint32 *next_mdns_ms)
{
    Uint32 now = SDL_GetTicks();
    if (*next_mdns_ms != 0 && (Sint32)(now - *next_mdns_ms) < 0) {
        return;
    }
    (void)integral_gb_runtime_mdns_advertise_once(port);
    *next_mdns_ms = now + 1000u;
}

static bool wait_window_tick(void *user)
{
    Slot2WaitContext *context = (Slot2WaitContext *)user;
    integral_gb_runtime_slot2_upload_advertise_mdns_if_due(context->port, &context->next_mdns_ms);
    if (!context->window) {
        return true;
    }
    return integral_gb_runtime_wait_window_poll_and_render(context->window);
}

int integral_gb_runtime_slot2_upload_wait(IntegralGBRuntimeStreamServer *stream_server,
                                const ServerOptions *options,
                                char *rom_out,
                                size_t rom_out_size,
                                char *save_out,
                                size_t save_out_size,
                                uint32_t *client_unix_time,
                                IntegralGBRuntimeSlot2UploadWaitResult *result)
{
    if (result) {
        result->return_to_menu_requested = false;
    }

    IntegralGBRuntimeWaitWindow *wait_window = NULL;
    char wait_host[128];
    wait_display_host(options, wait_host, sizeof(wait_host));
    if (options->display) {
        if (integral_gb_runtime_wait_window_open(&wait_window, wait_host, options->port, options->escape_key) != 0) {
            return -1;
        }
    }

    Slot2WaitContext wait_context = {
        .window = wait_window,
        .port = options->port,
        .next_mdns_ms = 0,
    };
    if (integral_gb_runtime_stream_server_wait_for_slot2_upload(stream_server,
                                                      rom_out,
                                                      rom_out_size,
                                                      save_out,
                                                      save_out_size,
                                                      client_unix_time,
                                                      wait_window_tick,
                                                      &wait_context) != 0) {
        if (result) {
            result->return_to_menu_requested =
                integral_gb_runtime_wait_window_return_to_menu_requested(wait_window);
        }
        integral_gb_runtime_wait_window_close(wait_window);
        return -1;
    }

    integral_gb_runtime_wait_window_close(wait_window);
    return 0;
}

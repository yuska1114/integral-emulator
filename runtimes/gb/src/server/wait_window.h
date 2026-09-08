/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_WAIT_WINDOW_H
#define INTEGRAL_GB_RUNTIME_WAIT_WINDOW_H

#include <stdbool.h>

#include <SDL.h>

typedef struct IntegralGBRuntimeWaitWindow IntegralGBRuntimeWaitWindow;

int integral_gb_runtime_wait_window_open(IntegralGBRuntimeWaitWindow **window_out,
                               const char *host,
                               unsigned port,
                               SDL_Keycode escape_key);
bool integral_gb_runtime_wait_window_poll_and_render(IntegralGBRuntimeWaitWindow *window);
bool integral_gb_runtime_wait_window_return_to_menu_requested(const IntegralGBRuntimeWaitWindow *window);
void integral_gb_runtime_wait_window_close(IntegralGBRuntimeWaitWindow *window);

#endif

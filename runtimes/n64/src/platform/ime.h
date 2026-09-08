/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_PLATFORM_IME_H
#define INTEGRAL_N64_RUNTIME_PLATFORM_IME_H

#include <stdbool.h>

/* Disables SDL text composition and prepares physical-scancode input. */
bool integral_n64_runtime_ime_force_direct_input(void);

#endif

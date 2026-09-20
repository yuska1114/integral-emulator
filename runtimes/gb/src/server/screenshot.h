/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SCREENSHOT_H
#define INTEGRAL_GB_RUNTIME_SCREENSHOT_H

#include <stddef.h>

#include "slot.h"

int integral_gb_runtime_screenshot_save_slot(const IntegralGBRuntimeSlot *slot, char *out_path, size_t out_path_size);
int integral_gb_runtime_screenshot_save_pair(const IntegralGBRuntimeSlot *slot1,
    const IntegralGBRuntimeSlot *slot2, const char *mode, const char *role,
    char *out_path, size_t out_path_size);

#endif

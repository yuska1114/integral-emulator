/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_COMMON_LOG_UTIL_H
#define INTEGRAL_GB_RUNTIME_COMMON_LOG_UTIL_H

#include <stdbool.h>

#define INTEGRAL_GB_RUNTIME_LOG_CONFIG_FILE "config/gb_runtime_log.conf"

bool integral_gb_runtime_log_enabled(void);
void integral_gb_runtime_log_redirect_stdio(const char *component);

#endif

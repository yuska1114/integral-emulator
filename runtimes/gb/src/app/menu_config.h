/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_APP_MENU_CONFIG_H
#define INTEGRAL_GB_RUNTIME_APP_MENU_CONFIG_H

#include "menu_state.h"

const char *integral_gb_runtime_menu_config_path(void);
void integral_gb_runtime_menu_load_config_file(IntegralGBRuntimeMenu *menu);
int integral_gb_runtime_menu_save_config_file(const IntegralGBRuntimeMenu *menu);
void integral_gb_runtime_menu_refresh_key_descriptions(IntegralGBRuntimeMenu *menu);

#endif

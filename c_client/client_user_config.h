/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_USER_CONFIG_H
#define INTEGRAL_CLIENT_USER_CONFIG_H

#include "client_config.h"

int load_user_config(const char *path, IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS],
                      IntegralConfigKeys *keys, int local_indices[2]);

#endif

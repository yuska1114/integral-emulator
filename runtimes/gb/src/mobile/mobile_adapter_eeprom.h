/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_H
#define INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_SIZE 0xC0u

bool integral_gb_runtime_mobile_adapter_eeprom_write_synthetic_isp(uint8_t *config,
                                                           size_t config_size);
bool integral_gb_runtime_mobile_adapter_eeprom_validate(const uint8_t *config,
                                                size_t config_size);

#endif

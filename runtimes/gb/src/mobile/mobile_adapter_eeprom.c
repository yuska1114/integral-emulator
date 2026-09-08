/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_adapter_eeprom.h"

#include <string.h>

#define INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_CHECKSUM_OFFSET 0xBEu

static uint16_t eeprom_checksum(const uint8_t *config)
{
    uint16_t sum = 0;
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_CHECKSUM_OFFSET; i++) {
        sum = (uint16_t)(sum + config[i]);
    }
    return sum;
}

bool integral_gb_runtime_mobile_adapter_eeprom_write_synthetic_isp(uint8_t *config,
                                                           size_t config_size)
{
    if (!config || config_size < INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_SIZE) {
        return false;
    }
    memset(config, 0, INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_SIZE);
    config[0x00] = 'M';
    config[0x01] = 'A';
    config[0x02] = 0x81;
    static const uint8_t dns[4] = {127, 0, 0, 1};
    memcpy(config + 0x04, dns, sizeof(dns));
    memcpy(config + 0x08, dns, sizeof(dns));
    memcpy(config + 0x0C, "g000000000", 10);
    memcpy(config + 0x2C, "probe@local.invalid", 19);
    memcpy(config + 0x4A, "mail.local.invalid", 18);
    memcpy(config + 0x5E, "pop.local.invalid", 17);

    memset(config + 0x76, 0xFF, 0x48);
    static const uint8_t phone[8] = {
        0xA9, 0x67, 0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    };
    memcpy(config + 0x76, phone, sizeof(phone));
    memcpy(config + 0x7E, "DION PDC/CDMAONE", 16);

    uint16_t sum = eeprom_checksum(config);
    config[0xBE] = (uint8_t)(sum >> 8);
    config[0xBF] = (uint8_t)sum;
    return true;
}

bool integral_gb_runtime_mobile_adapter_eeprom_validate(const uint8_t *config,
                                                size_t config_size)
{
    if (!config || config_size < INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_SIZE) {
        return false;
    }
    uint16_t stored = (uint16_t)((uint16_t)config[0xBE] << 8 | config[0xBF]);
    return config[0] == 'M' && config[1] == 'A' &&
           stored == eeprom_checksum(config);
}

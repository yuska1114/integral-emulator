/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_adapter_eeprom.h"
#include "mobile_bounded_buffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t eeprom[INTEGRAL_GB_RUNTIME_MOBILE_ADAPTER_EEPROM_SIZE];
    assert(integral_gb_runtime_mobile_adapter_eeprom_write_synthetic_isp(eeprom, sizeof(eeprom)));
    assert(integral_gb_runtime_mobile_adapter_eeprom_validate(eeprom, sizeof(eeprom)));
    eeprom[0x10] ^= 1u;
    assert(!integral_gb_runtime_mobile_adapter_eeprom_validate(eeprom, sizeof(eeprom)));
    assert(!integral_gb_runtime_mobile_adapter_eeprom_write_synthetic_isp(eeprom, 8));

    uint8_t storage_a[8];
    uint8_t storage_b[8];
    IntegralGBRuntimeMobileBoundedBuffer connections[2];
    integral_gb_runtime_mobile_bounded_buffer_init(&connections[0], storage_a, sizeof(storage_a));
    integral_gb_runtime_mobile_bounded_buffer_init(&connections[1], storage_b, sizeof(storage_b));

    static const uint8_t first[] = {1, 2, 3, 4};
    static const uint8_t second[] = {9, 8};
    assert(integral_gb_runtime_mobile_bounded_buffer_append(&connections[0], first, sizeof(first)));
    assert(integral_gb_runtime_mobile_bounded_buffer_append(&connections[1], second, sizeof(second)));
    assert(!integral_gb_runtime_mobile_bounded_buffer_append(&connections[0], first, 5));
    assert(integral_gb_runtime_mobile_bounded_buffer_seal(&connections[0]));

    uint8_t output[8] = {0};
    assert(integral_gb_runtime_mobile_bounded_buffer_read(&connections[0], output, 2) == 2);
    assert(memcmp(output, first, 2) == 0);
    assert(connections[1].cursor == 0 && connections[1].length == sizeof(second));

    integral_gb_runtime_mobile_bounded_buffer_cancel(&connections[1]);
    assert(integral_gb_runtime_mobile_bounded_buffer_read(&connections[1], output, sizeof(output)) == -1);
    integral_gb_runtime_mobile_bounded_buffer_reset(&connections[1]);
    assert(integral_gb_runtime_mobile_bounded_buffer_append(&connections[1], second, sizeof(second)));
    integral_gb_runtime_mobile_bounded_buffer_timeout(&connections[1]);
    assert(integral_gb_runtime_mobile_bounded_buffer_read(&connections[1], output, sizeof(output)) == -1);

    integral_gb_runtime_mobile_bounded_buffer_reset(&connections[0]);
    assert(connections[0].length == 0 && connections[0].cursor == 0);
    assert(integral_gb_runtime_mobile_bounded_buffer_append(&connections[0], first, sizeof(first)));
    assert(integral_gb_runtime_mobile_bounded_buffer_truncate(&connections[0], 3));
    assert(integral_gb_runtime_mobile_bounded_buffer_seal(&connections[0]));
    assert(integral_gb_runtime_mobile_bounded_buffer_read(&connections[0], output, sizeof(output)) == 3);
    assert(integral_gb_runtime_mobile_bounded_buffer_read(&connections[0], output, sizeof(output)) == -2);

    puts("mobile production core tests passed");
    return 0;
}

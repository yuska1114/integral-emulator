/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rom_profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_model(uint8_t cgb,
                         uint8_t sgb,
                         uint8_t old_licensee,
                         int expected_result,
                         GB_model_t expected_model)
{
    IntegralGBRuntimeRomProfile profile = {
        .cgb_flag = cgb,
        .sgb_flag = sgb,
        .old_licensee = old_licensee,
    };
    GB_model_t model = GB_MODEL_DMG_B;
    IntegralGBRuntimeRomModelReason reason = INTEGRAL_GB_RUNTIME_ROM_MODEL_DMG_DEFAULT;
    char error[128] = {0};
    int result = integral_gb_runtime_rom_profile_select_model(
        &profile, &model, &reason, error, sizeof(error));
    assert(result == expected_result);
    if (result == 0) {
        assert(model == expected_model);
    }
    else {
        assert(error[0] != '\0');
    }
}

int main(void)
{
    expect_model(0x80, 0x03, 0x33, 0, GB_MODEL_CGB_E);
    expect_model(0xC0, 0x03, 0x33, 0, GB_MODEL_CGB_E);
    expect_model(0x00, 0x03, 0x33, 0, GB_MODEL_SGB2);
    expect_model(0x00, 0x03, 0x01, 0, GB_MODEL_DMG_B);
    expect_model(0x00, 0x00, 0x33, 0, GB_MODEL_DMG_B);
    expect_model(0x00, 0x00, 0x01, 0, GB_MODEL_DMG_B);
    expect_model(0x81, 0x03, 0x33, -1, GB_MODEL_DMG_B);
    expect_model(0xFF, 0x00, 0x01, -1, GB_MODEL_DMG_B);

    assert(strcmp(integral_gb_runtime_rom_model_reason_name(INTEGRAL_GB_RUNTIME_ROM_MODEL_SGB_HEADER),
                  "sgb_header") == 0);
    puts("rom profile tests passed");
    return 0;
}

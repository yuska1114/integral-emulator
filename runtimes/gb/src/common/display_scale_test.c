/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "display_scale.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned scale = 99;
    char text[16];
    if (integral_display_scale_parse("auto", &scale) != 0 || scale != 0u ||
        integral_display_scale_parse("6", &scale) != 0 || scale != 6u ||
        integral_display_scale_parse("0", &scale) == 0 ||
        integral_display_scale_parse("7", &scale) == 0 ||
        integral_display_scale_resolve(0u, 1920, 1040, 480, 480) != 2u ||
        integral_display_scale_resolve(0u, 800, 600, 320, 144) != 2u ||
        integral_display_scale_resolve(5u, 320, 200, 480, 480) != 5u ||
        integral_display_scale_resolve(0u, 0, 0, 160, 144) != 1u) {
        return 1;
    }
    integral_display_scale_format(0u, text, sizeof(text));
    if (strcmp(text, "AUTO") != 0) return 1;
    integral_display_scale_format(6u, text, sizeof(text));
    if (strcmp(text, "6") != 0) return 1;
    puts("display scale test passed");
    return 0;
}

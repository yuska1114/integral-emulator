/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "log_util.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    bool expected = strcmp(argv[1], "enabled") == 0;
    bool actual = integral_gb_runtime_log_enabled();
    if (actual != expected) {
        fprintf(stderr, "GB Runtime log config mismatch: expected=%d actual=%d\n", expected, actual);
        return 1;
    }
    return 0;
}

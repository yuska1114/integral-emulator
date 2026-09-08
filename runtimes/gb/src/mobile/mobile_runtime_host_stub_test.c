/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_runtime_host.h"

int integral_gb_runtime_mobile_runtime_main(int argc, char **argv)
{
    return argc == 1 && argv && argv[0] ? 0 : 1;
}

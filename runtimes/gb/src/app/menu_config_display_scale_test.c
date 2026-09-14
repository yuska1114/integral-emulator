/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "menu_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int main(void)
{
#ifdef _WIN32
    const char *path = "gb-runtime-display-scale-test.conf";
#else
    char directory[] = "/tmp/integral-gb-scale-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    char path_buffer[512];
    snprintf(path_buffer, sizeof(path_buffer), "%s/settings.conf", directory);
    const char *path = path_buffer;
#endif
    if (SDL_setenv("GB_RUNTIME_KEY_CONFIG", path, 1) != 0) return 1;

    IntegralGBRuntimeMenu saved;
    memset(&saved, 0, sizeof(saved));
    saved.display_scale = 4u;
    if (integral_gb_runtime_menu_save_config_file(&saved) != 0) return 2;

    IntegralGBRuntimeMenu loaded;
    memset(&loaded, 0, sizeof(loaded));
    integral_gb_runtime_menu_load_config_file(&loaded);
    if (loaded.display_scale != 4u) return 3;

    (void)remove(path);
#ifndef _WIN32
    (void)rmdir(directory);
#endif
    puts("GB Runtime display scale config persistence test passed");
    return 0;
}

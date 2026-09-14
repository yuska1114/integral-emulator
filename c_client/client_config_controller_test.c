/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_config.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <unistd.h>
#endif

int main(void)
{
#ifdef _WIN32
    char path[] = "integral-controller-config-test.conf";
#else
    char directory[] = "/tmp/integral-controller-config-XXXXXX";
    if (!mkdtemp(directory)) return 1;
    char path[512];
    snprintf(path, sizeof(path), "%s/integral_client.conf", directory);
#endif
    (void)remove(path);
    IntegralConfigWindow default_window = {0};
    if (integral_config_load_window(path, &default_window) != 0 ||
        default_window.width != 360u || default_window.height != 360u) return 2;
    IntegralConfigKeys expected;
    memset(&expected, 0, sizeof(expected));
    snprintf(expected.slot1, sizeof(expected.slot1),
             "JOY@0123456789abcdef-0:B0,JOY@0123456789abcdef-0:B1,JOY@0123456789abcdef-0:H0UP,JOY@0123456789abcdef-0:H0DOWN,JOY@0123456789abcdef-0:A0+,JOY@0123456789abcdef-0:A0-,RETURN,RSHIFT");
    snprintf(expected.slot2, sizeof(expected.slot2),
             "PAD@fedcba9876543210-0:DPAD_RIGHT,PAD@fedcba9876543210-0:DPAD_LEFT,PAD@fedcba9876543210-0:DPAD_UP,PAD@fedcba9876543210-0:DPAD_DOWN,PAD@fedcba9876543210-0:A,PAD@fedcba9876543210-0:B,PAD@fedcba9876543210-0:BACK,PAD@fedcba9876543210-0:START");
    snprintf(expected.n64_p1, sizeof(expected.n64_p1),
             "PAD@fedcba9876543210-0:DPAD_RIGHT,PAD@fedcba9876543210-0:DPAD_LEFT,PAD@fedcba9876543210-0:DPAD_UP,PAD@fedcba9876543210-0:DPAD_DOWN,PAD@fedcba9876543210-0:START,PAD@fedcba9876543210-0:LSHOULDER,PAD@fedcba9876543210-0:B,PAD@fedcba9876543210-0:A,PAD@fedcba9876543210-0:X,PAD@fedcba9876543210-0:Y,PAD@fedcba9876543210-0:RY-,PAD@fedcba9876543210-0:RY+,PAD@fedcba9876543210-0:RSHOULDER,PAD@fedcba9876543210-0:BACK,PAD@fedcba9876543210-0:LX+,PAD@fedcba9876543210-0:LX-,PAD@fedcba9876543210-0:LY-,PAD@fedcba9876543210-0:LY+");
    snprintf(expected.fast, sizeof(expected.fast), "F");
    snprintf(expected.screenshot, sizeof(expected.screenshot), "P");
    snprintf(expected.escape, sizeof(expected.escape), "Escape");
    snprintf(expected.turbo_hold, sizeof(expected.turbo_hold), "B");
    snprintf(expected.reset, sizeof(expected.reset), "I");
    IntegralConfigWindow expected_window = {.width = 777u, .height = 611u};
    if (integral_config_save_window(path, &expected_window) != 0) return 2;
    if (integral_config_save_keys(path, &expected) != 0) return 2;
    IntegralConfigKeys actual;
    IntegralConfigWindow actual_window;
    memset(&actual, 0, sizeof(actual));
    if (integral_config_load_keys(path, &actual) != 0 ||
        integral_config_load_window(path, &actual_window) != 0 ||
        memcmp(&expected, &actual, sizeof(expected)) != 0 ||
        actual_window.width != expected_window.width ||
        actual_window.height != expected_window.height) return 3;
    FILE *config = fopen(path, "r");
    if (!config) return 4;
    bool saw_width = false;
    bool saw_height = false;
    char line[256];
    while (fgets(line, sizeof(line), config)) {
        if (strcmp(line, "window.width=777\n") == 0) saw_width = true;
        if (strcmp(line, "window.height=611\n") == 0) saw_height = true;
        if (strncmp(line, "display.scale=", 14u) == 0) return 5;
    }
    if (fclose(config) != 0 || !saw_width || !saw_height) return 4;
    (void)remove(path);
#ifndef _WIN32
    (void)rmdir(directory);
#endif
    printf("C client controller config persistence test passed\n");
    return 0;
}

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
    if (integral_config_ir_off_delay(path) != 32) return 50;
    const char *ir_values[] = {"0", "32", "256", "-1", "257", "32junk", ""};
    const unsigned ir_expected[] = {0, 32, 256, 32, 32, 32, 32};
    for (unsigned i = 0; i < sizeof(ir_expected)/sizeof(ir_expected[0]); i++) {
        FILE *file = fopen(path, "w");
        if (!file) return 51;
        fprintf(file, "gb.ir_off_delay_ticks=%s\n", ir_values[i]);
        fclose(file);
        if (integral_config_ir_off_delay(path) != ir_expected[i]) return 52;
        IntegralConfigWindow window = {.width = 360, .height = 360};
        if (integral_config_save_window(path, &window) != 0 ||
            integral_config_ir_off_delay(path) != ir_expected[i]) return 53;
    }
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
    snprintf(expected.n64_p2, sizeof(expected.n64_p2), "%s", expected.n64_p1);
    snprintf(expected.n64_p3, sizeof(expected.n64_p3), "%s", expected.n64_p1);
    snprintf(expected.n64_p4, sizeof(expected.n64_p4), "%s", expected.n64_p1);
    char *extra_ports[] = {expected.n64_p2, expected.n64_p3, expected.n64_p4};
    for (unsigned i = 0; i < 3; i++) {
        for (char *p = extra_ports[i]; (p = strstr(p, "-0:")) != NULL; p += 3)
            p[1] = (char)('1' + i);
    }
    snprintf(expected.screenshot, sizeof(expected.screenshot), "P");
    snprintf(expected.escape, sizeof(expected.escape), "Escape");
    snprintf(expected.turbo_hold, sizeof(expected.turbo_hold), "B");
    snprintf(expected.reset, sizeof(expected.reset), "O");
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
    integral_keys_reset_editable_defaults(&actual);
    if (strcmp(actual.n64_p2, expected.n64_p2) || strcmp(actual.n64_p3, expected.n64_p3) ||
        strcmp(actual.n64_p4, expected.n64_p4)) return 6;
    if (integral_config_save_keys(path, &actual) || integral_config_save_window(path, &expected_window) ||
        integral_config_load_keys(path, &actual) || strcmp(actual.n64_p4, expected.n64_p4)) return 7;
    integral_keys_defaults(&actual);
    if (actual.n64_p2[0] || actual.n64_p3[0] || actual.n64_p4[0]) return 8;
    (void)remove(path);
#ifndef _WIN32
    (void)rmdir(directory);
#endif
    printf("C client controller config persistence test passed\n");
    return 0;
}

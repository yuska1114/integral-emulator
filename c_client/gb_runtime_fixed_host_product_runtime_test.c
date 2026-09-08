/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_product_runtime.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

int main(void)
{
    IntegralGBRuntimeFixedHostProductRuntime runtime;
    unsigned cycle;
    CHECK(!integral_gb_runtime_fixed_host_product_runtime_init(&runtime, 0));
    for (cycle = 0u; cycle < 100u; cycle++) {
        CHECK(integral_gb_runtime_fixed_host_product_runtime_init(
            &runtime, cycle & 1u ? INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_HOST
                                : INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_REMOTE));
        integral_gb_runtime_fixed_host_product_runtime_set_button(&runtime, 0x10u, true);
        CHECK(runtime.buttons == 0x10u);
        integral_gb_runtime_fixed_host_product_runtime_request_exit(&runtime);
        CHECK(runtime.exit_confirming && !runtime.exit_confirm_yes);
        CHECK(runtime.buttons == 0u);
        CHECK(integral_gb_runtime_fixed_host_product_runtime_take_neutral(&runtime));
        CHECK(integral_gb_runtime_fixed_host_product_runtime_select_exit(&runtime) ==
              INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONTINUE);
        CHECK(!runtime.exit_confirming);
        integral_gb_runtime_fixed_host_product_runtime_request_exit(&runtime);
        integral_gb_runtime_fixed_host_product_runtime_toggle_exit_selection(&runtime);
        CHECK(runtime.exit_confirming && runtime.exit_confirm_yes);
        CHECK(integral_gb_runtime_fixed_host_product_runtime_select_exit(&runtime) ==
              INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONFIRMED);
        integral_gb_runtime_fixed_host_product_runtime_cancel_exit(&runtime);
        integral_gb_runtime_fixed_host_product_runtime_focus_lost(&runtime);
        CHECK(runtime.buttons == 0u);
        CHECK(integral_gb_runtime_fixed_host_product_runtime_take_neutral(&runtime));
        CHECK(!integral_gb_runtime_fixed_host_product_runtime_take_neutral(&runtime));
        integral_gb_runtime_fixed_host_product_runtime_set_button(&runtime, 0x20u, true);
        CHECK(runtime.buttons == 0u);
        integral_gb_runtime_fixed_host_product_runtime_stop(&runtime);
        CHECK(!runtime.running && runtime.buttons == 0u && runtime.role == 0);
    }
    printf("PASS fixed Host product lifecycle cycles=100 checks=%u\n", checks);
    return 0;
}

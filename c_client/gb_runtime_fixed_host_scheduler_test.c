/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_scheduler.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

int main(void)
{
    IntegralGBRuntimeFixedHostScheduler scheduler;
    uint8_t host = 0xffu, remote = 0xffu;
    CHECK(!integral_gb_runtime_fixed_host_scheduler_init(&scheduler, "", "A", 1u, 1u));
    CHECK(integral_gb_runtime_fixed_host_scheduler_init(&scheduler, "A.|A", "..|B", 1u, 2u));
    CHECK(integral_gb_runtime_fixed_host_scheduler_frame(&scheduler, 1u, 2u, &host, &remote));
    CHECK(host == 1u && remote == 2u);
    integral_gb_runtime_fixed_host_scheduler_start_test_automation(&scheduler);
    CHECK(scheduler.macro_running);
    CHECK(integral_gb_runtime_fixed_host_scheduler_frame(&scheduler, 1u, 2u, &host, &remote));
    CHECK(host == 0x10u && remote == 0u);
    integral_gb_runtime_fixed_host_scheduler_set_paused(&scheduler, true);
    CHECK(integral_gb_runtime_fixed_host_scheduler_frame(&scheduler, 1u, 2u, &host, &remote));
    CHECK(host == 0u && remote == 0u && scheduler.step_frame == 1u);
    integral_gb_runtime_fixed_host_scheduler_set_paused(&scheduler, false);
    for (unsigned index = 0u; index < 12u; index++)
        CHECK(integral_gb_runtime_fixed_host_scheduler_frame(&scheduler, 1u, 2u, &host, &remote));
    CHECK(!scheduler.macro_running);
    integral_gb_runtime_fixed_host_scheduler_stop(&scheduler);
    CHECK(scheduler.step_frames == 0u && !scheduler.macro_running);
    printf("PASS fixed Host Host-owned scheduler checks=%u\n", checks);
    return 0;
}

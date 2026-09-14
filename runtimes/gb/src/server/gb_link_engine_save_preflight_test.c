/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_link_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned checks;

#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

static void put_u64_le(uint8_t *data, uint64_t value)
{
    for (unsigned index = 0; index < 8u; index++) {
        data[index] = (uint8_t)(value >> (index * 8u));
    }
}

static uint64_t get_u64_le(const uint8_t *data)
{
    uint64_t value = 0u;
    for (unsigned index = 0; index < 8u; index++) {
        value |= (uint64_t)data[index] << (index * 8u);
    }
    return value;
}

static void prepare_vba64_rtc_save(uint8_t *save, uint8_t seconds,
                                   uint8_t minutes, uint8_t hours,
                                   uint16_t days, uint64_t timestamp)
{
    uint8_t *rtc = save + 8192u;
    rtc[0] = seconds;
    rtc[4] = minutes;
    rtc[8] = hours;
    rtc[12] = (uint8_t)days;
    rtc[16] = (uint8_t)((days >> 8u) & 1u);
    memcpy(rtc + 20u, rtc, 20u);
    put_u64_le(rtc + 40u, timestamp);
}

int main(int argc, char **argv)
{
    if (argc != 4 && argc != 6) {
        fprintf(stderr,
                "usage: %s RTC_ROM NON_RTC_ROM SGB_ROM [REAL_RTC_ROM LOCAL_EXIT_SAVE]\n",
                argv[0]);
        return 2;
    }

    CHECK(integral_gb_runtime_link_save_preflight(argv[1], 8192u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_RTC_MISSING);
    CHECK(integral_gb_runtime_link_save_preflight(argv[1], 8192u + 48u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK);
    CHECK(integral_gb_runtime_link_save_preflight(argv[1], 8192u + 47u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_RTC_UNSUPPORTED);
    CHECK(integral_gb_runtime_link_save_preflight(argv[1], 8192u + 49u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_RTC_UNSUPPORTED);
    CHECK(integral_gb_runtime_link_save_preflight(argv[2], 0u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK);
    CHECK(integral_gb_runtime_link_save_preflight(argv[2], 8192u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK);
    /* Client-side preflight must not need the Runtime-only SGB2 boot resource. */
#ifdef _WIN32
    CHECK(_putenv_s("INTEGRAL_EMULATOR_GB_RUNTIME_SGB2_BOOT_ROM",
                    "missing-sgb2-boot.bin") == 0);
#else
    CHECK(setenv("INTEGRAL_EMULATOR_GB_RUNTIME_SGB2_BOOT_ROM",
                 "missing-sgb2-boot.bin", 1) == 0);
#endif
    CHECK(integral_gb_runtime_link_save_preflight(argv[3], 0u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK);
    CHECK(integral_gb_runtime_link_save_preflight("missing.gb", 8192u) ==
          INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_ROM_ERROR);

    {
        enum { rtc_save_size = 8192 + 48 };
        const uint64_t base_timestamp = UINT64_C(1700000000);
        const uint64_t target_timestamp =
            base_timestamp + UINT64_C(32) * UINT64_C(86400) + UINT64_C(5);
        uint8_t host_save[rtc_save_size] = {0};
        uint8_t remote_save[rtc_save_size] = {0};
        IntegralGBRuntimeLinkEngine engine = {0};
        IntegralGBRuntimeLinkSnapshot snapshot = {0};
        IntegralGBRuntimeLinkRTCRegisters host_rtc, remote_rtc;
        prepare_vba64_rtc_save(host_save, 3u, 2u, 1u, 3u, base_timestamp);
        prepare_vba64_rtc_save(remote_save, 30u, 20u, 10u, 7u,
                               base_timestamp - UINT64_C(3600));
        IntegralGBRuntimeLinkEngineConfig config = {
            .rom_a = argv[1],
            .rom_b = argv[1],
            .save_a = host_save,
            .save_a_size = sizeof(host_save),
            .save_b = remote_save,
            .save_b_size = sizeof(remote_save),
            .rtc_target_unix = target_timestamp,
            .display_role = 0,
        };
        CHECK(integral_gb_runtime_link_engine_init(&engine, &config) == 0);
        CHECK(integral_gb_runtime_link_engine_get_rtc(&engine, 0u, &host_rtc) == 0);
        CHECK(integral_gb_runtime_link_engine_get_rtc(&engine, 1u, &remote_rtc) == 0);
        CHECK(host_rtc.days == 35u && host_rtc.hours == 1u &&
              host_rtc.minutes == 2u && host_rtc.seconds == 8u);
        CHECK(remote_rtc.days == 39u && remote_rtc.hours == 11u &&
              remote_rtc.minutes == 20u && remote_rtc.seconds == 35u);
        CHECK(integral_gb_runtime_link_engine_snapshot(&engine, &snapshot) == 0);
        CHECK(snapshot.battery_a_size == rtc_save_size &&
              snapshot.battery_b_size == rtc_save_size);
        CHECK(get_u64_le(snapshot.battery_a + snapshot.battery_a_size - 8u) ==
              target_timestamp);
        CHECK(get_u64_le(snapshot.battery_b + snapshot.battery_b_size - 8u) ==
              target_timestamp);
        integral_gb_runtime_link_snapshot_free(&snapshot);
        integral_gb_runtime_link_engine_free(&engine);

        config.rtc_offset_seconds = 1u;
        CHECK(integral_gb_runtime_link_engine_init(&engine, &config) != 0);
    }

    if (argc == 6) {
        struct stat local_save;
        CHECK(stat(argv[5], &local_save) == 0 && local_save.st_size >= 48);
        CHECK(integral_gb_runtime_link_save_preflight(
                  argv[4], (size_t)local_save.st_size) ==
              INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_OK);
        CHECK(integral_gb_runtime_link_save_preflight(
                  argv[4], (size_t)local_save.st_size - 48u) ==
              INTEGRAL_GB_RUNTIME_LINK_SAVE_PREFLIGHT_RTC_MISSING);
    }

    printf("PASS GB link SAV preflight checks=%u\n", checks);
    return 0;
}

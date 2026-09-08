/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_link_engine.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;

#define CHECK(condition) do { \
    checks++; \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static bool all_zero(const uint8_t *data, size_t size)
{
    size_t index;
    for (index = 0u; index < size; index++) {
        if (data[index] != 0u) return false;
    }
    return true;
}

static uint8_t *direct(GB_gameboy_t *gb,
                       GB_direct_access_t access,
                       size_t *size)
{
    return (uint8_t *)GB_get_direct_access(gb, access, size, NULL);
}

int main(int argc, char **argv)
{
    IntegralGBRuntimeLinkEngine engine;
    IntegralGBRuntimeLinkEngineConfig config;
    uint8_t *a_rom, *a_ram, *a_cart, *a_vram, *a_hram;
    uint8_t *b_rom, *b_ram, *b_cart, *b_vram, *b_hram;
    size_t a_rom_size, a_ram_size, a_cart_size, a_vram_size, a_hram_size;
    size_t b_rom_size, b_ram_size, b_cart_size, b_vram_size, b_hram_size;
    if (argc != 3) {
        fprintf(stderr, "usage: %s ROM_A ROM_B\n", argv[0]);
        return 2;
    }
    config = (IntegralGBRuntimeLinkEngineConfig){
        .rom_a = argv[1], .rom_b = argv[2], .display_role = 0,
    };
    CHECK(integral_gb_runtime_link_engine_init(&engine, &config) == 0);
    CHECK(integral_gb_runtime_link_engine_run_frame(&engine, 0u, 0u) == 0);
    a_rom = direct(engine.a.gb, GB_DIRECT_ACCESS_ROM, &a_rom_size);
    a_ram = direct(engine.a.gb, GB_DIRECT_ACCESS_RAM, &a_ram_size);
    a_cart = direct(engine.a.gb, GB_DIRECT_ACCESS_CART_RAM, &a_cart_size);
    a_vram = direct(engine.a.gb, GB_DIRECT_ACCESS_VRAM, &a_vram_size);
    a_hram = direct(engine.a.gb, GB_DIRECT_ACCESS_HRAM, &a_hram_size);
    b_rom = direct(engine.b.gb, GB_DIRECT_ACCESS_ROM, &b_rom_size);
    b_ram = direct(engine.b.gb, GB_DIRECT_ACCESS_RAM, &b_ram_size);
    b_cart = direct(engine.b.gb, GB_DIRECT_ACCESS_CART_RAM, &b_cart_size);
    b_vram = direct(engine.b.gb, GB_DIRECT_ACCESS_VRAM, &b_vram_size);
    b_hram = direct(engine.b.gb, GB_DIRECT_ACCESS_HRAM, &b_hram_size);
    CHECK(a_rom && a_ram && a_cart && a_vram && a_hram);
    CHECK(b_rom && b_ram && b_cart && b_vram && b_hram);
    CHECK(integral_gb_runtime_link_engine_zeroize_sensitive(&engine));
    CHECK(engine.sensitive_zeroized && !engine.initialized);
    CHECK(all_zero(a_rom, a_rom_size));
    CHECK(all_zero(a_ram, a_ram_size));
    CHECK(all_zero(a_cart, a_cart_size));
    CHECK(all_zero(a_vram, a_vram_size));
    CHECK(all_zero(a_hram, a_hram_size));
    CHECK(all_zero(b_rom, b_rom_size));
    CHECK(all_zero(b_ram, b_ram_size));
    CHECK(all_zero(b_cart, b_cart_size));
    CHECK(all_zero(b_vram, b_vram_size));
    CHECK(all_zero(b_hram, b_hram_size));
    CHECK(integral_gb_runtime_link_engine_run_frame(&engine, 0u, 0u) != 0);
    CHECK(integral_gb_runtime_link_engine_zeroize_sensitive(&engine));
    integral_gb_runtime_link_engine_free(&engine);
    CHECK(!engine.a.gb && !engine.b.gb && !engine.initialized);
    printf("PASS GB link engine zeroization checks=%u\n", checks);
    return 0;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slot.h"
#include "display.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct GuardedSlot {
    uint64_t before[8];
    IntegralGBRuntimeSlot slot;
    uint64_t after[8];
} GuardedSlot;

static void initialize_guards(GuardedSlot *guarded)
{
    memset(guarded, 0, sizeof(*guarded));
    for (unsigned i = 0; i < 8; i++) {
        guarded->before[i] = 0x1122334455667788ULL + i;
        guarded->after[i] = 0x8877665544332211ULL + i;
    }
}

static void assert_guards(const GuardedSlot *guarded)
{
    for (unsigned i = 0; i < 8; i++) {
        assert(guarded->before[i] == 0x1122334455667788ULL + i);
        assert(guarded->after[i] == 0x8877665544332211ULL + i);
    }
}

static void observe_palette_states(IntegralGBRuntimeSlot *slot, bool *seen_a, bool *seen_b)
{
    *seen_a = false;
    *seen_b = false;
    for (unsigned frame = 0; frame < 1600; frame++) {
        assert(integral_gb_runtime_slot_run_frames(slot, 1) == 0);
        unsigned width = integral_gb_runtime_slot_screen_width(slot);
        unsigned x_offset = (width - INTEGRAL_GB_RUNTIME_GB_WIDTH) / 2u;
        unsigned y_offset =
            (integral_gb_runtime_slot_screen_height(slot) - INTEGRAL_GB_RUNTIME_GB_HEIGHT) / 2u;
        const uint32_t *pixels = integral_gb_runtime_slot_presented_pixels(slot);
        uint32_t left = pixels[(72u + y_offset) * width + 40u + x_offset];
        uint32_t right = pixels[(72u + y_offset) * width + 120u + x_offset];
        if (left == 0xFFFF0000u && right == 0xFF0000FFu) {
            *seen_a = true;
        }
        if (left == 0xFF00FF00u && right == 0xFFFF0000u) {
            *seen_b = true;
        }
        if (*seen_a && *seen_b) return;
    }
}

static bool pixels_are_zero(const IntegralGBRuntimeSlot *slot, const uint32_t *pixels)
{
    unsigned count = integral_gb_runtime_slot_screen_width(slot) *
                     integral_gb_runtime_slot_screen_height(slot);
    for (unsigned i = 0; i < count; i++) {
        if (pixels[i] != 0u) return false;
    }
    return true;
}

static void assert_sameboot_presentation_boundary(IntegralGBRuntimeSlot *slot)
{
    assert(integral_gb_runtime_slot_presentation_suppressed(slot));
    slot->audio_buffer[0] = 123;
    slot->audio_buffer[1] = -123;
    slot->audio_frames = 1u;
    int16_t audio[2] = {0};
    assert(integral_gb_runtime_slot_drain_audio(slot, audio, 1u) == 0u);
    assert(slot->audio_frames == 0u);

    bool saw_hidden_core_frame = false;
    for (unsigned frame = 0; frame < 400u; frame++) {
        assert(integral_gb_runtime_slot_run_frames(slot, 1u) == 0);
        if (!integral_gb_runtime_slot_presentation_suppressed(slot)) break;
        const uint32_t *raw_pixels = slot->framebuffer ? slot->framebuffer : slot->pixels;
        if (!pixels_are_zero(slot, raw_pixels)) saw_hidden_core_frame = true;
        assert(pixels_are_zero(slot, integral_gb_runtime_slot_presented_pixels(slot)));
        assert(integral_gb_runtime_slot_drain_audio(slot, audio, 1u) == 0u);
    }
    assert(saw_hidden_core_frame);
    assert(!integral_gb_runtime_slot_presentation_suppressed(slot));
    assert(integral_gb_runtime_slot_presented_pixels(slot) == slot->pixels);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s SGB_FIXTURE_ROM\n", argv[0]);
        return 2;
    }

    IntegralGBRuntimeRomProfile profile;
    IntegralGBRuntimeRomModelReason reason;
    GB_model_t model;
    assert(integral_gb_runtime_slot_model_for_rom(argv[1], &model, &profile, &reason) == 0);
    assert(model == GB_MODEL_SGB2);
    assert(reason == INTEGRAL_GB_RUNTIME_ROM_MODEL_SGB_HEADER);
    assert(integral_gb_runtime_slot_apply_sgb_policy(model, true) == GB_MODEL_SGB2);
    assert(integral_gb_runtime_slot_apply_sgb_policy(model, false) == GB_MODEL_DMG_B);
    assert(integral_gb_runtime_slot_apply_sgb_policy(GB_MODEL_CGB_E, false) == GB_MODEL_CGB_E);
    assert(integral_gb_runtime_slot_apply_sgb_policy(GB_MODEL_DMG_B, false) == GB_MODEL_DMG_B);

    GuardedSlot legacy;
    initialize_guards(&legacy);
    IntegralGBRuntimeSlotConfig legacy_config = {
        .name = "sgb-legacy-test",
        .rom_path = argv[1],
        .save_path = "/tmp/integral_emulator_sgb_fixture_no_battery.sav",
        .model = model,
        .skip_boot_rom = true,
    };
    assert(integral_gb_runtime_slot_init(&legacy.slot, &legacy_config) == 0);
    assert(GB_get_screen_width(legacy.slot.gb) == INTEGRAL_GB_RUNTIME_GB_WIDTH);
    assert(GB_get_screen_height(legacy.slot.gb) == INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    assert_guards(&legacy);
    integral_gb_runtime_slot_free(&legacy.slot);
    assert_guards(&legacy);

    GuardedSlot guarded;
    initialize_guards(&guarded);
    IntegralGBRuntimeSlotConfig config = {
        .name = "sgb-test",
        .rom_path = argv[1],
        .save_path = "/tmp/integral_emulator_sgb_fixture_no_battery.sav",
        .model = model,
        .skip_boot_rom = true,
        .display_sgb_border = true,
    };
    assert(integral_gb_runtime_slot_init(&guarded.slot, &config) == 0);
    assert(guarded.slot.bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT);
    assert(GB_get_model(guarded.slot.gb) == GB_MODEL_SGB2);
    assert(GB_get_screen_width(guarded.slot.gb) == INTEGRAL_GB_RUNTIME_SGB_WIDTH);
    assert(GB_get_screen_height(guarded.slot.gb) == INTEGRAL_GB_RUNTIME_SGB_HEIGHT);
    assert(integral_gb_runtime_slot_screen_width(&guarded.slot) == INTEGRAL_GB_RUNTIME_SGB_WIDTH);
    assert(integral_gb_runtime_slot_screen_height(&guarded.slot) == INTEGRAL_GB_RUNTIME_SGB_HEIGHT);
    assert(guarded.slot.framebuffer != guarded.slot.pixels);

    assert_sameboot_presentation_boundary(&guarded.slot);

    bool seen_a = false;
    bool seen_b = false;
    observe_palette_states(&guarded.slot, &seen_a, &seen_b);
    assert(seen_a && seen_b);
    assert_guards(&guarded);

    integral_gb_runtime_slot_reset(&guarded.slot);
    assert_sameboot_presentation_boundary(&guarded.slot);
    observe_palette_states(&guarded.slot, &seen_a, &seen_b);
    assert(seen_a && seen_b);
    assert_guards(&guarded);

    integral_gb_runtime_slot_free(&guarded.slot);
    assert_guards(&guarded);
    puts("SGB slot integration tests passed");
    return 0;
}

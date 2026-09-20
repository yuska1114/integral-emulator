/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "slot.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "file_util.h"
#include "utf8_file.h"
#include "net_compat.h"
#include "display.h"
#include "memory.h"

#define INTEGRAL_GB_RUNTIME_PATH_MAX INTEGRAL_GB_RUNTIME_FILE_PATH_MAX
/* The pinned SameBoy SGB2 boot presentation runs from frame -10 through 199. */
#define INTEGRAL_GB_RUNTIME_SAMEBOOT_PRESENTATION_FRAMES 210u

static void slot_log_callback(GB_gameboy_t *gb, const char *message, GB_log_attributes_t attributes)
{
    (void)attributes;
    IntegralGBRuntimeSlot *slot = GB_get_user_data(gb);
    if (strstr(message, "Serial read request while using internal clock") ||
        strstr(message, "Serial write request while using internal clock")) {
        return;
    }
    fprintf(stderr, "[%s] %s", slot ? slot->name : "slot", message);
}

static uint32_t slot_rgb_encode_callback(GB_gameboy_t *gb, uint8_t r, uint8_t g, uint8_t b)
{
    (void)gb;
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

static bool slot_presentation_suppressed(const IntegralGBRuntimeSlot *slot)
{
    return slot &&
           slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT &&
           slot->sameboot_presentation_frames_remaining > 0u;
}

static void slot_vblank_callback(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    (void)type;
    IntegralGBRuntimeSlot *slot = GB_get_user_data(gb);
    if (slot) {
        integral_gb_runtime_slot_note_vblank(slot);
    }
}

static void slot_audio_callback(GB_gameboy_t *gb, GB_sample_t *sample)
{
    IntegralGBRuntimeSlot *slot = GB_get_user_data(gb);
    if (!slot || slot_presentation_suppressed(slot)) {
        return;
    }
    if (slot->audio_frames >= INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES) {
        slot->audio_frames_dropped++;
        return;
    }

    unsigned index = slot->audio_frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS;
    slot->audio_buffer[index] = sample->left;
    slot->audio_buffer[index + 1] = sample->right;
    slot->audio_frames++;
}

int integral_gb_runtime_slot_model_for_rom(const char *rom_path,
                                   GB_model_t *model,
                                   IntegralGBRuntimeRomProfile *profile,
                                   IntegralGBRuntimeRomModelReason *reason)
{
    char error[160];
    if (integral_gb_runtime_rom_profile_read(rom_path, profile, error, sizeof(error)) != 0) {
        fprintf(stderr, "ROM header analysis failed: %s\n", error);
        return -1;
    }
    if (integral_gb_runtime_rom_profile_select_model(profile, model, reason, error, sizeof(error)) != 0) {
        fprintf(stderr, "ROM model selection failed: %s\n", error);
        return -1;
    }
    return 0;
}

const char *integral_gb_runtime_slot_model_name(GB_model_t model)
{
    switch (model) {
        case GB_MODEL_DMG_B:
            return "DMG";
        case GB_MODEL_CGB_E:
            return "CGB";
        case GB_MODEL_SGB2:
            return "SGB2";
        default:
            return "custom";
    }
}

static void slot_boot_rom_load_callback(GB_gameboy_t *gb, GB_boot_rom_t type)
{
    IntegralGBRuntimeSlot *slot = GB_get_user_data(gb);
    if (!slot || slot->bootstrap_policy != INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT ||
        type != GB_BOOT_ROM_SGB2) {
        return;
    }
    GB_load_boot_rom_from_buffer(gb, slot->sgb2_boot_rom, sizeof(slot->sgb2_boot_rom));
}

static void slot_skip_boot_rom(GB_gameboy_t *gb)
{
    GB_registers_t *registers = GB_get_registers(gb);
    registers->pc = 0x0100;
    registers->sp = 0xFFFE;

    if (GB_is_cgb(gb)) {
        registers->af = 0x1180;
        registers->bc = 0x0000;
        registers->de = 0xFF56;
        registers->hl = 0x000D;
    }
    else {
        registers->af = 0x01B0;
        registers->bc = 0x0013;
        registers->de = 0x00D8;
        registers->hl = 0x014D;
    }

    GB_write_memory(gb, 0xFF00 + GB_IO_NR52, 0x80);
    GB_write_memory(gb, 0xFF00 + GB_IO_NR51, 0xF3);
    GB_write_memory(gb, 0xFF00 + GB_IO_NR50, 0x77);
    GB_write_memory(gb, 0xFF00 + GB_IO_BGP, 0xFC);
    GB_write_memory(gb, 0xFF00 + GB_IO_OBP0, 0xFF);
    GB_write_memory(gb, 0xFF00 + GB_IO_OBP1, 0xFF);
    GB_write_memory(gb, 0xFF00 + GB_IO_LCDC, 0x91);
    GB_write_memory(gb, 0xFF00 + GB_IO_BANK, 0x01);
}

int integral_gb_runtime_slot_init(IntegralGBRuntimeSlot *slot, const IntegralGBRuntimeSlotConfig *config)
{
    memset(slot, 0, sizeof(*slot));
    slot->name = config->name;
    slot->rom_path = config->rom_path;
    slot->save_path = config->save_path;
    slot->skip_boot_rom = config->skip_boot_rom;
    slot->battery_mode = config->battery_mode;
    slot->bootstrap_policy = config->model == GB_MODEL_SGB2
        ? INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT
        : (config->skip_boot_rom ? INTEGRAL_GB_RUNTIME_BOOTSTRAP_MANUAL_POST_BOOT
                                 : INTEGRAL_GB_RUNTIME_BOOTSTRAP_CORE_DEFAULT);
    if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT) {
        /* SameBoy starts the SGB presentation counter at -10 and completes
           the upstream intro at GB_SGB_INTRO_ANIMATION_LENGTH. Tracking that
           interval here changes only Integral's output gate. */
        slot->sameboot_presentation_frames_remaining =
            INTEGRAL_GB_RUNTIME_SAMEBOOT_PRESENTATION_FRAMES;
    }

    if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT) {
        char error[192];
        if (integral_gb_runtime_sgb2_boot_resource_load(slot->sgb2_boot_rom,
                                                slot->sgb2_boot_rom_path,
                                                sizeof(slot->sgb2_boot_rom_path),
                                                error,
                                                sizeof(error)) != 0) {
            fprintf(stderr, "%s: %s\n", slot->name, error);
            return -1;
        }
    }

    slot->gb = GB_alloc();
    if (!slot->gb) {
        fprintf(stderr, "%s: failed to allocate SameBoy instance\n", slot->name);
        return -1;
    }

    GB_init(slot->gb, config->model);
    integral_gb_runtime_serial_peripheral_router_init(&slot->serial_router, slot->gb);
    GB_set_user_data(slot->gb, slot);
    if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT) {
        GB_set_boot_rom_load_callback(slot->gb, slot_boot_rom_load_callback);
        GB_load_boot_rom_from_buffer(slot->gb, slot->sgb2_boot_rom, sizeof(slot->sgb2_boot_rom));
        GB_set_border_mode(slot->gb, GB_BORDER_NEVER);
    }
    GB_set_log_callback(slot->gb, slot_log_callback);
    GB_set_input_callback(slot->gb, NULL);
    GB_set_async_input_callback(slot->gb, NULL);
    GB_set_rgb_encode_callback(slot->gb, slot_rgb_encode_callback);
    GB_set_vblank_callback(slot->gb, slot_vblank_callback);
    GB_set_sample_rate(slot->gb, INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE);
    GB_apu_set_sample_callback(slot->gb, slot_audio_callback);
    unsigned screen_width = GB_get_screen_width(slot->gb);
    unsigned screen_height = GB_get_screen_height(slot->gb);
    if (screen_width != INTEGRAL_GB_RUNTIME_GB_WIDTH || screen_height != INTEGRAL_GB_RUNTIME_GB_HEIGHT) {
        fprintf(stderr,
                "%s: framebuffer geometry mismatch: %ux%u (expected %ux%u)\n",
                slot->name,
                screen_width,
                screen_height,
                INTEGRAL_GB_RUNTIME_GB_WIDTH,
                INTEGRAL_GB_RUNTIME_GB_HEIGHT);
        integral_gb_runtime_slot_free(slot);
        return -1;
    }
    GB_set_pixels_output(slot->gb, slot->pixels);

    int load_result;
#ifdef _WIN32
    /* SameBoy's filename API uses the Windows code page. Keep its core intact. */
    FILE *rom_file = integral_fopen(slot->rom_path, "rb");
    load_result = rom_file ? 0 : errno;
    if (rom_file) {
        long size;
        if (fseek(rom_file, 0, SEEK_END) || (size = ftell(rom_file)) <= 0 ||
            size > 0x2000000 || fseek(rom_file, 0, SEEK_SET)) {
            load_result = EINVAL;
        } else {
            uint8_t *bytes = malloc((size_t)size);
            if (!bytes) load_result = ENOMEM;
            else {
                if (fread(bytes, 1, (size_t)size, rom_file) != (size_t)size) load_result = EIO;
                else GB_load_rom_from_buffer(slot->gb, bytes, (size_t)size);
                free(bytes);
            }
        }
        fclose(rom_file);
    }
#else
    load_result = GB_load_rom(slot->gb, slot->rom_path);
#endif
    if (load_result != 0) {
        fprintf(stderr, "%s: failed to load ROM '%s': %s\n", slot->name, slot->rom_path, strerror(load_result));
        integral_gb_runtime_slot_free(slot);
        return -1;
    }

    if (slot->battery_mode == INTEGRAL_GB_RUNTIME_BATTERY_MEMORY_ONLY) {
        if (config->battery_buffer_size > 0 && !config->battery_buffer) {
            fprintf(stderr, "%s: memory-only battery length has no buffer\n", slot->name);
            integral_gb_runtime_slot_free_without_save(slot);
            return -1;
        }
        if (config->battery_buffer_size > 0) {
            GB_load_battery_from_buffer(slot->gb,
                                        config->battery_buffer,
                                        config->battery_buffer_size);
        }
    }
    else {
        if (!slot->save_path) {
            fprintf(stderr, "%s: file-backed battery requires a save path\n", slot->name);
            integral_gb_runtime_slot_free_without_save(slot);
            return -1;
        }
        load_result = GB_load_battery(slot->gb, slot->save_path);
        if (load_result != 0 && load_result != ENOENT) {
            fprintf(stderr, "%s: failed to load save '%s': %s\n", slot->name, slot->save_path, strerror(load_result));
            integral_gb_runtime_slot_free(slot);
            return -1;
        }

        if (load_result == ENOENT) {
            fprintf(stderr, "%s: save '%s' does not exist yet; starting with empty cartridge RAM\n", slot->name, slot->save_path);
        }
    }

    if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_MANUAL_POST_BOOT) {
        slot_skip_boot_rom(slot->gb);
    }

    if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT) {
        fprintf(stderr,
                "%s: SGB2 SameBoot loaded size=%u sha256=%s frame_rate=%.6f\n",
                slot->name,
                INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE,
                INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SHA256,
                GB_get_usual_frame_rate(slot->gb));
    }

    GB_clear_battery_dirty(slot->gb);
    slot->initialized = true;
    return 0;
}

unsigned integral_gb_runtime_slot_run_until_sync(IntegralGBRuntimeSlot *slot)
{
    if (!slot->initialized) {
        return 0;
    }
    return GB_run(slot->gb);
}

void integral_gb_runtime_slot_note_vblank(IntegralGBRuntimeSlot *slot)
{
    if (!slot) return;
    slot->vblank_occurred = true;
    slot->vblank_count++;
    if (slot->sameboot_presentation_frames_remaining > 0u) {
        slot->sameboot_presentation_frames_remaining--;
    }
}

bool integral_gb_runtime_slot_serial_active(const IntegralGBRuntimeSlot *slot)
{
    return slot && slot->initialized && (GB_read_memory(slot->gb, 0xFF00 + GB_IO_SC) & 0x80) != 0;
}

bool integral_gb_runtime_slot_serial_internal_clock(const IntegralGBRuntimeSlot *slot)
{
    return integral_gb_runtime_slot_serial_active(slot) && (GB_read_memory(slot->gb, 0xFF00 + GB_IO_SC) & 1) != 0;
}

int integral_gb_runtime_slot_run_frames(IntegralGBRuntimeSlot *slot, unsigned frame_count)
{
    if (!slot->initialized) {
        return -1;
    }

    for (unsigned i = 0; i < frame_count; i++) {
        (void)GB_run_frame(slot->gb);
    }
    return 0;
}

void integral_gb_runtime_slot_reset(IntegralGBRuntimeSlot *slot)
{
    if (!slot || !slot->initialized) {
        return;
    }
    GB_quick_reset(slot->gb);
    if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_MANUAL_POST_BOOT) {
        slot_skip_boot_rom(slot->gb);
    }
    else if (slot->bootstrap_policy == INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT) {
        slot->sameboot_presentation_frames_remaining =
            INTEGRAL_GB_RUNTIME_SAMEBOOT_PRESENTATION_FRAMES;
    }
    slot->vblank_occurred = false;
    slot->audio_frames = 0;
    slot->audio_frames_dropped = 0;
}

int integral_gb_runtime_slot_apply_rtc_offset_seconds(IntegralGBRuntimeSlot *slot, int64_t offset_seconds)
{
    if (!slot || !slot->initialized || offset_seconds == 0) {
        return 0;
    }
    int result = GB_apply_rtc_offset(slot->gb, offset_seconds);
    slot->rtc_offset_applied = result > 0;
    return result;
}

int integral_gb_runtime_slot_apply_rtc_offset_minutes(IntegralGBRuntimeSlot *slot, int offset_minutes)
{
    return integral_gb_runtime_slot_apply_rtc_offset_seconds(slot, (int64_t)offset_minutes * 60);
}

uint32_t integral_gb_runtime_slot_pixel_checksum(const IntegralGBRuntimeSlot *slot)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < sizeof(slot->pixels) / sizeof(slot->pixels[0]); i++) {
        hash ^= slot->pixels[i];
        hash *= 16777619u;
    }
    return hash;
}

bool integral_gb_runtime_slot_presentation_suppressed(const IntegralGBRuntimeSlot *slot)
{
    return slot_presentation_suppressed(slot);
}

const uint32_t *integral_gb_runtime_slot_presented_pixels(const IntegralGBRuntimeSlot *slot)
{
    static const uint32_t black_pixels[
        INTEGRAL_GB_RUNTIME_GB_WIDTH * INTEGRAL_GB_RUNTIME_GB_HEIGHT] = {0};
    if (!slot || !slot->initialized || slot_presentation_suppressed(slot)) {
        return black_pixels;
    }
    return slot->pixels;
}

unsigned integral_gb_runtime_slot_drain_audio(IntegralGBRuntimeSlot *slot, int16_t *dest, unsigned max_frames)
{
    if (!slot || slot_presentation_suppressed(slot)) {
        if (slot) {
            memset(slot->audio_buffer, 0, sizeof(slot->audio_buffer));
            slot->audio_frames = 0;
            slot->audio_frames_dropped = 0;
        }
        return 0;
    }
    unsigned frames = slot->audio_frames;
    if (frames > max_frames) {
        frames = max_frames;
    }
    if (frames > 0) {
        memcpy(dest, slot->audio_buffer, frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS * sizeof(int16_t));
        unsigned remaining = slot->audio_frames - frames;
        if (remaining > 0) {
            memmove(slot->audio_buffer,
                    slot->audio_buffer + frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS,
                    remaining * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS * sizeof(int16_t));
        }
        slot->audio_frames = remaining;
    }
    return frames;
}

bool integral_gb_runtime_slot_battery_dirty(const IntegralGBRuntimeSlot *slot)
{
    return slot && slot->initialized && GB_get_battery_dirty(slot->gb);
}

void integral_gb_runtime_slot_clear_battery_dirty(IntegralGBRuntimeSlot *slot)
{
    if (slot && slot->initialized) {
        GB_clear_battery_dirty(slot->gb);
    }
}

static bool build_temp_save_path(char *dest, size_t dest_size, const char *save_path)
{
    int written = snprintf(dest, dest_size, "%s.tmp.%ld", save_path, (long)integral_gb_runtime_getpid());
    return written > 0 && (size_t)written < dest_size;
}

int integral_gb_runtime_slot_save_battery(IntegralGBRuntimeSlot *slot)
{
    if (!slot->initialized) {
        return 0;
    }

    if (slot->battery_mode == INTEGRAL_GB_RUNTIME_BATTERY_MEMORY_ONLY) {
        return 0;
    }

    int save_size = GB_save_battery_size(slot->gb);
    if (save_size <= 0) {
        return 0;
    }

    if (integral_gb_runtime_ensure_parent_directories(slot->save_path, 0755) != 0) {
        fprintf(stderr, "%s: failed to create save directory for '%s': %s\n", slot->name, slot->save_path, strerror(errno));
        return -1;
    }
    char temp_path[INTEGRAL_GB_RUNTIME_PATH_MAX];
    if (!build_temp_save_path(temp_path, sizeof(temp_path), slot->save_path)) {
        fprintf(stderr, "%s: save path is too long: %s\n", slot->name, slot->save_path);
        return -1;
    }

    int result = GB_save_battery(slot->gb, temp_path);
    if (result != 0) {
        fprintf(stderr, "%s: failed to save battery '%s': %s\n", slot->name, temp_path, strerror(result));
        remove(temp_path);
        return -1;
    }
    if (integral_gb_runtime_replace_file(temp_path, slot->save_path) != 0) {
        fprintf(stderr, "%s: failed to publish save '%s': %s\n", slot->name, slot->save_path, strerror(errno));
        remove(temp_path);
        return -1;
    }
    slot->last_battery_save_us = integral_gb_runtime_now_us();
    return 0;
}

int integral_gb_runtime_slot_extract_battery(IntegralGBRuntimeSlot *slot,
                                     uint8_t **buffer,
                                     size_t *buffer_size)
{
    if (!slot || !slot->initialized || !buffer || !buffer_size) {
        return -1;
    }
    *buffer = NULL;
    *buffer_size = 0;
    int save_size = GB_save_battery_size(slot->gb);
    if (save_size <= 0) {
        return 0;
    }
    uint8_t *result = malloc((size_t)save_size);
    if (!result) {
        return -1;
    }
    int status = GB_save_battery_to_buffer(slot->gb, result, (size_t)save_size);
    if (status != 0) {
        memset(result, 0, (size_t)save_size);
        free(result);
        return -1;
    }
    *buffer = result;
    *buffer_size = (size_t)save_size;
    return 0;
}

static void free_slot(IntegralGBRuntimeSlot *slot, bool save_battery)
{
    if (!slot || !slot->gb) {
        return;
    }

    if (slot->initialized && save_battery &&
        slot->battery_mode == INTEGRAL_GB_RUNTIME_BATTERY_FILE) {
        (void)integral_gb_runtime_slot_save_battery(slot);
    }
    integral_gb_runtime_serial_peripheral_router_shutdown(&slot->serial_router);
    GB_free(slot->gb);
    GB_dealloc(slot->gb);
    slot->gb = NULL;
    slot->initialized = false;
}

void integral_gb_runtime_slot_free_without_save(IntegralGBRuntimeSlot *slot)
{
    free_slot(slot, false);
}

void integral_gb_runtime_slot_free(IntegralGBRuntimeSlot *slot)
{
    free_slot(slot, true);
}

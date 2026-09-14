/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_SLOT_H
#define INTEGRAL_GB_RUNTIME_SLOT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "gb.h"
#include "protocol.h"
#include "rom_profile.h"
#include "sgb_boot_resource.h"
#include "../serial/serial_peripheral.h"

typedef enum IntegralGBRuntimeBootstrapPolicy {
    INTEGRAL_GB_RUNTIME_BOOTSTRAP_CORE_DEFAULT,
    INTEGRAL_GB_RUNTIME_BOOTSTRAP_MANUAL_POST_BOOT,
    INTEGRAL_GB_RUNTIME_BOOTSTRAP_SGB2_SAMEBOOT,
} IntegralGBRuntimeBootstrapPolicy;

typedef enum IntegralGBRuntimeBatteryMode {
    INTEGRAL_GB_RUNTIME_BATTERY_FILE = 0,
    INTEGRAL_GB_RUNTIME_BATTERY_MEMORY_ONLY = 1,
} IntegralGBRuntimeBatteryMode;

typedef struct IntegralGBRuntimeSlotConfig {
    const char *name;
    const char *rom_path;
    const char *save_path;
    GB_model_t model;
    bool skip_boot_rom;
    IntegralGBRuntimeBatteryMode battery_mode;
    const uint8_t *battery_buffer;
    size_t battery_buffer_size;
} IntegralGBRuntimeSlotConfig;

typedef struct IntegralGBRuntimeSlot {
    const char *name;
    const char *rom_path;
    const char *save_path;
    GB_gameboy_t *gb;
    IntegralGBRuntimeSerialPeripheralRouter serial_router;
    void *local_link_context;
    bool initialized;
    bool skip_boot_rom;
    IntegralGBRuntimeBatteryMode battery_mode;
    IntegralGBRuntimeBootstrapPolicy bootstrap_policy;
    uint8_t sgb2_boot_rom[INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE];
    char sgb2_boot_rom_path[4096];
    bool vblank_occurred;
    unsigned vblank_count;
    unsigned sameboot_presentation_frames_remaining;
    bool rtc_offset_applied;
    int16_t audio_buffer[INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS];
    unsigned audio_frames;
    unsigned audio_frames_dropped;
    uint64_t last_battery_save_us;
    uint32_t pixels[INTEGRAL_GB_RUNTIME_GB_WIDTH * INTEGRAL_GB_RUNTIME_GB_HEIGHT];
} IntegralGBRuntimeSlot;

int integral_gb_runtime_slot_model_for_rom(const char *rom_path,
                                   GB_model_t *model,
                                   IntegralGBRuntimeRomProfile *profile,
                                   IntegralGBRuntimeRomModelReason *reason);
const char *integral_gb_runtime_slot_model_name(GB_model_t model);
int integral_gb_runtime_slot_init(IntegralGBRuntimeSlot *slot, const IntegralGBRuntimeSlotConfig *config);
unsigned integral_gb_runtime_slot_run_until_sync(IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_slot_note_vblank(IntegralGBRuntimeSlot *slot);
bool integral_gb_runtime_slot_serial_active(const IntegralGBRuntimeSlot *slot);
bool integral_gb_runtime_slot_serial_internal_clock(const IntegralGBRuntimeSlot *slot);
int integral_gb_runtime_slot_run_frames(IntegralGBRuntimeSlot *slot, unsigned frame_count);
void integral_gb_runtime_slot_reset(IntegralGBRuntimeSlot *slot);
int integral_gb_runtime_slot_apply_rtc_offset_seconds(IntegralGBRuntimeSlot *slot, int64_t offset_seconds);
int integral_gb_runtime_slot_apply_rtc_offset_minutes(IntegralGBRuntimeSlot *slot, int offset_minutes);
uint32_t integral_gb_runtime_slot_pixel_checksum(const IntegralGBRuntimeSlot *slot);
bool integral_gb_runtime_slot_presentation_suppressed(const IntegralGBRuntimeSlot *slot);
const uint32_t *integral_gb_runtime_slot_presented_pixels(const IntegralGBRuntimeSlot *slot);
unsigned integral_gb_runtime_slot_drain_audio(IntegralGBRuntimeSlot *slot, int16_t *dest, unsigned max_frames);
bool integral_gb_runtime_slot_battery_dirty(const IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_slot_clear_battery_dirty(IntegralGBRuntimeSlot *slot);
int integral_gb_runtime_slot_save_battery(IntegralGBRuntimeSlot *slot);
int integral_gb_runtime_slot_extract_battery(IntegralGBRuntimeSlot *slot,
                                     uint8_t **buffer,
                                     size_t *buffer_size);
void integral_gb_runtime_slot_free_without_save(IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_slot_free(IntegralGBRuntimeSlot *slot);

#endif

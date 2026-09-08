/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#define GB_INTERNAL
#include "gb_link_engine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "joypad.h"
#include "random.h"
#include "save_state.h"
#include "secure_memory.h"

enum { LINK_ROLE_A = 0, LINK_ROLE_B = 1 };
static const uint64_t LINK_RTC_EPOCH = 946684800u;

static void apply_input(GB_gameboy_t *gb, uint8_t mask);

static IntegralGBRuntimeLinkEngine *engine_for_gb(GB_gameboy_t *gb, IntegralGBRuntimeSlot **slot)
{
    *slot = GB_get_user_data(gb);
    return *slot ? (IntegralGBRuntimeLinkEngine *)(*slot)->local_link_context : NULL;
}

static void record_serial(IntegralGBRuntimeLinkEngine *engine,
                          uint8_t role,
                          bool outgoing,
                          bool incoming)
{
    uint8_t event[3] = {role, outgoing ? 1u : 0u, incoming ? 1u : 0u};
    integral_gb_runtime_content_sha256_update(&engine->serial_hash, event, sizeof(event));
    engine->serial_events++;
}

static void serial_a_start(GB_gameboy_t *gb, bool bit)
{
    IntegralGBRuntimeSlot *slot;
    IntegralGBRuntimeLinkEngine *engine = engine_for_gb(gb, &slot);
    (void)slot;
    if (engine) engine->a_bit = bit;
}

static bool serial_a_end(GB_gameboy_t *gb)
{
    IntegralGBRuntimeSlot *slot;
    IntegralGBRuntimeLinkEngine *engine = engine_for_gb(gb, &slot);
    (void)slot;
    if (!engine || !engine->initialized) return true;
    bool incoming = GB_serial_get_data_bit(engine->b.gb);
    GB_serial_set_data_bit(engine->b.gb, engine->a_bit);
    record_serial(engine, LINK_ROLE_A, engine->a_bit, incoming);
    return incoming;
}

static void serial_b_start(GB_gameboy_t *gb, bool bit)
{
    IntegralGBRuntimeSlot *slot;
    IntegralGBRuntimeLinkEngine *engine = engine_for_gb(gb, &slot);
    (void)slot;
    if (engine) engine->b_bit = bit;
}

static bool serial_b_end(GB_gameboy_t *gb)
{
    IntegralGBRuntimeSlot *slot;
    IntegralGBRuntimeLinkEngine *engine = engine_for_gb(gb, &slot);
    (void)slot;
    if (!engine || !engine->initialized) return true;
    bool incoming = GB_serial_get_data_bit(engine->a.gb);
    GB_serial_set_data_bit(engine->a.gb, engine->b_bit);
    record_serial(engine, LINK_ROLE_B, engine->b_bit, incoming);
    return incoming;
}

static void record_ir(IntegralGBRuntimeLinkEngine *engine, uint8_t role, bool output)
{
    uint8_t event[2] = {role, output ? 1u : 0u};
    integral_gb_runtime_content_sha256_update(&engine->ir_hash, event, sizeof(event));
    engine->ir_events++;
}

static void link_engine_vblank(GB_gameboy_t *gb, GB_vblank_type_t type)
{
    (void)type;
    IntegralGBRuntimeSlot *slot;
    (void)engine_for_gb(gb, &slot);
    if (!slot) return;
    slot->vblank_occurred = true;
    slot->vblank_count++;
}

static void ir_a(GB_gameboy_t *gb, bool output)
{
    IntegralGBRuntimeSlot *slot;
    IntegralGBRuntimeLinkEngine *engine = engine_for_gb(gb, &slot);
    (void)slot;
    if (!engine || !engine->initialized) return;
    record_ir(engine, LINK_ROLE_A, output);
    GB_set_infrared_input(engine->b.gb, output);
}

static void ir_b(GB_gameboy_t *gb, bool output)
{
    IntegralGBRuntimeSlot *slot;
    IntegralGBRuntimeLinkEngine *engine = engine_for_gb(gb, &slot);
    (void)slot;
    if (!engine || !engine->initialized) return;
    record_ir(engine, LINK_ROLE_B, output);
    GB_set_infrared_input(engine->a.gb, output);
}

static void apply_input(GB_gameboy_t *gb, uint8_t mask)
{
    GB_set_key_state(gb, GB_KEY_RIGHT, (mask & 0x01u) != 0);
    GB_set_key_state(gb, GB_KEY_LEFT, (mask & 0x02u) != 0);
    GB_set_key_state(gb, GB_KEY_UP, (mask & 0x04u) != 0);
    GB_set_key_state(gb, GB_KEY_DOWN, (mask & 0x08u) != 0);
    GB_set_key_state(gb, GB_KEY_A, (mask & 0x10u) != 0);
    GB_set_key_state(gb, GB_KEY_B, (mask & 0x20u) != 0);
    GB_set_key_state(gb, GB_KEY_SELECT, (mask & 0x40u) != 0);
    GB_set_key_state(gb, GB_KEY_START, (mask & 0x80u) != 0);
}

static uint16_t decode_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | (uint16_t)((uint16_t)data[1] << 8);
}

static uint64_t decode_u64_le(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; i++) value |= (uint64_t)data[i] << (i * 8);
    return value;
}

static void decode_vba_rtc_time(GB_rtc_time_t *rtc, const uint8_t *data)
{
    rtc->seconds = data[0];
    rtc->minutes = data[4];
    rtc->hours = data[8];
    rtc->days = data[12];
    rtc->high = data[16];
}

static int load_battery_deterministically(IntegralGBRuntimeSlot *slot,
                                          const uint8_t *save,
                                          size_t save_size)
{
    GB_gameboy_t *gb = slot->gb;
    if (!save || save_size == 0) {
        gb->last_rtc_second = LINK_RTC_EPOCH;
        return 0;
    }
    GB_load_battery_from_buffer(gb, save, save_size);
    /* SameBoy serializes last_rtc_second even for cartridges without RTC.
       Keep that otherwise-unused field deterministic across process starts. */
    gb->last_rtc_second = LINK_RTC_EPOCH;
    if (!gb->cartridge_type->has_rtc && gb->cartridge_type->mbc_type != GB_HUC3) {
        return 0;
    }

    size_t ram_size = gb->mbc_ram_size;
    size_t rtc_size = save_size > ram_size ? save_size - ram_size : 0;
    const uint8_t *rtc = save + (save_size < ram_size ? save_size : ram_size);
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        if (rtc_size != sizeof(GB_huc3_rtc_time_t)) return -1;
        gb->huc3.minutes = decode_u16_le(rtc + 8);
        gb->huc3.days = decode_u16_le(rtc + 10);
        gb->huc3.alarm_minutes = decode_u16_le(rtc + 12);
        gb->huc3.alarm_days = decode_u16_le(rtc + 14);
        gb->huc3.alarm_enabled = rtc[16] != 0;
    }
    else if (gb->cartridge_type->mbc_type == GB_TPP1) {
        if (rtc_size != 20) return -1;
        gb->tpp1_mr4 = rtc[6];
        for (unsigned i = 0; i < 4; i++) gb->rtc_real.data[i ^ 3] = rtc[16 + i];
    }
    else {
        /* Canonical Integral/SameBoy RTC SAVs use VBA64 layout: two
           20-byte register groups followed by an 8-byte host timestamp. */
        if (rtc_size != 48) return -1;
        decode_vba_rtc_time(&gb->rtc_real, rtc);
        decode_vba_rtc_time(&gb->rtc_latched, rtc + 20);
        (void)decode_u64_le(rtc + 40); /* validated layout; host time is ignored */
    }
    return 0;
}

static void advance_rtc_deterministically(GB_gameboy_t *gb, uint64_t seconds)
{
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        while (seconds--) {
            gb->last_rtc_second++;
            if (gb->last_rtc_second % 60 != 0) continue;
            if (++gb->huc3.minutes == 60 * 24) {
                gb->huc3.minutes = 0;
                gb->huc3.days++;
            }
        }
        return;
    }
    bool running = gb->cartridge_type->mbc_type == GB_TPP1
        ? (gb->tpp1_mr4 & 0x4) != 0
        : (gb->rtc_real.high & 0x40) == 0;
    if (!running) return;
    while (seconds--) {
        gb->last_rtc_second++;
        if (++gb->rtc_real.seconds != 60) continue;
        gb->rtc_real.seconds = 0;
        if (++gb->rtc_real.minutes != 60) continue;
        gb->rtc_real.minutes = 0;
        if (gb->cartridge_type->mbc_type == GB_TPP1) {
            if (++gb->rtc_real.tpp1.hours != 24) continue;
            gb->rtc_real.tpp1.hours = 0;
            if (++gb->rtc_real.tpp1.weekday != 7) continue;
            gb->rtc_real.tpp1.weekday = 0;
            if (++gb->rtc_real.tpp1.weeks == 0) gb->tpp1_mr4 |= 8;
        }
        else {
            if (++gb->rtc_real.hours != 24) continue;
            gb->rtc_real.hours = 0;
            if (++gb->rtc_real.days != 0) continue;
            if (gb->rtc_real.high & 1) gb->rtc_real.high |= 0x80;
            gb->rtc_real.high ^= 1;
        }
    }
}

static int init_slot(IntegralGBRuntimeSlot *slot,
                     const char *name,
                     const char *rom,
                     const uint8_t *save,
                     size_t save_size)
{
    GB_model_t model;
    IntegralGBRuntimeRomProfile profile;
    IntegralGBRuntimeRomModelReason reason;
    if (integral_gb_runtime_slot_model_for_rom(rom, &model, &profile, &reason) != 0) return -1;
    IntegralGBRuntimeSlotConfig config = {
        .name = name,
        .rom_path = rom,
        .save_path = NULL,
        .model = model,
        .skip_boot_rom = true,
        .battery_mode = INTEGRAL_GB_RUNTIME_BATTERY_MEMORY_ONLY,
        .battery_buffer = NULL,
        .battery_buffer_size = 0,
    };
    if (integral_gb_runtime_slot_init(slot, &config) != 0) return -1;
    GB_set_rtc_mode(slot->gb, GB_RTC_MODE_ACCURATE);
    if (load_battery_deterministically(slot, save, save_size) != 0) {
        fprintf(stderr, "%s: unsupported deterministic RTC battery layout\n", name);
        integral_gb_runtime_slot_free_without_save(slot);
        return -1;
    }
    GB_clear_battery_dirty(slot->gb);
    return 0;
}

int integral_gb_runtime_link_engine_init(IntegralGBRuntimeLinkEngine *engine,
                                 const IntegralGBRuntimeLinkEngineConfig *config)
{
    if (!engine || !config || !config->rom_a || !config->rom_b ||
        (config->display_role != LINK_ROLE_A &&
         config->display_role != LINK_ROLE_B)) return -1;
    memset(engine, 0, sizeof(*engine));
    /* SameBoy's process-global power-on noise seed is wall-clock initialized.
       The Phase 1 ABI disables it so separately initialized pairs receive the
       same defined zero-valued power-on noise. */
    GB_random_set_enabled(false);
    if (init_slot(&engine->a, "link-a", config->rom_a, config->save_a, config->save_a_size) != 0) {
        return -1;
    }
    if (init_slot(&engine->b, "link-b", config->rom_b, config->save_b, config->save_b_size) != 0) {
        integral_gb_runtime_slot_free_without_save(&engine->a);
        return -1;
    }
    if (config->rtc_offset_seconds > 0) {
        advance_rtc_deterministically(engine->a.gb, config->rtc_offset_seconds);
        advance_rtc_deterministically(engine->b.gb, config->rtc_offset_seconds);
    }
    engine->a.local_link_context = engine;
    engine->b.local_link_context = engine;
    engine->input_a = config->input_a;
    engine->input_b = config->input_b;
    engine->input_frames = config->input_frames;
    engine->display_role = (unsigned)config->display_role;
    engine->preserve_both_audio = config->preserve_both_audio;
    if (integral_gb_runtime_serial_peripheral_router_attach(&engine->a.serial_router,
                                                    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK,
                                                    serial_a_start,
                                                    serial_a_end) != 0 ||
        integral_gb_runtime_serial_peripheral_router_attach(&engine->b.serial_router,
                                                    INTEGRAL_GB_RUNTIME_SERIAL_PERIPHERAL_LINK,
                                                    serial_b_start,
                                                    serial_b_end) != 0) {
        integral_gb_runtime_link_engine_free(engine);
        return -1;
    }
    GB_set_infrared_callback(engine->a.gb, ir_a);
    GB_set_infrared_callback(engine->b.gb, ir_b);
    GB_set_vblank_callback(engine->a.gb, link_engine_vblank);
    GB_set_vblank_callback(engine->b.gb, link_engine_vblank);
    /* Rendering is part of SameBoy's emulated PPU state. A shadow core may be
       headless at the presentation layer, but disabling core rendering makes
       otherwise identical owner/shadow save states diverge immediately. Keep
       both canonical cores rendering and route only the selected role's pixels
       when a product runtime adds a window. */
    GB_set_rendering_disabled(engine->a.gb, false);
    GB_set_rendering_disabled(engine->b.gb, false);
    integral_gb_runtime_content_sha256_init(&engine->input_hash);
    integral_gb_runtime_content_sha256_init(&engine->serial_hash);
    integral_gb_runtime_content_sha256_init(&engine->ir_hash);
    engine->a_bit = true;
    engine->b_bit = true;
    engine->initialized = true;
    return 0;
}

int integral_gb_runtime_link_engine_run_frame(IntegralGBRuntimeLinkEngine *engine,
                                      uint8_t input_a,
                                      uint8_t input_b)
{
    if (!engine || !engine->initialized) return -1;
    uint8_t record[10];
    uint64_t frame = engine->logical_frame;
    for (unsigned i = 0; i < 8; i++) record[7 - i] = (uint8_t)(frame >> (i * 8));
    record[8] = input_a;
    record[9] = input_b;
    integral_gb_runtime_content_sha256_update(&engine->input_hash, record, sizeof(record));
    if (engine->input_a && engine->input_b) {
        if (frame >= engine->input_frames ||
            input_a != engine->input_a[frame] || input_b != engine->input_b[frame]) {
            fprintf(stderr, "Local link input timeline mismatch at frame %llu\n",
                    (unsigned long long)frame);
            return -1;
        }
    }
    apply_input(engine->a.gb, input_a);
    apply_input(engine->b.gb, input_b);
    engine->a.vblank_occurred = false;
    engine->b.vblank_occurred = false;
    unsigned calls = 0;
    signed cycle_delta = 0;
    while (!engine->a.vblank_occurred || !engine->b.vblank_occurred) {
        if (cycle_delta >= 0) {
            cycle_delta -= (signed)integral_gb_runtime_slot_run_until_sync(&engine->a);
        }
        else {
            cycle_delta += (signed)integral_gb_runtime_slot_run_until_sync(&engine->b);
        }
        if (++calls > INTEGRAL_GB_RUNTIME_LINK_MAX_CORE_CALLS_PER_FRAME) {
            fprintf(stderr, "Local link scheduler exceeded call bound at frame %llu\n",
                    (unsigned long long)engine->logical_frame);
            return -1;
        }
    }
    engine->logical_frame++;
    /* Audio is presentation-only. Retain only the local owner's stream; the
       shadow stream must never accumulate or become selectable accidentally. */
    if (!engine->preserve_both_audio) {
        IntegralGBRuntimeSlot *shadow = engine->display_role == LINK_ROLE_A
            ? &engine->b : &engine->a;
        integral_gb_runtime_secure_zero(shadow->audio_buffer,
                                     sizeof(shadow->audio_buffer));
        shadow->audio_frames = 0u;
        shadow->audio_frames_dropped = 0u;
    }
    return 0;
}

static void hash_copy(const IntegralGBRuntimeContentSha256 *source, uint8_t digest[32])
{
    IntegralGBRuntimeContentSha256 copy = *source;
    integral_gb_runtime_content_sha256_finish(&copy, digest);
}

static int capture_state(IntegralGBRuntimeSlot *slot, uint8_t **buffer, size_t *size)
{
    *size = GB_get_save_state_size(slot->gb);
    *buffer = malloc(*size);
    if (!*buffer) return -1;
    GB_save_state_to_buffer(slot->gb, *buffer);
    return 0;
}

static int canonicalize_battery_rtc_timestamp(const IntegralGBRuntimeSlot *slot,
                                               uint8_t *buffer,
                                               size_t size)
{
    const GB_gameboy_t *gb = slot->gb;
    if (!gb || !gb->cartridge_type || !buffer) return 0;
    size_t offset;
    if (gb->cartridge_type->mbc_type == GB_HUC3) {
        offset = gb->mbc_ram_size;
    }
    else if (gb->cartridge_type->mbc_type == GB_TPP1) {
        offset = gb->mbc_ram_size + 8;
    }
    else if (gb->cartridge_type->has_rtc) {
        if (size < 8) return -1;
        offset = size - 8;
    }
    else {
        return 0;
    }
    if (offset > size || size - offset < 8) return -1;
    uint64_t timestamp = gb->last_rtc_second;
    for (unsigned i = 0; i < 8; i++) {
        buffer[offset + i] = (uint8_t)(timestamp >> (i * 8));
    }
    return 0;
}

static int capture_battery(IntegralGBRuntimeSlot *slot, uint8_t **buffer, size_t *size)
{
    if (integral_gb_runtime_slot_extract_battery(slot, buffer, size) != 0) return -1;
    if (canonicalize_battery_rtc_timestamp(slot, *buffer, *size) != 0) {
        if (*buffer) {
            memset(*buffer, 0, *size);
            free(*buffer);
        }
        *buffer = NULL;
        *size = 0;
        return -1;
    }
    return 0;
}

int integral_gb_runtime_link_engine_snapshot(IntegralGBRuntimeLinkEngine *engine,
                                     IntegralGBRuntimeLinkSnapshot *snapshot)
{
    if (!engine || !engine->initialized || !snapshot) return -1;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->logical_frame = engine->logical_frame;
    if (capture_state(&engine->a, &snapshot->state_a, &snapshot->state_a_size) != 0 ||
        capture_state(&engine->b, &snapshot->state_b, &snapshot->state_b_size) != 0 ||
        capture_battery(&engine->a, &snapshot->battery_a,
                        &snapshot->battery_a_size) != 0 ||
        capture_battery(&engine->b, &snapshot->battery_b,
                        &snapshot->battery_b_size) != 0) {
        integral_gb_runtime_link_snapshot_free(snapshot);
        return -1;
    }
    integral_gb_runtime_content_sha256(snapshot->state_a, snapshot->state_a_size, snapshot->state_a_sha256);
    integral_gb_runtime_content_sha256(snapshot->state_b, snapshot->state_b_size, snapshot->state_b_sha256);
    integral_gb_runtime_content_sha256(snapshot->battery_a, snapshot->battery_a_size, snapshot->battery_a_sha256);
    integral_gb_runtime_content_sha256(snapshot->battery_b, snapshot->battery_b_size, snapshot->battery_b_sha256);
    hash_copy(&engine->input_hash, snapshot->input_sha256);
    hash_copy(&engine->serial_hash, snapshot->serial_sha256);
    hash_copy(&engine->ir_hash, snapshot->ir_sha256);
    snapshot->serial_events = engine->serial_events;
    snapshot->ir_events = engine->ir_events;
    IntegralGBRuntimeContentSha256 pair;
    uint8_t frame_be[8];
    for (unsigned i = 0; i < 8; i++) {
        frame_be[7 - i] = (uint8_t)(snapshot->logical_frame >> (i * 8));
    }
    integral_gb_runtime_content_sha256_init(&pair);
    integral_gb_runtime_content_sha256_update(&pair, INTEGRAL_GB_RUNTIME_LINK_ABI_ID,
                                   strlen(INTEGRAL_GB_RUNTIME_LINK_ABI_ID));
    integral_gb_runtime_content_sha256_update(&pair, frame_be, sizeof(frame_be));
    integral_gb_runtime_content_sha256_update(&pair, snapshot->state_a_sha256, 32);
    integral_gb_runtime_content_sha256_update(&pair, snapshot->state_b_sha256, 32);
    integral_gb_runtime_content_sha256_update(&pair, snapshot->input_sha256, 32);
    integral_gb_runtime_content_sha256_finish(&pair, snapshot->pair_sha256);
    return 0;
}

int integral_gb_runtime_link_engine_get_rtc(const IntegralGBRuntimeLinkEngine *engine,
                                    unsigned role,
                                    IntegralGBRuntimeLinkRTCRegisters *registers)
{
    if (!engine || !engine->initialized || !registers || role > LINK_ROLE_B) return -1;
    const GB_gameboy_t *gb = role == LINK_ROLE_A ? engine->a.gb : engine->b.gb;
    if (!gb || !gb->cartridge_type || !gb->cartridge_type->has_rtc) return -1;
    registers->seconds = gb->rtc_real.seconds;
    registers->minutes = gb->rtc_real.minutes;
    registers->hours = gb->rtc_real.hours;
    registers->days = (uint16_t)gb->rtc_real.days |
                      (uint16_t)((gb->rtc_real.high & 1u) << 8);
    registers->halted = (gb->rtc_real.high & 0x40u) != 0;
    registers->overflowed = (gb->rtc_real.high & 0x80u) != 0;
    return 0;
}

const uint32_t *integral_gb_runtime_link_engine_presented_pixels(
    const IntegralGBRuntimeLinkEngine *engine)
{
    if (!engine || !engine->initialized) return NULL;
    return engine->display_role == LINK_ROLE_A
        ? engine->a.pixels : engine->b.pixels;
}

unsigned integral_gb_runtime_link_engine_drain_presented_audio(
    IntegralGBRuntimeLinkEngine *engine,
    int16_t *destination,
    unsigned max_frames)
{
    IntegralGBRuntimeSlot *owner;
    if (!engine || !engine->initialized) return 0u;
    owner = engine->display_role == LINK_ROLE_A ? &engine->a : &engine->b;
    return integral_gb_runtime_slot_drain_audio(owner, destination, max_frames);
}

void integral_gb_runtime_link_snapshot_free(IntegralGBRuntimeLinkSnapshot *snapshot)
{
    if (!snapshot) return;
    if (snapshot->state_a) { memset(snapshot->state_a, 0, snapshot->state_a_size); free(snapshot->state_a); }
    if (snapshot->state_b) { memset(snapshot->state_b, 0, snapshot->state_b_size); free(snapshot->state_b); }
    if (snapshot->battery_a) { memset(snapshot->battery_a, 0, snapshot->battery_a_size); free(snapshot->battery_a); }
    if (snapshot->battery_b) { memset(snapshot->battery_b, 0, snapshot->battery_b_size); free(snapshot->battery_b); }
    memset(snapshot, 0, sizeof(*snapshot));
}

static bool zeroize_core(GB_gameboy_t *gb)
{
    bool complete = true;
    if (!gb) return true;
    if (gb->ram) {
        integral_gb_runtime_secure_zero(gb->ram, gb->ram_size);
    }
    if (gb->vram) {
        integral_gb_runtime_secure_zero(gb->vram, gb->vram_size);
    }
    if (gb->mbc_ram) {
        integral_gb_runtime_secure_zero(gb->mbc_ram, gb->mbc_ram_size);
    }
    if (gb->rom) {
        integral_gb_runtime_secure_zero(gb->rom, gb->rom_size);
    }
    if (gb->sgb) {
        integral_gb_runtime_secure_zero(gb->sgb, sizeof(*gb->sgb));
    }
#ifndef GB_DISABLE_REWIND
    /* The local link engine never enables rewind. Compressed rewind buffers do not retain
       their allocation sizes, so fail the strict completeness signal if a
       future caller enables it instead of pretending they were wiped. */
    if (gb->rewind_buffer_length != 0u || gb->rewind_sequences != NULL) {
        complete = false;
    }
#endif
    integral_gb_runtime_secure_zero(gb, GB_SECTION_OFFSET(unsaved));
    return complete;
}

bool integral_gb_runtime_link_engine_zeroize_sensitive(IntegralGBRuntimeLinkEngine *engine)
{
    bool complete_a;
    bool complete_b;
    if (!engine) return false;
    if (engine->sensitive_zeroized) return true;
    complete_a = zeroize_core(engine->a.gb);
    complete_b = zeroize_core(engine->b.gb);
    integral_gb_runtime_secure_zero(engine->a.pixels,
                                 sizeof(engine->a.pixels));
    integral_gb_runtime_secure_zero(engine->b.pixels,
                                 sizeof(engine->b.pixels));
    integral_gb_runtime_secure_zero(engine->a.audio_buffer,
                                 sizeof(engine->a.audio_buffer));
    integral_gb_runtime_secure_zero(engine->b.audio_buffer,
                                 sizeof(engine->b.audio_buffer));
    integral_gb_runtime_secure_zero(engine->a.sgb2_boot_rom,
                                 sizeof(engine->a.sgb2_boot_rom));
    integral_gb_runtime_secure_zero(engine->b.sgb2_boot_rom,
                                 sizeof(engine->b.sgb2_boot_rom));
    integral_gb_runtime_secure_zero(&engine->input_hash,
                                 sizeof(engine->input_hash));
    integral_gb_runtime_secure_zero(&engine->serial_hash,
                                 sizeof(engine->serial_hash));
    integral_gb_runtime_secure_zero(&engine->ir_hash,
                                 sizeof(engine->ir_hash));
    engine->input_a = NULL;
    engine->input_b = NULL;
    engine->input_frames = 0u;
    engine->initialized = false;
    engine->sensitive_zeroized = true;
    return complete_a && complete_b;
}

void integral_gb_runtime_link_engine_free(IntegralGBRuntimeLinkEngine *engine)
{
    if (!engine) return;
    (void)integral_gb_runtime_link_engine_zeroize_sensitive(engine);
    if (engine->a.gb) {
        GB_set_infrared_callback(engine->a.gb, NULL);
        integral_gb_runtime_slot_free_without_save(&engine->a);
    }
    if (engine->b.gb) {
        GB_set_infrared_callback(engine->b.gb, NULL);
        integral_gb_runtime_slot_free_without_save(&engine->b);
    }
    integral_gb_runtime_secure_zero(engine, sizeof(*engine));
}

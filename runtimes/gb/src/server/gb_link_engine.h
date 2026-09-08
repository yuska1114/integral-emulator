/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_LINK_ENGINE_H
#define INTEGRAL_GB_RUNTIME_LINK_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "slot.h"
#include "content_hash.h"

#define INTEGRAL_GB_RUNTIME_LINK_ABI_ID "ilp-det-v3-draft"
#define INTEGRAL_GB_RUNTIME_LINK_MAX_CORE_CALLS_PER_FRAME 200000u

typedef struct IntegralGBRuntimeLinkEngineConfig {
    const char *rom_a;
    const char *rom_b;
    const uint8_t *save_a;
    size_t save_a_size;
    const uint8_t *save_b;
    size_t save_b_size;
    const uint8_t *input_a;
    const uint8_t *input_b;
    size_t input_frames;
    uint64_t rtc_offset_seconds;
    int display_role;
    bool preserve_both_audio;
} IntegralGBRuntimeLinkEngineConfig;

typedef struct IntegralGBRuntimeLinkSnapshot {
    uint64_t logical_frame;
    uint8_t *state_a;
    size_t state_a_size;
    uint8_t *state_b;
    size_t state_b_size;
    uint8_t *battery_a;
    size_t battery_a_size;
    uint8_t *battery_b;
    size_t battery_b_size;
    uint8_t state_a_sha256[32];
    uint8_t state_b_sha256[32];
    uint8_t battery_a_sha256[32];
    uint8_t battery_b_sha256[32];
    uint8_t pair_sha256[32];
    uint8_t input_sha256[32];
    uint8_t serial_sha256[32];
    uint8_t ir_sha256[32];
    uint64_t serial_events;
    uint64_t ir_events;
} IntegralGBRuntimeLinkSnapshot;

typedef struct IntegralGBRuntimeLinkRTCRegisters {
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint16_t days;
    bool halted;
    bool overflowed;
} IntegralGBRuntimeLinkRTCRegisters;

typedef struct IntegralGBRuntimeLinkEngine {
    IntegralGBRuntimeSlot a;
    IntegralGBRuntimeSlot b;
    bool initialized;
    bool sensitive_zeroized;
    unsigned display_role;
    uint64_t logical_frame;
    const uint8_t *input_a;
    const uint8_t *input_b;
    size_t input_frames;
    bool a_bit;
    bool b_bit;
    uint64_t serial_events;
    uint64_t ir_events;
    IntegralGBRuntimeContentSha256 input_hash;
    IntegralGBRuntimeContentSha256 serial_hash;
    IntegralGBRuntimeContentSha256 ir_hash;
    bool preserve_both_audio;
} IntegralGBRuntimeLinkEngine;

int integral_gb_runtime_link_engine_init(IntegralGBRuntimeLinkEngine *engine,
                                 const IntegralGBRuntimeLinkEngineConfig *config);
int integral_gb_runtime_link_engine_run_frame(IntegralGBRuntimeLinkEngine *engine,
                                      uint8_t input_a,
                                      uint8_t input_b);
int integral_gb_runtime_link_engine_snapshot(IntegralGBRuntimeLinkEngine *engine,
                                     IntegralGBRuntimeLinkSnapshot *snapshot);
int integral_gb_runtime_link_engine_get_rtc(const IntegralGBRuntimeLinkEngine *engine,
                                    unsigned role,
                                    IntegralGBRuntimeLinkRTCRegisters *registers);
const uint32_t *integral_gb_runtime_link_engine_presented_pixels(
    const IntegralGBRuntimeLinkEngine *engine);
unsigned integral_gb_runtime_link_engine_drain_presented_audio(
    IntegralGBRuntimeLinkEngine *engine,
    int16_t *destination,
    unsigned max_frames);
void integral_gb_runtime_link_snapshot_free(IntegralGBRuntimeLinkSnapshot *snapshot);
bool integral_gb_runtime_link_engine_zeroize_sensitive(IntegralGBRuntimeLinkEngine *engine);
void integral_gb_runtime_link_engine_free(IntegralGBRuntimeLinkEngine *engine);

#endif

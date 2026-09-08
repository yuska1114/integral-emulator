/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_AUDIO_PLAYER_H
#define INTEGRAL_GB_RUNTIME_AUDIO_PLAYER_H

#include <stdint.h>

#include "slot.h"

typedef struct IntegralGBRuntimeAudioPlayer IntegralGBRuntimeAudioPlayer;

int integral_gb_runtime_audio_player_open(IntegralGBRuntimeAudioPlayer **player_out, unsigned max_queue_ms);
void integral_gb_runtime_audio_player_queue_slot(IntegralGBRuntimeAudioPlayer *player, IntegralGBRuntimeSlot *slot);
void integral_gb_runtime_audio_player_queue_samples(IntegralGBRuntimeAudioPlayer *player, const int16_t *samples, unsigned frames);
void integral_gb_runtime_audio_player_print_stats(const IntegralGBRuntimeAudioPlayer *player);
void integral_gb_runtime_audio_player_close(IntegralGBRuntimeAudioPlayer *player);

#endif

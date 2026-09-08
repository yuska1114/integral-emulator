/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "audio_player.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>

#include "protocol.h"

struct IntegralGBRuntimeAudioPlayer {
    SDL_AudioDeviceID device;
    unsigned max_queue_bytes;
    unsigned packets_queued;
    unsigned frames_queued;
    unsigned queue_resets;
};

int integral_gb_runtime_audio_player_open(IntegralGBRuntimeAudioPlayer **player_out, unsigned max_queue_ms)
{
    if (max_queue_ms == 0) {
        max_queue_ms = 120;
    }

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
        fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
        return -1;
    }

    IntegralGBRuntimeAudioPlayer *player = calloc(1, sizeof(*player));
    if (!player) {
        fprintf(stderr, "Failed to allocate audio player\n");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return -1;
    }

    SDL_AudioSpec want;
    SDL_zero(want);
    want.freq = INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS;
    want.samples = 1024;

    player->device = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
    if (player->device == 0) {
        fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
        integral_gb_runtime_audio_player_close(player);
        return -1;
    }

    player->max_queue_bytes = max_queue_ms *
                              INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE *
                              INTEGRAL_GB_RUNTIME_AUDIO_BYTES_PER_FRAME / 1000u;
    SDL_PauseAudioDevice(player->device, 0);
    *player_out = player;
    return 0;
}

void integral_gb_runtime_audio_player_queue_slot(IntegralGBRuntimeAudioPlayer *player, IntegralGBRuntimeSlot *slot)
{
    if (!player || !slot || player->device == 0) {
        return;
    }

    int16_t samples[INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS];
    unsigned frames = integral_gb_runtime_slot_drain_audio(slot, samples, INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES);
    if (frames == 0) {
        return;
    }

    integral_gb_runtime_audio_player_queue_samples(player, samples, frames);
}

void integral_gb_runtime_audio_player_queue_samples(IntegralGBRuntimeAudioPlayer *player, const int16_t *samples, unsigned frames)
{
    if (!player || !samples || frames == 0 || player->device == 0) {
        return;
    }

    unsigned queued = SDL_GetQueuedAudioSize(player->device);
    if (queued > player->max_queue_bytes) {
        SDL_ClearQueuedAudio(player->device);
        player->queue_resets++;
    }

    SDL_QueueAudio(player->device,
                   samples,
                   frames * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS * sizeof(int16_t));
    player->packets_queued++;
    player->frames_queued += frames;
}

void integral_gb_runtime_audio_player_print_stats(const IntegralGBRuntimeAudioPlayer *player)
{
    if (!player) {
        return;
    }
    printf("  local audio packets queued: %u\n", player->packets_queued);
    printf("  local audio frames queued: %u\n", player->frames_queued);
    printf("  local audio queue resets: %u\n", player->queue_resets);
}

void integral_gb_runtime_audio_player_close(IntegralGBRuntimeAudioPlayer *player)
{
    if (!player) {
        return;
    }
    if (player->device != 0) {
        SDL_CloseAudioDevice(player->device);
    }
    free(player);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

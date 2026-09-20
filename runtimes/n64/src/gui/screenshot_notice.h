/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_SCREENSHOT_NOTICE_H
#define INTEGRAL_N64_SCREENSHOT_NOTICE_H
#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct IntegralN64ScreenshotNotice {
    Uint32 window_id;
    Uint64 until;
    char original_title[256];
} IntegralN64ScreenshotNotice;

static inline void integral_n64_screenshot_notice_tick(
    IntegralN64ScreenshotNotice *notice, Uint64 now)
{
    if (!notice->window_id || now < notice->until) return;
    SDL_Window *window = SDL_GetWindowFromID(notice->window_id);
    if (window) SDL_SetWindowTitle(window, notice->original_title);
    notice->window_id = 0;
}

static inline void integral_n64_screenshot_notice_show(
    IntegralN64ScreenshotNotice *notice, SDL_Window *window, bool saved, Uint64 now)
{
    if (!window) return;
    Uint32 id = SDL_GetWindowID(window);
    if (notice->window_id != id) {
        integral_n64_screenshot_notice_tick(notice, notice->until);
        snprintf(notice->original_title, sizeof(notice->original_title),
                 "%s", SDL_GetWindowTitle(window));
        notice->window_id = id;
    }
    char title[288];
    snprintf(title, sizeof(title), "%s - SCREENSHOT %s",
             notice->original_title, saved ? "SAVED" : "FAILED");
    SDL_SetWindowTitle(window, title);
    notice->until = now + 2000;
}
#endif

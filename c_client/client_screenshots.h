/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_SCREENSHOTS_H
#define INTEGRAL_CLIENT_SCREENSHOTS_H
#include <SDL.h>
#include <stdbool.h>
#include <stddef.h>
#include <time.h>

typedef struct { char *path; time_t modified; } IntegralScreenshotEntry;
typedef struct {
    IntegralScreenshotEntry *entries;
    size_t count, selected;
    SDL_Surface *image;
    SDL_Texture *texture;
    bool confirm_delete, confirm_yes;
    char status[80];
} IntegralScreenshots;

IntegralScreenshots *integral_screenshots_open(const char *root);
void integral_screenshots_close(IntegralScreenshots *viewer);
/* true means return to the main menu; no nested event loop. */
bool integral_screenshots_key(IntegralScreenshots *viewer, const SDL_KeyboardEvent *key);
void integral_screenshots_draw(SDL_Renderer *renderer, IntegralScreenshots *viewer);
SDL_Rect integral_screenshots_fit(int width, int height);
#endif

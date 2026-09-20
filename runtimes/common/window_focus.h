/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_WINDOW_FOCUS_H
#define INTEGRAL_WINDOW_FOCUS_H

#include <SDL.h>

/* Call once immediately after creating a visible game window, on its SDL
 * thread. Never retry from focus/restore events or the rendering loop.
 * SDL delegates activation to Cocoa, Windows or the Linux window system;
 * an OS focus-policy refusal is nonfatal. Hidden diagnostic windows stay hidden. */
static inline void integral_focus_new_game_window(SDL_Window *window)
{
    if (!window || (SDL_GetWindowFlags(window) & SDL_WINDOW_HIDDEN)) return;
    SDL_RaiseWindow(window);
    (void)SDL_SetWindowInputFocus(window);
}

#endif

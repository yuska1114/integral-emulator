/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "wait_window.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "key_config.h"
#include "display_scale.h"
#include "protocol.h"
#include "sdl_text.h"
#include "string_util.h"

struct IntegralGBRuntimeWaitWindow {
    SDL_Window *window;
    SDL_Renderer *renderer;
    char host[128];
    unsigned port;
    SDL_Keycode escape_key;
    bool return_to_menu_requested;
    bool return_confirm;
    bool return_confirm_yes;
};

int integral_gb_runtime_wait_window_open(IntegralGBRuntimeWaitWindow **window_out,
                               const char *host,
                               unsigned port,
                               SDL_Keycode escape_key)
{
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    (void)integral_gb_runtime_key_config_open_game_controllers();
    IntegralGBRuntimeWaitWindow *window = calloc(1, sizeof(*window));
    if (!window) {
        SDL_Quit();
        return -1;
    }
    if (!integral_gb_runtime_copy_text(window->host, sizeof(window->host), host ? host : "127.0.0.1")) {
        fprintf(stderr, "wait window host text is too long\n");
        free(window);
        SDL_Quit();
        return -1;
    }
    window->port = port;
    window->escape_key = escape_key;

    SDL_Rect usable = {0, 0, 480, 480};
    if (SDL_GetDisplayUsableBounds(0, &usable) != 0) {
        usable.w = 480;
        usable.h = 480;
    }
    unsigned scale = integral_display_scale_resolve(
        integral_display_scale_from_environment("INTEGRAL_EMULATOR_DISPLAY_SCALE"),
        usable.w, usable.h, 480, 480);
    window->window = SDL_CreateWindow("INTEGRAL EMULATOR - GB Runtime Waiting",
                                      SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED,
                                      480 * (int)scale,
                                      480 * (int)scale,
                                      SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        integral_gb_runtime_wait_window_close(window);
        return -1;
    }
    SDL_RaiseWindow(window->window);
    (void)SDL_SetWindowInputFocus(window->window);

    window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window->renderer) {
        window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!window->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        integral_gb_runtime_wait_window_close(window);
        return -1;
    }
    if (SDL_RenderSetLogicalSize(window->renderer, 480, 480) != 0) {
        fprintf(stderr, "SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
        integral_gb_runtime_wait_window_close(window);
        return -1;
    }
    (void)SDL_RenderSetIntegerScale(window->renderer, SDL_TRUE);

    *window_out = window;
    return 0;
}

bool integral_gb_runtime_wait_window_poll_and_render(IntegralGBRuntimeWaitWindow *window)
{
    if (!window) {
        return true;
    }

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            window->return_confirm = true;
            window->return_confirm_yes = false;
            continue;
        }
        if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
            event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
            integral_gb_runtime_key_config_handle_device_event(&event);
            continue;
        }
        bool escape_pressed = event.type == SDL_KEYDOWN &&
                              !event.key.repeat &&
                              event.key.keysym.sym == SDLK_ESCAPE;
        bool escape_binding_pressed = false;
        if (integral_gb_runtime_key_config_binding_matches_event(window->escape_key, &event, &escape_binding_pressed) &&
            escape_binding_pressed) {
            escape_pressed = true;
        }
        if (window->return_confirm) {
            bool left_right = event.type == SDL_KEYDOWN && !event.key.repeat &&
                              (event.key.keysym.sym == SDLK_LEFT ||
                               event.key.keysym.sym == SDLK_RIGHT);
            bool enter = event.type == SDL_KEYDOWN && !event.key.repeat &&
                         (event.key.keysym.sym == SDLK_RETURN ||
                          event.key.keysym.sym == SDLK_KP_ENTER);
            if (escape_pressed) {
                window->return_confirm = false;
                window->return_confirm_yes = false;
            }
            else if (left_right) {
                window->return_confirm_yes = !window->return_confirm_yes;
            }
            else if (enter) {
                if (window->return_confirm_yes) {
                    window->return_to_menu_requested = true;
                    return false;
                }
                window->return_confirm = false;
            }
            continue;
        }
        if (escape_pressed) {
            window->return_confirm = true;
            window->return_confirm_yes = false;
        }
    }

    char host_line[192];
    char port_line[64];
    const char host_prefix[] = "HOST ";
    size_t host_prefix_len = sizeof(host_prefix) - 1u;
    size_t host_len = strlen(window->host);
    if (host_prefix_len + host_len + 1u <= sizeof(host_line)) {
        memcpy(host_line, host_prefix, host_prefix_len);
        memcpy(host_line + host_prefix_len, window->host, host_len + 1u);
    }
    else {
        (void)integral_gb_runtime_copy_text(host_line, sizeof(host_line), host_prefix);
    }
    snprintf(port_line, sizeof(port_line), "PORT %u", window->port);

    SDL_Color title = {238, 238, 220, 255};
    SDL_Color text = {185, 205, 216, 255};
    SDL_Color muted = {116, 132, 142, 255};
    SDL_SetRenderDrawColor(window->renderer, 20, 24, 28, 255);
    SDL_RenderClear(window->renderer);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 42, "FAMILY GBC", 5, title);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 128, "WAITING FOR CLIENT", 3, text);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 190, host_line, 3, text);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 236, port_line, 3, text);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 328, "START CLIENT MODE", 2, muted);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 360, "ON THE OTHER MACHINE", 2, muted);
    integral_gb_runtime_sdl_draw_text(window->renderer, 34, 416, "ESC RETURNS MENU", 2, muted);
    if (window->return_confirm) {
        SDL_Rect panel = {.x = 30, .y = 160, .w = 420, .h = 140};
        SDL_Color selected = {86, 220, 150, 255};
        SDL_SetRenderDrawColor(window->renderer, 10, 14, 18, 245);
        SDL_RenderFillRect(window->renderer, &panel);
        SDL_SetRenderDrawColor(window->renderer, 86, 162, 126, 255);
        SDL_RenderDrawRect(window->renderer, &panel);
        integral_gb_runtime_sdl_draw_text(window->renderer, panel.x + 36, panel.y + 28,
                                           "RETURN TO MENU?", 3, title);
        integral_gb_runtime_sdl_draw_text(window->renderer, panel.x + 86, panel.y + 84,
            window->return_confirm_yes ? "> YES" : "  YES", 3,
            window->return_confirm_yes ? selected : text);
        integral_gb_runtime_sdl_draw_text(window->renderer, panel.x + 244, panel.y + 84,
            window->return_confirm_yes ? "  NO" : "> NO", 3,
            window->return_confirm_yes ? text : selected);
    }
    SDL_RenderPresent(window->renderer);
    return true;
}

bool integral_gb_runtime_wait_window_return_to_menu_requested(const IntegralGBRuntimeWaitWindow *window)
{
    return window && window->return_to_menu_requested;
}

void integral_gb_runtime_wait_window_close(IntegralGBRuntimeWaitWindow *window)
{
    if (!window) {
        return;
    }
    if (window->renderer) {
        SDL_DestroyRenderer(window->renderer);
    }
    if (window->window) {
        SDL_DestroyWindow(window->window);
    }
    free(window);
    SDL_Quit();
}

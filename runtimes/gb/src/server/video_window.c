/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "video_window.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "key_config.h"
#include "display_scale.h"
#include "protocol.h"
#include "sdl_text.h"
#include "string_util.h"

struct IntegralGBRuntimeVideoWindow {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *slot1_texture;
    SDL_Texture *slot2_texture;
    unsigned scale;
    unsigned slot_count;
    int canvas_width;
    int canvas_height;
    int content_x;
    int content_y;
    bool active;
    char message[64];
    char speed_message[32];
    char status_message[64];
    Uint32 message_until_ms;
    Uint32 speed_message_until_ms;
    bool return_confirm;
    bool return_confirm_yes;
};

static void set_layout(IntegralGBRuntimeVideoWindow *window, int width, int height)
{
    int content_width = INTEGRAL_GB_RUNTIME_GB_WIDTH * (int)window->slot_count;
    int content_height = INTEGRAL_GB_RUNTIME_GB_HEIGHT;
    window->canvas_width = width;
    window->canvas_height = height;
    window->scale = integral_display_scale_resolve(
        INTEGRAL_DISPLAY_SCALE_AUTO, width, height, content_width, content_height);
    window->content_x = (width - content_width * (int)window->scale) / 2;
    window->content_y = (height - content_height * (int)window->scale) / 2;
}

static int update_layout(IntegralGBRuntimeVideoWindow *window)
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window->window, &width, &height);
    if (width <= 0 || height <= 0) return -1;
    if (width == window->canvas_width && height == window->canvas_height) return 0;
    if (SDL_RenderSetLogicalSize(window->renderer, width, height) != 0) return -1;
    set_layout(window, width, height);
    return 0;
}

#ifdef INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_TEST_READBACK
void integral_gb_runtime_video_window_test_readback(SDL_Renderer *renderer);
#endif

static void close_sdl_objects(IntegralGBRuntimeVideoWindow *window)
{
    if (window->slot2_texture) {
        SDL_DestroyTexture(window->slot2_texture);
    }
    if (window->slot1_texture) {
        SDL_DestroyTexture(window->slot1_texture);
    }
    if (window->renderer) {
        SDL_DestroyRenderer(window->renderer);
    }
    if (window->window) {
        SDL_DestroyWindow(window->window);
    }
}

int integral_gb_runtime_video_window_open(IntegralGBRuntimeVideoWindow **window_out, unsigned scale)
{
    return integral_gb_runtime_video_window_open_slots(window_out, scale, 2);
}

int integral_gb_runtime_video_window_open_slots(IntegralGBRuntimeVideoWindow **window_out, unsigned scale, unsigned slot_count)
{
    return integral_gb_runtime_video_window_open_titled(window_out, scale, slot_count, "GB Runtime");
}

static int open_titled(IntegralGBRuntimeVideoWindow **window_out,
                       unsigned scale,
                       unsigned slot_count,
                       const char *title,
                       bool vsync,
                       unsigned target_width,
                       unsigned target_height)
{
    if (slot_count == 0 || slot_count > 2) {
        slot_count = 2;
    }
    int content_width = INTEGRAL_GB_RUNTIME_GB_WIDTH * (int)slot_count;
    int content_height = INTEGRAL_GB_RUNTIME_GB_HEIGHT;
    if ((target_width == 0u) != (target_height == 0u) ||
        (target_width != 0u &&
         (target_width < (unsigned)content_width ||
          target_height < (unsigned)content_height))) {
        fprintf(stderr, "Invalid GB Runtime target window size\n");
        return -1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    int controller_count = integral_gb_runtime_key_config_open_game_controllers();
    if (controller_count > 0) {
        printf("  game controllers: %d\n", controller_count);
    }
    IntegralGBRuntimeVideoWindow *window = calloc(1, sizeof(*window));
    if (!window) {
        fprintf(stderr, "Failed to allocate video window\n");
        SDL_Quit();
        return -1;
    }
    window->slot_count = slot_count;
    window->active = true;

    int width = (int)target_width;
    int height = (int)target_height;
    if (target_width == 0u) {
        SDL_Rect usable = {0, 0, content_width, content_height};
        if (SDL_GetDisplayUsableBounds(0, &usable) != 0) {
            usable.w = content_width;
            usable.h = content_height;
        }
        scale = integral_display_scale_resolve(
            scale, usable.w, usable.h, content_width, content_height);
        width = content_width * (int)scale;
        height = content_height * (int)scale;
    }
    set_layout(window, width, height);
    window->window = SDL_CreateWindow(title ? title : "INTEGRAL EMULATOR - GB Runtime",
                                      SDL_WINDOWPOS_CENTERED,
                                      SDL_WINDOWPOS_CENTERED,
                                      width,
                                      height,
                                      SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI |
                                          SDL_WINDOW_RESIZABLE);
    if (!window->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        integral_gb_runtime_video_window_close(window);
        return -1;
    }
    SDL_SetWindowMinimumSize(window->window, content_width, content_height);
    SDL_RaiseWindow(window->window);
    (void)SDL_SetWindowInputFocus(window->window);

    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (vsync) {
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    window->renderer = SDL_CreateRenderer(window->window, -1, renderer_flags);
    if (!window->renderer) {
        window->renderer = SDL_CreateRenderer(window->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!window->renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        integral_gb_runtime_video_window_close(window);
        return -1;
    }
    if (SDL_RenderSetLogicalSize(window->renderer, width, height) != 0) {
        fprintf(stderr, "SDL_RenderSetLogicalSize failed: %s\n", SDL_GetError());
        integral_gb_runtime_video_window_close(window);
        return -1;
    }
    (void)SDL_RenderSetIntegerScale(window->renderer, SDL_TRUE);

    window->slot1_texture = SDL_CreateTexture(window->renderer,
                                              SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              INTEGRAL_GB_RUNTIME_GB_WIDTH,
                                              INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    window->slot2_texture = SDL_CreateTexture(window->renderer,
                                              SDL_PIXELFORMAT_ARGB8888,
                                              SDL_TEXTUREACCESS_STREAMING,
                                              INTEGRAL_GB_RUNTIME_GB_WIDTH,
                                              INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    if (!window->slot1_texture || (window->slot_count > 1 && !window->slot2_texture)) {
        fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
        integral_gb_runtime_video_window_close(window);
        return -1;
    }
#if SDL_VERSION_ATLEAST(2, 0, 12)
    SDL_SetTextureScaleMode(window->slot1_texture, SDL_ScaleModeNearest);
    if (window->slot2_texture) {
        SDL_SetTextureScaleMode(window->slot2_texture, SDL_ScaleModeNearest);
    }
#endif

    *window_out = window;
    return 0;
}

int integral_gb_runtime_video_window_open_titled(IntegralGBRuntimeVideoWindow **window_out,
                                        unsigned scale,
                                        unsigned slot_count,
                                        const char *title)
{
    return open_titled(window_out, scale, slot_count, title, true, 0u, 0u);
}

int integral_gb_runtime_video_window_open_titled_sized(
    IntegralGBRuntimeVideoWindow **window_out,
    unsigned scale,
    unsigned slot_count,
    const char *title,
    unsigned window_width,
    unsigned window_height)
{
    return open_titled(window_out, scale, slot_count, title, true,
                       window_width, window_height);
}

int integral_gb_runtime_video_window_open_titled_unthrottled(
    IntegralGBRuntimeVideoWindow **window_out,
    unsigned scale,
    unsigned slot_count,
    const char *title)
{
    return open_titled(window_out, scale, slot_count, title, false, 0u, 0u);
}

int integral_gb_runtime_video_window_open_titled_unthrottled_sized(
    IntegralGBRuntimeVideoWindow **window_out,
    unsigned scale,
    unsigned slot_count,
    const char *title,
    unsigned window_width,
    unsigned window_height)
{
    return open_titled(window_out, scale, slot_count, title, false,
                       window_width, window_height);
}

static void render_top_right_message(IntegralGBRuntimeVideoWindow *window,
                                     int x,
                                     int width,
                                     int y,
                                     const char *message)
{
    int scale = window->scale >= 2u ? 2 : 1;
    int text_w = (int)strlen(message) * 6 * scale;
    SDL_Rect panel = {
        .x = x + width - text_w - 20,
        .y = y,
        .w = text_w + 12,
        .h = 22,
    };
    SDL_SetRenderDrawColor(window->renderer, 8, 12, 16, 210);
    SDL_RenderFillRect(window->renderer, &panel);
    SDL_Color color = {238, 238, 220, 255};
    integral_gb_runtime_sdl_draw_text(window->renderer, panel.x + 6, panel.y + 4, message, scale, color);
}

static void render_textured_slots(IntegralGBRuntimeVideoWindow *window,
                                  const IntegralGBRuntimeSlot *slot2,
                                  bool confirm,
                                  bool yes_selected)
{
    if (update_layout(window) != 0) return;
    SDL_Rect slot1_rect = {
        .x = window->content_x,
        .y = window->content_y,
        .w = (int)(INTEGRAL_GB_RUNTIME_GB_WIDTH * window->scale),
        .h = (int)(INTEGRAL_GB_RUNTIME_GB_HEIGHT * window->scale),
    };
    SDL_Rect slot2_rect = {
        .x = slot1_rect.w,
        .y = slot1_rect.y,
        .w = slot1_rect.w,
        .h = slot1_rect.h,
    };
    slot2_rect.x += slot1_rect.x;

    SDL_SetRenderDrawColor(window->renderer, 0, 0, 0, 255);
    SDL_RenderClear(window->renderer);
    SDL_RenderCopy(window->renderer, window->slot1_texture, NULL, &slot1_rect);
    if (window->slot_count > 1 && slot2) {
        SDL_RenderCopy(window->renderer, window->slot2_texture, NULL, &slot2_rect);
    }

    int width = slot1_rect.w * (int)window->slot_count;
    if (confirm) {
        int height = slot1_rect.h;
        int text_scale = width >= 440 && height >= 160 ? 3 : (width >= 280 ? 2 : 1);
        int panel_width = width > 428 ? 420 : width - 8;
        int panel_height = height > 148 ? 140 : height - 8;
        SDL_Rect panel = {
            .x = window->content_x + (width - panel_width) / 2,
            .y = window->content_y + (height - panel_height) / 2,
            .w = panel_width,
            .h = panel_height,
        };
        SDL_SetRenderDrawColor(window->renderer, 10, 14, 18, 235);
        SDL_RenderFillRect(window->renderer, &panel);
        SDL_SetRenderDrawColor(window->renderer, 86, 162, 126, 255);
        SDL_RenderDrawRect(window->renderer, &panel);

        SDL_Color title = {238, 238, 220, 255};
        SDL_Color text = {185, 205, 216, 255};
        SDL_Color selected = {86, 220, 150, 255};
        integral_gb_runtime_sdl_draw_text(window->renderer, panel.x + 10, panel.y + 18,
                                          "RETURN TO MENU?", text_scale, title);
        integral_gb_runtime_sdl_draw_text(window->renderer,
                                panel.x + 10,
                                panel.y + panel.h - 32,
                                yes_selected ? "> YES" : "  YES",
                                text_scale,
                                yes_selected ? selected : text);
        integral_gb_runtime_sdl_draw_text(window->renderer,
                                panel.x + panel.w / 2,
                                panel.y + panel.h - 32,
                                !yes_selected ? "> NO" : "  NO",
                                text_scale,
                                !yes_selected ? selected : text);
    }

    int overlay_y = window->content_y + 10;
    if (window->speed_message[0] != '\0') {
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(now - window->speed_message_until_ms) < 0) {
            render_top_right_message(window, window->content_x, width,
                                     overlay_y, window->speed_message);
            overlay_y += 26;
        }
        else {
            window->speed_message[0] = '\0';
        }
    }
    if (window->status_message[0] != '\0') {
        render_top_right_message(window, window->content_x, width,
                                 overlay_y, window->status_message);
        overlay_y += 26;
    }
    if (window->message[0] != '\0') {
        Uint32 now = SDL_GetTicks();
        if ((Sint32)(now - window->message_until_ms) < 0) {
            render_top_right_message(window, window->content_x, width,
                                     overlay_y, window->message);
        }
        else {
            window->message[0] = '\0';
        }
    }

#ifdef INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_TEST_READBACK
    integral_gb_runtime_video_window_test_readback(window->renderer);
#endif
    SDL_RenderPresent(window->renderer);
}

static void open_return_confirmation(IntegralGBRuntimeVideoWindow *window,
                                     IntegralGBRuntimeInputRouter *input_router)
{
    window->return_confirm = true;
    window->return_confirm_yes = false;
    integral_gb_runtime_input_router_release_all(input_router);
}

static bool event_requests_return(const IntegralGBRuntimeInputRouter *input_router,
                                  const SDL_Event *event)
{
    bool binding_pressed = false;
    if (event->type == SDL_KEYDOWN && !event->key.repeat &&
        event->key.keysym.sym == SDLK_ESCAPE) return true;
    return integral_gb_runtime_key_config_binding_matches_event(
               input_router->escape_key, event, &binding_pressed) && binding_pressed;
}

IntegralGBRuntimeVideoWindowPollResult integral_gb_runtime_video_window_poll(IntegralGBRuntimeVideoWindow *window,
                                                           IntegralGBRuntimeInputRouter *input_router,
                                                           const IntegralGBRuntimeSlot *slot1,
                                                           const IntegralGBRuntimeSlot *slot2)
{
    (void)slot2;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
            event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
            integral_gb_runtime_key_config_handle_device_event(&event);
            continue;
        }
        if (event.type == SDL_QUIT ||
            (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE)) {
            if (!window->return_confirm) open_return_confirmation(window, input_router);
            continue;
        }
        if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                event.window.event == SDL_WINDOWEVENT_RESTORED ||
                event.window.event == SDL_WINDOWEVENT_SHOWN) {
                window->active = true;
            }
            else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                     event.window.event == SDL_WINDOWEVENT_MINIMIZED ||
                     event.window.event == SDL_WINDOWEVENT_HIDDEN) {
                window->active = false;
                integral_gb_runtime_input_router_release_all(input_router);
            }
        }
        if (window->return_confirm) {
            bool pressed = false;
            uint8_t menu_button = integral_gb_runtime_key_config_button_for_event(
                &input_router->slot1_keys, &event, &pressed);
            bool escape_pressed = event_requests_return(input_router, &event);
            bool enter_pressed = event.type == SDL_KEYDOWN && !event.key.repeat &&
                                 (event.key.keysym.sym == SDLK_RETURN ||
                                  event.key.keysym.sym == SDLK_KP_ENTER);
            bool left_right_pressed = event.type == SDL_KEYDOWN && !event.key.repeat &&
                                      (event.key.keysym.sym == SDLK_LEFT ||
                                       event.key.keysym.sym == SDLK_RIGHT);
            if (escape_pressed) {
                window->return_confirm = false;
                window->return_confirm_yes = false;
            }
            else if (left_right_pressed ||
                     (pressed && (menu_button & (INTEGRAL_GB_RUNTIME_BTN_LEFT |
                                                 INTEGRAL_GB_RUNTIME_BTN_RIGHT)))) {
                window->return_confirm_yes = !window->return_confirm_yes;
            }
            else if (enter_pressed || (pressed && (menu_button & INTEGRAL_GB_RUNTIME_BTN_A))) {
                if (window->return_confirm_yes) {
                    integral_gb_runtime_input_router_release_all(input_router);
                    return INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_RETURN_MENU;
                }
                window->return_confirm = false;
            }
            integral_gb_runtime_input_router_release_all(input_router);
            continue;
        }
        if (event_requests_return(input_router, &event)) {
            (void)slot1;
            open_return_confirmation(window, input_router);
            continue;
        }
        if (!integral_gb_runtime_input_router_handle_event(input_router, &event)) {
            open_return_confirmation(window, input_router);
            continue;
        }
        if (integral_gb_runtime_input_router_take_escape_request(input_router)) {
            (void)slot1;
            open_return_confirmation(window, input_router);
            continue;
        }
    }
    return INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE;
}

bool integral_gb_runtime_video_window_is_active(const IntegralGBRuntimeVideoWindow *window)
{
    if (!window) {
        return true;
    }
    return window->active;
}

void integral_gb_runtime_video_window_show_message(IntegralGBRuntimeVideoWindow *window, const char *message)
{
    if (!window || !message) {
        return;
    }
    (void)integral_gb_runtime_copy_text(window->message, sizeof(window->message), message);
    window->message_until_ms = SDL_GetTicks() + 1500u;
}

void integral_gb_runtime_video_window_show_speed_message(IntegralGBRuntimeVideoWindow *window, const char *message)
{
    if (!window || !message) {
        return;
    }
    (void)integral_gb_runtime_copy_text(window->speed_message, sizeof(window->speed_message), message);
    window->speed_message_until_ms = SDL_GetTicks() + 3000u;
}

void integral_gb_runtime_video_window_set_status_message(IntegralGBRuntimeVideoWindow *window, const char *message)
{
    if (!window) {
        return;
    }
    if (!message) {
        window->status_message[0] = '\0';
        return;
    }
    (void)integral_gb_runtime_copy_text(window->status_message, sizeof(window->status_message), message);
}

int integral_gb_runtime_video_window_render(IntegralGBRuntimeVideoWindow *window,
                                   const IntegralGBRuntimeSlot *slot1,
                                   const IntegralGBRuntimeSlot *slot2)
{
    int pitch = (int)(INTEGRAL_GB_RUNTIME_GB_WIDTH * sizeof(uint32_t));
    if (SDL_UpdateTexture(window->slot1_texture, NULL,
                          integral_gb_runtime_slot_presented_pixels(slot1), pitch) != 0) {
        fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return -1;
    }
    if (window->slot_count > 1 && slot2 &&
        SDL_UpdateTexture(window->slot2_texture, NULL,
                          integral_gb_runtime_slot_presented_pixels(slot2), pitch) != 0) {
        fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return -1;
    }

    render_textured_slots(window, slot2, window->return_confirm, window->return_confirm_yes);
    return 0;
}

int integral_gb_runtime_video_window_render_pixels(IntegralGBRuntimeVideoWindow *window,
                                           const uint32_t *pixels)
{
    int pitch = (int)(INTEGRAL_GB_RUNTIME_GB_WIDTH * sizeof(uint32_t));
    if (window == NULL || pixels == NULL || window->slot_count != 1u ||
        SDL_UpdateTexture(window->slot1_texture, NULL, pixels, pitch) != 0) {
        if (window != NULL && pixels != NULL)
            fprintf(stderr, "SDL_UpdateTexture failed: %s\n", SDL_GetError());
        return -1;
    }
    render_textured_slots(window, NULL, window->return_confirm, window->return_confirm_yes);
    return 0;
}

void integral_gb_runtime_video_window_close(IntegralGBRuntimeVideoWindow *window)
{
    if (!window) {
        return;
    }
    close_sdl_objects(window);
    free(window);
    SDL_Quit();
}

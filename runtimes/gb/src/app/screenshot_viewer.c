/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screenshot_viewer.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "choice_list.h"
#include "file_util.h"
#include "menu_draw.h"
#include "menu_paths.h"
#include "protocol.h"

static bool screenshot_path_from_filename(const char *name, char *out, size_t out_size)
{
    if (!name || name[0] == '\0' || strchr(name, '/') || strchr(name, '\\')) {
        return false;
    }
    const char prefix[] = "screenshot/";
    size_t prefix_len = sizeof(prefix) - 1u;
    size_t name_len = strlen(name);
    if (prefix_len + name_len + 1u > out_size) {
        if (out_size > 0) {
            out[0] = '\0';
        }
        return false;
    }
    memcpy(out, prefix, prefix_len);
    memcpy(out + prefix_len, name, name_len + 1u);
    return true;
}

static void draw_screenshot_viewer(SDL_Renderer *renderer,
                                   const ChoiceList *screenshots,
                                   unsigned selected,
                                   const char *status,
                                   bool confirm_delete,
                                   bool confirm_yes)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color muted = {112, 122, 130, 255};

    integral_gb_runtime_app_draw_text(renderer, 22, 20, "SCREENSHOT VIEW", 3, title);

    if (screenshots->count == 0) {
        integral_gb_runtime_app_draw_text(renderer, 86, 208, "NO SCREENSHOTS", 3, label);
        integral_gb_runtime_app_draw_text(renderer, 86, 258, "PRESS ANY BUTTON", 2, muted);
        SDL_RenderPresent(renderer);
        return;
    }

    char path[INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
    SDL_Texture *texture = NULL;
    int image_w = 0;
    int image_h = 0;
    if (screenshot_path_from_filename(screenshots->items[selected], path, sizeof(path))) {
        SDL_Surface *surface = SDL_LoadBMP(path);
        if (surface) {
            image_w = surface->w;
            image_h = surface->h;
            texture = SDL_CreateTextureFromSurface(renderer, surface);
            SDL_FreeSurface(surface);
        }
    }

    if (texture) {
        int scale_x = (INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 64) / image_w;
        int scale_y = 294 / image_h;
        int scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale < 1) {
            scale = 1;
        }
        SDL_Rect dest = {
            .w = image_w * scale,
            .h = image_h * scale,
        };
        dest.x = (INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - dest.w) / 2;
        dest.y = 88 + (294 - dest.h) / 2;
        SDL_RenderCopy(renderer, texture, NULL, &dest);
        SDL_DestroyTexture(texture);
    }
    else {
        integral_gb_runtime_app_draw_text(renderer, 86, 208, "LOAD FAILED", 3, label);
    }

    char count_text[64];
    snprintf(count_text, sizeof(count_text), "%u/%u", selected + 1, screenshots->count);
    integral_gb_runtime_app_draw_text(renderer, 22, 398, count_text, 2, label);
    integral_gb_runtime_app_draw_text_fit(renderer,
                                116,
                                402,
                                screenshots->items[selected],
                                1,
                                value,
                                INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 138);
    if (status && status[0] != '\0') {
        integral_gb_runtime_app_draw_text_fit(renderer, 22, 424, status, 1, muted, INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 44);
    }
    if (confirm_delete) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 235);
        SDL_Rect panel = {.x = 48, .y = 160, .w = 384, .h = 150};
        SDL_RenderFillRect(renderer, &panel);
        SDL_SetRenderDrawColor(renderer, 86, 162, 126, 255);
        SDL_RenderDrawRect(renderer, &panel);
        integral_gb_runtime_app_draw_text(renderer, 90, 190, "DELETE FILE", 4, title);
        integral_gb_runtime_app_draw_text(renderer, 112, 254, confirm_yes ? "> YES" : "  YES", 3, confirm_yes ? value : label);
        integral_gb_runtime_app_draw_text(renderer, 270, 254, !confirm_yes ? "> NO" : "  NO", 3, !confirm_yes ? value : label);
    }

    integral_gb_runtime_app_draw_text(renderer, 22, 452, "ARROWS CHANGE   DEL DELETE   ENTER ESC MENU", 1, muted);
    SDL_RenderPresent(renderer);
}

static bool event_to_viewer_input(const SDL_Event *event,
                                  const IntegralGBRuntimeKeyConfig *menu_keys,
                                  SDL_Keycode escape_key,
                                  SDL_Keycode *key_out,
                                  uint8_t *button_out,
                                  bool *escape_out)
{
    *key_out = SDLK_UNKNOWN;
    *button_out = 0;
    *escape_out = false;
    bool escape_pressed = false;
    if (integral_gb_runtime_key_config_binding_matches_event(escape_key, event, &escape_pressed) &&
        escape_pressed) {
        *escape_out = true;
    }
    if (event->type == SDL_KEYDOWN) {
        if (event->key.repeat) {
            return false;
        }
        SDL_Keycode key = event->key.keysym.sym;
        *key_out = key;
        *button_out = menu_keys ? integral_gb_runtime_key_config_button_for_key(menu_keys, key) : 0;
        if (key == SDLK_ESCAPE) {
            *escape_out = true;
        }
        return true;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN ||
        event->type == SDL_CONTROLLERBUTTONUP ||
        event->type == SDL_CONTROLLERAXISMOTION ||
        event->type == SDL_JOYBUTTONDOWN ||
        event->type == SDL_JOYBUTTONUP ||
        event->type == SDL_JOYAXISMOTION ||
        event->type == SDL_JOYHATMOTION) {
        bool pressed = false;
        *button_out = menu_keys ? integral_gb_runtime_key_config_button_for_event(menu_keys, event, &pressed) : 0;
        return (pressed && *button_out != 0) || *escape_out;
    }
    return *escape_out;
}

void integral_gb_runtime_screenshot_viewer_run(SDL_Renderer *renderer,
                                     SDL_Window *window,
                                     const IntegralGBRuntimeKeyConfig *menu_keys,
                                     SDL_Keycode escape_key)
{
    ChoiceList screenshots = {0};
    const char *suffixes[] = {".bmp"};
    integral_gb_runtime_choice_list_scan_dir_for_suffixes(&screenshots, "screenshot", suffixes, 1);
    integral_gb_runtime_choice_list_sort(&screenshots);

    unsigned selected = screenshots.count > 0 ? screenshots.count - 1 : 0;
    bool running = true;
    bool confirm_delete = false;
    bool confirm_yes = false;
    char status[128];
    snprintf(status, sizeof(status), "SCREENSHOT/ STORES SELF MODE CAPTURES");
    while (running) {
        draw_screenshot_viewer(renderer, &screenshots, selected, status, confirm_delete, confirm_yes);

        SDL_Event event;
        while (SDL_WaitEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
                break;
            }
            if (event.type == SDL_WINDOWEVENT &&
                (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ||
                 event.window.event == SDL_WINDOWEVENT_RESTORED ||
                 event.window.event == SDL_WINDOWEVENT_SHOWN)) {
                SDL_RaiseWindow(window);
                SDL_StopTextInput();
            }
            if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
                event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
                integral_gb_runtime_key_config_handle_device_event(&event);
                continue;
            }

            SDL_Keycode key = SDLK_UNKNOWN;
            uint8_t menu_button = 0;
            bool escape_pressed = false;
            if (!event_to_viewer_input(&event, menu_keys, escape_key, &key, &menu_button, &escape_pressed)) {
                continue;
            }
            if (screenshots.count == 0) {
                running = false;
                break;
            }
            if (confirm_delete) {
                if (escape_pressed) {
                    confirm_delete = false;
                    snprintf(status, sizeof(status), "DELETE CANCELED");
                    break;
                }
                if (key == SDLK_LEFT || key == SDLK_RIGHT || key == SDLK_UP || key == SDLK_DOWN ||
                    (menu_button & (INTEGRAL_GB_RUNTIME_BTN_LEFT | INTEGRAL_GB_RUNTIME_BTN_RIGHT |
                                    INTEGRAL_GB_RUNTIME_BTN_UP | INTEGRAL_GB_RUNTIME_BTN_DOWN))) {
                    confirm_yes = !confirm_yes;
                    break;
                }
                if (key == SDLK_RETURN || key == SDLK_KP_ENTER || (menu_button & INTEGRAL_GB_RUNTIME_BTN_A)) {
                    if (confirm_yes) {
                        char path[INTEGRAL_GB_RUNTIME_MENU_PATH_MAX];
                        if (screenshot_path_from_filename(screenshots.items[selected], path, sizeof(path)) &&
                            integral_gb_runtime_unlink(path) == 0) {
                            integral_gb_runtime_choice_list_remove_at(&screenshots, selected);
                            if (screenshots.count == 0) {
                                selected = 0;
                            }
                            else if (selected >= screenshots.count) {
                                selected = screenshots.count - 1;
                            }
                            snprintf(status, sizeof(status), "DELETED");
                        }
                        else {
                            snprintf(status, sizeof(status), "DELETE FAILED");
                        }
                    }
                    else {
                        snprintf(status, sizeof(status), "DELETE CANCELED");
                    }
                    confirm_delete = false;
                    break;
                }
                break;
            }
            if (escape_pressed ||
                key == SDLK_RETURN ||
                key == SDLK_KP_ENTER ||
                (menu_button & INTEGRAL_GB_RUNTIME_BTN_A)) {
                running = false;
                break;
            }
            if (key == SDLK_DELETE || key == SDLK_BACKSPACE || key == SDLK_d) {
                confirm_delete = true;
                confirm_yes = false;
                snprintf(status, sizeof(status), "CONFIRM DELETE");
                break;
            }
            if (key == SDLK_LEFT ||
                key == SDLK_UP ||
                (menu_button & (INTEGRAL_GB_RUNTIME_BTN_LEFT | INTEGRAL_GB_RUNTIME_BTN_UP))) {
                selected = (selected + screenshots.count - 1) % screenshots.count;
                break;
            }
            if (key == SDLK_RIGHT ||
                key == SDLK_DOWN ||
                (menu_button & (INTEGRAL_GB_RUNTIME_BTN_RIGHT | INTEGRAL_GB_RUNTIME_BTN_DOWN))) {
                selected = (selected + 1) % screenshots.count;
                break;
            }
        }
    }
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <SDL.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "choice_list.h"
#include "key_config_menu.h"
#include "key_config.h"
#include "log_util.h"
#include "menu_config.h"
#include "menu_draw.h"
#include "menu_paths.h"
#include "menu_state.h"
#include "net_compat.h"
#include "parse_util.h"
#include "process_util.h"
#include "protocol.h"
#include "screenshot_viewer.h"
#include "string_util.h"

#define WINDOW_WIDTH INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH
#define WINDOW_HEIGHT INTEGRAL_GB_RUNTIME_APP_WINDOW_HEIGHT
#define MENU_ROWS INTEGRAL_GB_RUNTIME_MENU_ROWS

static const char *menu_row_name(unsigned row)
{
    switch (row) {
        case 0:
            return "START";
        case 1:
            return "SLOT1 CART";
        case 2:
            return "SLOT2 CART";
        case 3:
            return "MODE";
        case 4:
            return "SPEED";
        case 5:
            return "DISCOVER";
        case 6:
            return "HOST";
        case 7:
            return "PORT";
        case 8:
            return "CLIENT CART";
        case 9:
            return "RTC OFFSET";
        default:
            return "UNKNOWN";
    }
}

static const char *sdl_event_name(Uint32 type)
{
    switch (type) {
        case SDL_QUIT:
            return "SDL_QUIT";
        case SDL_WINDOWEVENT:
            return "SDL_WINDOWEVENT";
        case SDL_KEYDOWN:
            return "SDL_KEYDOWN";
        case SDL_KEYUP:
            return "SDL_KEYUP";
        case SDL_TEXTINPUT:
            return "SDL_TEXTINPUT";
        case SDL_CONTROLLERBUTTONDOWN:
            return "SDL_CONTROLLERBUTTONDOWN";
        case SDL_CONTROLLERBUTTONUP:
            return "SDL_CONTROLLERBUTTONUP";
        case SDL_CONTROLLERAXISMOTION:
            return "SDL_CONTROLLERAXISMOTION";
        case SDL_CONTROLLERDEVICEADDED:
            return "SDL_CONTROLLERDEVICEADDED";
        case SDL_JOYBUTTONDOWN:
            return "SDL_JOYBUTTONDOWN";
        case SDL_JOYBUTTONUP:
            return "SDL_JOYBUTTONUP";
        case SDL_JOYAXISMOTION:
            return "SDL_JOYAXISMOTION";
        case SDL_JOYDEVICEADDED:
            return "SDL_JOYDEVICEADDED";
        default:
            return "SDL_EVENT";
    }
}

static const char *sdl_window_event_name(Uint8 event)
{
    switch (event) {
        case SDL_WINDOWEVENT_SHOWN:
            return "SHOWN";
        case SDL_WINDOWEVENT_HIDDEN:
            return "HIDDEN";
        case SDL_WINDOWEVENT_EXPOSED:
            return "EXPOSED";
        case SDL_WINDOWEVENT_MOVED:
            return "MOVED";
        case SDL_WINDOWEVENT_RESIZED:
            return "RESIZED";
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            return "SIZE_CHANGED";
        case SDL_WINDOWEVENT_MINIMIZED:
            return "MINIMIZED";
        case SDL_WINDOWEVENT_MAXIMIZED:
            return "MAXIMIZED";
        case SDL_WINDOWEVENT_RESTORED:
            return "RESTORED";
        case SDL_WINDOWEVENT_ENTER:
            return "ENTER";
        case SDL_WINDOWEVENT_LEAVE:
            return "LEAVE";
        case SDL_WINDOWEVENT_FOCUS_GAINED:
            return "FOCUS_GAINED";
        case SDL_WINDOWEVENT_FOCUS_LOST:
            return "FOCUS_LOST";
        case SDL_WINDOWEVENT_CLOSE:
            return "CLOSE";
        default:
            return "OTHER";
    }
}

static void log_menu_state(const IntegralGBRuntimeMenu *menu, const char *reason)
{
    fprintf(stderr,
            "menu state: %s row=%u(%s) mode=%s editing=%d quit_confirm=%d quit_yes=%d "
            "slot1='%s' slot2='%s' speed=x%u host='%s' port='%s' discover=%s client_cart=%s status='%s'\n",
            reason,
            menu->selected_row,
            menu_row_name(menu->selected_row),
            integral_gb_runtime_menu_mode_name(menu->mode),
            menu->editing ? 1 : 0,
            menu->quit_confirm ? 1 : 0,
            menu->quit_confirm_yes ? 1 : 0,
            menu->slot1_rom,
            menu->slot2_rom,
            menu->speed_multiplier,
            menu->host,
            menu->port,
            menu->client_auto_discover ? "AUTOMATIC" : "MANUAL",
            menu->client_sends_slot2_paths ? "UPLOAD SLOT1" : "RECEIVE ONLY",
            menu->status);
    fflush(stderr);
}

static void log_menu_button_action(const IntegralGBRuntimeMenu *menu, uint8_t menu_button, const char *source)
{
    fprintf(stderr,
            "menu input: source=%s row=%u(%s) mode=%s button_mask=0x%02X editing=%d quit_confirm=%d quit_yes=%d\n",
            source,
            menu->selected_row,
            menu_row_name(menu->selected_row),
            integral_gb_runtime_menu_mode_name(menu->mode),
            menu_button,
            menu->editing ? 1 : 0,
            menu->quit_confirm ? 1 : 0,
            menu->quit_confirm_yes ? 1 : 0);
    fflush(stderr);
}

static void init_menu(IntegralGBRuntimeMenu *menu)
{
    memset(menu, 0, sizeof(*menu));
    if (!integral_gb_runtime_detect_local_ipv4(menu->host, sizeof(menu->host))) {
        snprintf(menu->host, sizeof(menu->host), "127.0.0.1");
    }
    snprintf(menu->port, sizeof(menu->port), "%u", INTEGRAL_GB_RUNTIME_DEFAULT_PORT);
    snprintf(menu->status, sizeof(menu->status), "READY");
    menu->client_sends_slot2_paths = true;
    menu->client_auto_discover = true;
    menu->speed_multiplier = 1;

    integral_gb_runtime_key_config_slot1_default(&menu->slot1_key_config);
    integral_gb_runtime_key_config_slot2_default(&menu->slot2_key_config);
    menu->fast_keycode = integral_gb_runtime_key_config_fast_default();
    menu->screenshot_keycode = integral_gb_runtime_key_config_screenshot_default();
    menu->escape_keycode = integral_gb_runtime_key_config_escape_default();
    menu->turbo_hold_keycode = integral_gb_runtime_key_config_turbo_hold_default();
    menu->reset_keycode = integral_gb_runtime_key_config_reset_default();
    integral_gb_runtime_menu_load_config_file(menu);
    integral_gb_runtime_menu_refresh_key_descriptions(menu);

    const char *rom_suffixes[] = {".gb", ".gbc"};
    integral_gb_runtime_choice_list_scan_dir_for_suffixes(&menu->roms, "roms", rom_suffixes, 2);
    integral_gb_runtime_choice_list_sort(&menu->roms);

    if (menu->roms.count > 0) {
        if (!integral_gb_runtime_menu_rom_file_exists_in_roms(menu->slot1_rom)) {
            (void)integral_gb_runtime_copy_text(menu->slot1_rom, sizeof(menu->slot1_rom), menu->roms.items[0]);
        }
        menu->slot2_rom[0] = '\0';
    }
    else {
        snprintf(menu->slot1_rom, sizeof(menu->slot1_rom), "slot1.gbc");
        menu->slot2_rom[0] = '\0';
        snprintf(menu->status, sizeof(menu->status), "NO ROMS FOUND IN ROMS/");
    }
}

static bool activate_selected(IntegralGBRuntimeMenu *menu, SDL_Renderer *renderer, SDL_Window *window)
{
    log_menu_state(menu, "activate selected");
    if (menu->selected_row == 0) {
        if (menu->mode == INTEGRAL_GB_RUNTIME_MODE_SCREENSHOT_VIEW) {
            fprintf(stderr, "menu action: open screenshot viewer\n");
            fflush(stderr);
            integral_gb_runtime_screenshot_viewer_run(renderer,
                                            window,
                                            &menu->slot1_key_config,
                                            menu->escape_keycode);
            snprintf(menu->status, sizeof(menu->status), "READY");
            return true;
        }
        if (menu->mode == INTEGRAL_GB_RUNTIME_MODE_KEY_CONFIG) {
            fprintf(stderr, "menu action: open key config\n");
            fflush(stderr);
            integral_gb_runtime_key_config_menu_run(renderer, window, menu);
            snprintf(menu->status, sizeof(menu->status), "READY");
            return true;
        }
        snprintf(menu->status, sizeof(menu->status), "START FROM INTEGRAL CLIENT");
        log_menu_state(menu, "standalone launch rejected");
        return true;
    }
    fprintf(stderr, "menu action: activate non-start row, cycle selected row=%u(%s)\n",
            menu->selected_row,
            menu_row_name(menu->selected_row));
    fflush(stderr);
    integral_gb_runtime_menu_cycle_selected(menu, 1);
    log_menu_state(menu, "after non-start activate cycle");
    return true;
}

int main(int argc, char **argv)
{
    (void)integral_gb_runtime_chdir_to_package_root();
    integral_gb_runtime_log_redirect_stdio("menu");
    unsigned smoke_draw_ms = 0;
    IntegralGBRuntimeMenu menu;
    init_menu(&menu);
    log_menu_state(&menu, "initialized");

    if (argc > 1 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [--smoke-test] [--smoke-save-key-config] [--smoke-draw-ms N]\n", argv[0]);
        printf("INTEGRAL EMULATOR GB Runtime diagnostics menu. Gameplay starts through Integral Client.\n");
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--smoke-save-key-config") == 0) {
        if (integral_gb_runtime_menu_save_config_file(&menu) != 0) {
            fprintf(stderr, "failed to write key config: %s\n", integral_gb_runtime_menu_config_path());
            return 1;
        }
        printf("INTEGRAL EMULATOR GB Runtime key config smoke save\n");
        printf("  key config file: %s\n", integral_gb_runtime_menu_config_path());
        printf("  slot1 keys: %s\n", menu.slot1_keys);
        printf("  slot2 keys: %s\n", menu.slot2_keys);
        printf("  client keys: uses slot1 keys\n");
        printf("  fast key: %s\n", menu.fast_key);
        printf("  screenshot key: %s\n", menu.screenshot_key);
        printf("  escape key: %s\n", menu.escape_key);
        printf("  turbo hold key: %s\n", menu.turbo_hold_key);
        printf("  reset key: %s\n", menu.reset_key);
        return 0;
    }
    if (argc > 1 && strcmp(argv[1], "--smoke-test") == 0) {
        printf("Family GBC menu smoke test\n");
        printf("  version: %s\n", INTEGRAL_GB_RUNTIME_VERSION);
        printf("  key config file: %s\n", integral_gb_runtime_menu_config_path());
        printf("  rom candidates: %u\n", menu.roms.count);
        printf("  save policy: server-authoritative session files\n");
        printf("  slot1 cart filename: %s\n", menu.slot1_rom);
        printf("  slot2 cart filename: %s\n", menu.slot2_rom);
        printf("  mode: %s\n", integral_gb_runtime_menu_mode_name(menu.mode));
        printf("  host: %s\n", menu.host);
        printf("  port: %s\n", menu.port);
        printf("  discover: %s\n", menu.client_auto_discover ? "automatic" : "manual");
        printf("  client slot2 paths: %s\n", menu.client_sends_slot2_paths ? "on" : "off");
        printf("  speed: x%u (self only)\n", menu.speed_multiplier);
        printf("  rtc offset minutes: %+d (self only)\n", menu.rtc_offset_minutes);
        printf("  slot1 keys: %s\n", menu.slot1_keys);
        printf("  slot2 keys: %s\n", menu.slot2_keys);
        printf("  client keys: uses slot1 keys\n");
        printf("  fast key: %s\n", menu.fast_key);
        printf("  screenshot key: %s\n", menu.screenshot_key);
        printf("  escape key: %s\n", menu.escape_key);
        printf("  turbo hold key: %s\n", menu.turbo_hold_key);
        printf("  reset key: %s\n", menu.reset_key);
        return menu.roms.count > 0 ? 0 : 1;
    }
    if (argc > 2 && strcmp(argv[1], "--smoke-draw-ms") == 0) {
        smoke_draw_ms = (unsigned)strtoul(argv[2], NULL, 10);
    }

    SDL_SetHint(SDL_HINT_IME_SHOW_UI, "0");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    int controller_count = integral_gb_runtime_key_config_open_game_controllers();
    if (controller_count > 0) {
        printf("  game controllers: %d\n", controller_count);
    }
    SDL_StopTextInput();

    SDL_Window *window = SDL_CreateWindow("INTEGRAL EMULATOR - GB Runtime",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          WINDOW_WIDTH,
                                          WINDOW_HEIGHT,
                                          SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_RaiseWindow(window);
    (void)SDL_SetWindowInputFocus(window);
    SDL_StopTextInput();

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    bool running = true;
    while (running) {
        SDL_Event event;
        while (running && SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                fprintf(stderr, "menu event: %s received, exiting menu loop\n", sdl_event_name(event.type));
                fflush(stderr);
                running = false;
                break;
            }
            else if (event.type == SDL_WINDOWEVENT) {
                fprintf(stderr,
                        "menu event: %s %s window=%u data1=%d data2=%d\n",
                        sdl_event_name(event.type),
                        sdl_window_event_name(event.window.event),
                        event.window.windowID,
                        event.window.data1,
                        event.window.data2);
                fflush(stderr);
            }
            else if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
                     event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
                fprintf(stderr, "menu event: %s device changed\n", sdl_event_name(event.type));
                fflush(stderr);
                integral_gb_runtime_key_config_handle_device_event(&event);
            }
            else if (event.type == SDL_TEXTINPUT && menu.editing) {
                char *value = integral_gb_runtime_menu_row_value(&menu, menu.selected_row);
                if (value) {
                    size_t capacity = integral_gb_runtime_menu_row_value_capacity(menu.selected_row);
                    integral_gb_runtime_menu_append_text(value, capacity, event.text.text);
                }
            }
            else if (event.type == SDL_KEYDOWN && event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                if (!menu.editing &&
                    !menu.quit_confirm &&
                    menu.selected_row == 9 &&
                    (key == SDLK_LEFT || key == SDLK_RIGHT)) {
                    integral_gb_runtime_menu_cycle_selected(&menu, key == SDLK_RIGHT ? 1 : -1);
                }
            }
            else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                SDL_Keycode key = event.key.keysym.sym;
                bool escape_pressed = key == SDLK_ESCAPE;
                bool escape_binding_pressed = false;
                if (integral_gb_runtime_key_config_binding_matches_event(menu.escape_keycode, &event, &escape_binding_pressed) &&
                    escape_binding_pressed) {
                    escape_pressed = true;
                }
                if (menu.quit_confirm) {
                    uint8_t menu_button = integral_gb_runtime_key_config_button_for_key(&menu.slot1_key_config, key);
                    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_A;
                    }
                    else if (key == SDLK_LEFT) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_LEFT;
                    }
                    else if (key == SDLK_RIGHT) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_RIGHT;
                    }
                    log_menu_button_action(&menu, menu_button, "keyboard quit confirm");
                    if (escape_pressed) {
                        fprintf(stderr, "menu quit: canceled by escape\n");
                        fflush(stderr);
                        menu.quit_confirm = false;
                    }
                    else if (key == SDLK_LEFT ||
                             key == SDLK_RIGHT ||
                             (menu_button & (INTEGRAL_GB_RUNTIME_BTN_LEFT | INTEGRAL_GB_RUNTIME_BTN_RIGHT))) {
                        menu.quit_confirm_yes = !menu.quit_confirm_yes;
                    }
                    else if (key == SDLK_RETURN ||
                             key == SDLK_KP_ENTER ||
                             (menu_button & INTEGRAL_GB_RUNTIME_BTN_A)) {
                        if (menu.quit_confirm_yes) {
                            fprintf(stderr, "menu quit: confirmed yes, exiting menu loop\n");
                            fflush(stderr);
                            running = false;
                            break;
                        }
                        else {
                            fprintf(stderr, "menu quit: selected no, close confirm\n");
                            fflush(stderr);
                            menu.quit_confirm = false;
                        }
                    }
                }
                else if (escape_pressed) {
                    if (menu.editing) {
                        fprintf(stderr, "menu edit: escape closes editing row=%u(%s)\n",
                                menu.selected_row,
                                menu_row_name(menu.selected_row));
                        fflush(stderr);
                        menu.editing = false;
                        SDL_StopTextInput();
                    }
                    else {
                        fprintf(stderr, "menu quit: open confirm by escape binding/key\n");
                        fflush(stderr);
                        menu.quit_confirm = true;
                        menu.quit_confirm_yes = false;
                    }
                }
                else if (menu.editing) {
                    char *value = integral_gb_runtime_menu_row_value(&menu, menu.selected_row);
                    if (key == SDLK_RETURN) {
                        menu.editing = false;
                        SDL_StopTextInput();
                    }
                    else if (key == SDLK_BACKSPACE && value && strlen(value) > 0) {
                        value[strlen(value) - 1] = '\0';
                    }
                }
                else if (key == SDLK_F2) {
                    if (integral_gb_runtime_menu_row_value(&menu, menu.selected_row)) {
                        menu.editing = true;
                        SDL_StartTextInput();
                    }
                    else {
                        snprintf(menu.status, sizeof(menu.status), "F2 EDITS CART HOST PORT");
                    }
                }
                else {
                    uint8_t menu_button = integral_gb_runtime_key_config_button_for_key(&menu.slot1_key_config, key);
                    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_A;
                    }
                    else if (key == SDLK_UP) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_UP;
                    }
                    else if (key == SDLK_DOWN) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_DOWN;
                    }
                    else if (key == SDLK_LEFT) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_LEFT;
                    }
                    else if (key == SDLK_RIGHT) {
                        menu_button |= INTEGRAL_GB_RUNTIME_BTN_RIGHT;
                    }
                    if (menu_button != 0) {
                        log_menu_button_action(&menu, menu_button, "keyboard");
                    }
                    if (menu_button & INTEGRAL_GB_RUNTIME_BTN_UP) {
                        menu.selected_row = (menu.selected_row + MENU_ROWS - 1) % MENU_ROWS;
                        log_menu_state(&menu, "after keyboard up");
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_DOWN) {
                        menu.selected_row = (menu.selected_row + 1) % MENU_ROWS;
                        log_menu_state(&menu, "after keyboard down");
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_LEFT) {
                        integral_gb_runtime_menu_cycle_selected(&menu, -1);
                        log_menu_state(&menu, "after keyboard left");
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_RIGHT) {
                        integral_gb_runtime_menu_cycle_selected(&menu, 1);
                        log_menu_state(&menu, "after keyboard right");
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_A) {
                        (void)activate_selected(&menu, renderer, window);
                    }
                }
            }
            else if ((event.type == SDL_CONTROLLERBUTTONDOWN ||
                      event.type == SDL_CONTROLLERBUTTONUP ||
                      event.type == SDL_CONTROLLERAXISMOTION ||
                      event.type == SDL_JOYBUTTONDOWN ||
                      event.type == SDL_JOYBUTTONUP ||
                      event.type == SDL_JOYAXISMOTION ||
                      event.type == SDL_JOYHATMOTION) &&
                     !menu.editing) {
                bool pressed = false;
                uint8_t menu_button = integral_gb_runtime_key_config_button_for_event(&menu.slot1_key_config, &event, &pressed);
                bool escape_pressed = false;
                if (integral_gb_runtime_key_config_binding_matches_event(menu.escape_keycode, &event, &escape_pressed) &&
                    escape_pressed) {
                    if (menu.quit_confirm) {
                        fprintf(stderr, "menu quit: canceled by controller escape binding event=%s\n",
                                sdl_event_name(event.type));
                        fflush(stderr);
                        menu.quit_confirm = false;
                    }
                    else {
                        fprintf(stderr, "menu quit: open confirm by controller escape binding event=%s\n",
                                sdl_event_name(event.type));
                        fflush(stderr);
                        menu.quit_confirm = true;
                        menu.quit_confirm_yes = false;
                    }
                    continue;
                }
                if (!pressed) {
                    continue;
                }
                log_menu_button_action(&menu, menu_button, "controller/joystick");
                if (menu.quit_confirm) {
                    if (menu_button & (INTEGRAL_GB_RUNTIME_BTN_LEFT | INTEGRAL_GB_RUNTIME_BTN_RIGHT)) {
                        menu.quit_confirm_yes = !menu.quit_confirm_yes;
                        log_menu_state(&menu, "after controller quit confirm toggle");
                    }
                    else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_A) {
                        if (menu.quit_confirm_yes) {
                            fprintf(stderr, "menu quit: confirmed yes by controller, exiting menu loop\n");
                            fflush(stderr);
                            running = false;
                            break;
                        }
                        else {
                            fprintf(stderr, "menu quit: selected no by controller, close confirm\n");
                            fflush(stderr);
                            menu.quit_confirm = false;
                        }
                    }
                    continue;
                }
                if (menu_button & INTEGRAL_GB_RUNTIME_BTN_UP) {
                    menu.selected_row = (menu.selected_row + MENU_ROWS - 1) % MENU_ROWS;
                    log_menu_state(&menu, "after controller up");
                }
                else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_DOWN) {
                    menu.selected_row = (menu.selected_row + 1) % MENU_ROWS;
                    log_menu_state(&menu, "after controller down");
                }
                else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_LEFT) {
                    integral_gb_runtime_menu_cycle_selected(&menu, -1);
                    log_menu_state(&menu, "after controller left");
                }
                else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_RIGHT) {
                    integral_gb_runtime_menu_cycle_selected(&menu, 1);
                    log_menu_state(&menu, "after controller right");
                }
                else if (menu_button & INTEGRAL_GB_RUNTIME_BTN_A) {
                    (void)activate_selected(&menu, renderer, window);
                }
            }
        }

        integral_gb_runtime_menu_draw(renderer, &menu);
        if (smoke_draw_ms > 0) {
            SDL_Delay(smoke_draw_ms);
            break;
        }
        SDL_Delay(16);
    }

    log_menu_state(&menu, "menu loop exited");
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    fprintf(stderr, "menu exit: normal return without launching runtime\n");
    fflush(stderr);
    return 0;
}

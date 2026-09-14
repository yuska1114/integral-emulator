/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "menu.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include "gui/cart.h"
#include "gui/hotkeys.h"
#include "gui/keymap.h"
#include "gui/text.h"

enum {
    GUI_WIDTH = 480,
    GUI_HEIGHT = 480,
    GUI_MAIN_ROWS = 4,
    GUI_SUBMENU_ROWS = 5,
    GUI_MAX_ROMS = 128,
    GUI_PATH_MAX = 1024,
    GUI_MAX_JOYSTICKS = 16,
    GUI_KEYMAP_VISIBLE_ROWS = 8,
};

#define N64_CONFIG_PATH "n64_runtime.conf"
#define N64_CONFIG_PART_PATH "n64_runtime.conf.part"

typedef enum GuiScreen {
    GUI_SCREEN_MAIN,
    GUI_SCREEN_CONTROLLERS,
    GUI_SCREEN_TRANSFER,
    GUI_SCREEN_KEY_CONFIG,
    GUI_SCREEN_HOTKEYS,
} GuiScreen;

typedef struct GuiState {
    char roms[GUI_MAX_ROMS][GUI_PATH_MAX];
    unsigned rom_count;
    unsigned rom_index;
    unsigned selected_row;
    GuiScreen screen;
    int controller_modes[4];
    IntegralN64RuntimeKeymap keymaps[4];
    IntegralN64RuntimeKeymap pending_keymap;
    unsigned key_config_controller;
    unsigned key_config_step;
    bool key_config_wait_release;
    IntegralN64RuntimeBinding captured_binding;
    SDL_Joystick *joysticks[GUI_MAX_JOYSTICKS];
    IntegralN64RuntimeHotkeys hotkeys;
    bool hotkey_capturing;
    bool hotkey_wait_release;
    IntegralN64RuntimeBinding hotkey_captured_binding;
    IntegralN64RuntimeCartCatalog carts;
    unsigned slot_choices[4];
    char status[128];
} GuiState;

static const char *controller_mode_name(int mode)
{
    return mode == 1 ? "CUSTOM" : "AUTO";
}

static void open_joysticks(GuiState *state)
{
    int count = SDL_NumJoysticks();
    int index;
    SDL_JoystickEventState(SDL_ENABLE);
    for (index = 0; index < count; ++index) {
        SDL_JoystickID instance = SDL_JoystickGetDeviceInstanceID(index);
        int slot;
        bool already_open = false;
        for (slot = 0; slot < GUI_MAX_JOYSTICKS; ++slot) {
            if (state->joysticks[slot] &&
                SDL_JoystickInstanceID(state->joysticks[slot]) == instance)
                already_open = true;
        }
        if (already_open) continue;
        for (slot = 0; slot < GUI_MAX_JOYSTICKS; ++slot) {
            if (!state->joysticks[slot]) {
                state->joysticks[slot] = SDL_JoystickOpen(index);
                break;
            }
        }
    }
}

static void close_removed_joystick(GuiState *state, SDL_JoystickID instance)
{
    int slot;
    for (slot = 0; slot < GUI_MAX_JOYSTICKS; ++slot) {
        if (state->joysticks[slot] &&
            SDL_JoystickInstanceID(state->joysticks[slot]) == instance) {
            SDL_JoystickClose(state->joysticks[slot]);
            state->joysticks[slot] = NULL;
        }
    }
}

static void close_joysticks(GuiState *state)
{
    int index;
    for (index = 0; index < GUI_MAX_JOYSTICKS; ++index) {
        if (state->joysticks[index]) SDL_JoystickClose(state->joysticks[index]);
        state->joysticks[index] = NULL;
    }
}

static bool has_rom_suffix(const char *name)
{
    size_t length = strlen(name);
    if (length < 4u) return false;
    name += length - 4u;
    return SDL_strcasecmp(name, ".z64") == 0 ||
           SDL_strcasecmp(name, ".n64") == 0 ||
           SDL_strcasecmp(name, ".v64") == 0;
}

static void scan_roms(GuiState *state)
{
    DIR *directory = opendir("roms");
    struct dirent *entry;
    if (!directory) {
        snprintf(state->status, sizeof(state->status), "ROMS DIRECTORY NOT FOUND");
        return;
    }
    while (state->rom_count < GUI_MAX_ROMS && (entry = readdir(directory)) != NULL) {
        if (has_rom_suffix(entry->d_name)) {
            snprintf(state->roms[state->rom_count], GUI_PATH_MAX,
                     "roms/%s", entry->d_name);
            ++state->rom_count;
        }
    }
    closedir(directory);
    if (state->rom_count == 0u) {
        snprintf(state->status, sizeof(state->status), "NO N64 ROMS IN ROMS/");
    }
}

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static bool replace_config_file(void)
{
#ifdef _WIN32
    return MoveFileExA(N64_CONFIG_PART_PATH,
                       N64_CONFIG_PATH,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return rename(N64_CONFIG_PART_PATH, N64_CONFIG_PATH) == 0;
#endif
}

static bool save_config(const GuiState *state)
{
    FILE *file = fopen(N64_CONFIG_PART_PATH, "w");
    unsigned controller;
    if (!file) return false;
    if (state->rom_count > 0u) {
        fprintf(file, "rom=%s\n", state->roms[state->rom_index]);
    }
    for (controller = 0u; controller < 4u; ++controller) {
        fprintf(file, "controller%u=%d\n", controller + 1u,
                state->controller_modes[controller]);
        {
            char map[INTEGRAL_N64_RUNTIME_KEYMAP_TEXT_MAX];
            integral_n64_runtime_keymap_format(&state->keymaps[controller], map,
                                    sizeof(map));
            fprintf(file, "keymap%u=%s\n", controller + 1u, map);
        }
    }
    {
        char hotkeys[INTEGRAL_N64_RUNTIME_HOTKEY_TEXT_MAX];
        integral_n64_runtime_hotkeys_format(&state->hotkeys, hotkeys, sizeof(hotkeys));
        fprintf(file, "hotkeys=%s\n", hotkeys);
    }
    for (controller = 0u; controller < 4u; ++controller) {
        unsigned choice = state->slot_choices[controller];
        if (choice > 0u && choice <= state->carts.count) {
            fprintf(file, "slot%u=%s\n", controller + 1u,
                    state->carts.roms[choice - 1u]);
        }
    }
    if (fclose(file) == 0 && replace_config_file()) {
        return true;
    }
    (void)remove(N64_CONFIG_PART_PATH);
    return false;
}

static bool load_config_path(GuiState *state, const char *path)
{
    FILE *file = fopen(path, "r");
    char line[GUI_PATH_MAX + 32];
    if (!file) return false;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strncmp(line, "controller", 10u) == 0 &&
                 line[10] >= '1' && line[10] <= '4' && line[11] == '=') {
            int mode = atoi(line + 12);
            if (mode == 1 || mode == 2) {
                state->controller_modes[line[10] - '1'] = mode;
            }
        }
        else if (strncmp(line, "rom=", 4u) == 0) {
            size_t length = strcspn(line + 4, "\r\n");
            unsigned rom;
            line[4 + length] = '\0';
            for (rom = 0u; rom < state->rom_count; ++rom) {
                if (strcmp(state->roms[rom], line + 4) == 0) {
                    state->rom_index = rom;
                    break;
                }
            }
        }
        else if (strncmp(line, "keymap", 6u) == 0 &&
                 line[6] >= '1' && line[6] <= '4' && line[7] == '=') {
            size_t length = strcspn(line + 8, "\r\n");
            IntegralN64RuntimeKeymap parsed;
            line[8 + length] = '\0';
            if (integral_n64_runtime_keymap_parse(line + 8, &parsed))
                state->keymaps[line[6] - '1'] = parsed;
        }
        else if (strncmp(line, "hotkeys=", 8u) == 0) {
            size_t length = strcspn(line + 8, "\r\n");
            IntegralN64RuntimeHotkeys parsed;
            line[8 + length] = '\0';
            if (integral_n64_runtime_hotkeys_parse(line + 8, &parsed))
                state->hotkeys = parsed;
        }
        else if (strncmp(line, "slot", 4u) == 0 &&
                 line[4] >= '1' && line[4] <= '4' && line[5] == '=') {
            size_t length = strcspn(line + 6, "\r\n");
            int found;
            line[6 + length] = '\0';
            found = integral_n64_runtime_cart_catalog_find(&state->carts, line + 6);
            if (found >= 0) state->slot_choices[line[4] - '1'] = (unsigned)found + 1u;
        }
    }
    (void)fclose(file);
    return true;
}

static void load_config(GuiState *state)
{
    (void)load_config_path(state, N64_CONFIG_PATH);
}

int integral_n64_runtime_gui_config_load_test(void)
{
    GuiState state;
    memset(&state, 0, sizeof(state));
    for (unsigned controller = 0u; controller < 4u; ++controller) {
        state.controller_modes[controller] = 2;
        integral_n64_runtime_keymap_defaults(&state.keymaps[controller]);
    }
    integral_n64_runtime_hotkeys_defaults(&state.hotkeys);
    load_config(&state);
    FILE *file = fopen(N64_CONFIG_PATH, "r");
    if (!file) return 1;
    (void)fclose(file);
    return 0;
}

static void draw_menu(SDL_Renderer *renderer, const GuiState *state)
{
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    const char *labels[GUI_MAIN_ROWS] = {
        "N64 CART", "CONTROLLERS", "TRANSFER PAK", "EMULATOR KEYS"
    };
    const char *rom = state->rom_count > 0u
                          ? base_name(state->roms[state->rom_index])
                          : "<NO ROM>";
    const char *values[GUI_MAIN_ROWS] = {
        rom, "P1-P4 SETUP", "LOCAL SLOT1-4", "STOP / SHOT / PAUSE..."
    };
    unsigned row;

    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);
    integral_n64_runtime_gui_draw_text_fit(renderer, 22, 20, "N64 RUNTIME", 3,
                                            title, 330);
    integral_n64_runtime_gui_draw_text(renderer, 366, 26, "VER1.0", 1, muted);
    for (row = 0u; row < GUI_MAIN_ROWS; ++row) {
        int y = 88 + (int)row * 60;
        if (state->selected_row == row) {
            SDL_Rect highlight = {14, y - 10, GUI_WIDTH - 28, 34};
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_RenderFillRect(renderer, &highlight);
            integral_n64_runtime_gui_draw_text(renderer, 24, y, ">", 2,
                                                selected);
        }
        integral_n64_runtime_gui_draw_text(
            renderer, 44, y, labels[row], 2,
            state->selected_row == row ? selected : label);
        integral_n64_runtime_gui_draw_text_fit(renderer, 210, y, values[row], 2,
                                                value, GUI_WIDTH - 228);
    }
    SDL_SetRenderDrawColor(renderer, 45, 52, 58, 255);
    SDL_Rect separator = {22, 385, GUI_WIDTH - 44, 2};
    SDL_RenderFillRect(renderer, &separator);
    integral_n64_runtime_gui_draw_text_fit(renderer, 22, 402, state->status, 1,
                                            muted, GUI_WIDTH - 44);
    integral_n64_runtime_gui_draw_text(renderer, 22, 438,
                                       "ENTER SELECT  ARROWS CHANGE", 1, muted);
    integral_n64_runtime_gui_draw_text(renderer, 22, 456, "ESC QUIT", 1,
                                       muted);
    SDL_RenderPresent(renderer);
}

static void draw_submenu(SDL_Renderer *renderer, const GuiState *state)
{
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    unsigned row;
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);
    integral_n64_runtime_gui_draw_text(
        renderer, 22, 20,
        state->screen == GUI_SCREEN_CONTROLLERS ? "CONTROLLERS" : "TRANSFER PAK",
        3, title);
    for (row = 0u; row < GUI_SUBMENU_ROWS; ++row) {
        char label_text[24];
        const char *row_value;
        int y = 92 + (int)row * 52;
        if (row < 4u) {
            snprintf(label_text, sizeof(label_text),
                     state->screen == GUI_SCREEN_CONTROLLERS
                         ? "CONTROLLER %u" : "SLOT %u",
                     row + 1u);
            row_value = state->screen == GUI_SCREEN_CONTROLLERS
                            ? controller_mode_name(state->controller_modes[row])
                            : integral_n64_runtime_cart_name(
                                  &state->carts, state->slot_choices[row]);
        }
        else {
            snprintf(label_text, sizeof(label_text), "BACK");
            row_value = "PRESS A";
        }
        if (state->selected_row == row) {
            SDL_Rect highlight = {14, y - 10, GUI_WIDTH - 28, 34};
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_RenderFillRect(renderer, &highlight);
            integral_n64_runtime_gui_draw_text(renderer, 24, y, ">", 2,
                                                selected);
        }
        integral_n64_runtime_gui_draw_text(
            renderer, 44, y, label_text, 2,
            state->selected_row == row ? selected : label);
        integral_n64_runtime_gui_draw_text_fit(renderer, 230, y, row_value, 2,
                                                value, GUI_WIDTH - 248);
    }
    integral_n64_runtime_gui_draw_text_fit(
        renderer, 22, 402,
        state->screen == GUI_SCREEN_CONTROLLERS
            ? "ENTER CONFIG / LEFT RIGHT PROFILE"
            : "LEFT RIGHT SELECTS GB CART",
        1, muted, GUI_WIDTH - 44);
    integral_n64_runtime_gui_draw_text(renderer, 22, 438,
                                       "ENTER SELECT  ARROWS MOVE", 1, muted);
    integral_n64_runtime_gui_draw_text(renderer, 22, 456, "ESC BACK", 1,
                                       muted);
    SDL_RenderPresent(renderer);
}

static void draw_key_config(SDL_Renderer *renderer, const GuiState *state)
{
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    unsigned first = 0u;
    unsigned row;
    char heading[32];
    if (state->key_config_step >= GUI_KEYMAP_VISIBLE_ROWS) {
        first = state->key_config_step - GUI_KEYMAP_VISIBLE_ROWS + 1u;
    }
    snprintf(heading, sizeof(heading), "CONTROLLER %u CONFIG",
             state->key_config_controller + 1u);
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);
    integral_n64_runtime_gui_draw_text(renderer, 22, 20, heading, 3, title);
    for (row = 0u; row < GUI_KEYMAP_VISIBLE_ROWS &&
                   first + row < INTEGRAL_N64_RUNTIME_KEY_BINDINGS; ++row) {
        unsigned binding = first + row;
        char binding_name[64];
        int y = 72 + (int)row * 39;
        bool active = binding == state->key_config_step;
        integral_n64_runtime_binding_name(&state->pending_keymap.bindings[binding],
                               binding_name, sizeof(binding_name));
        if (active) {
            SDL_Rect highlight = {14, y - 7, GUI_WIDTH - 28, 29};
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_RenderFillRect(renderer, &highlight);
            integral_n64_runtime_gui_draw_text(renderer, 24, y, ">", 1, selected);
        }
        integral_n64_runtime_gui_draw_text_fit(renderer, 42, y,
                                   integral_n64_runtime_key_binding_labels[binding], 1,
                                   active ? selected : label, 158);
        integral_n64_runtime_gui_draw_text_fit(renderer, 214, y, binding_name, 1, value,
                                   GUI_WIDTH - 230);
    }
    integral_n64_runtime_gui_draw_text_fit(renderer, 22, 402,
                               state->key_config_wait_release
                                   ? "RELEASE INPUT"
                                   : "PRESS PHYSICAL KEY / JOY-CON INPUT",
                               1, muted, GUI_WIDTH - 44);
    integral_n64_runtime_gui_draw_text(renderer, 22, 438, "18 INPUTS ARE CAPTURED IN ORDER", 1, muted);
    integral_n64_runtime_gui_draw_text(renderer, 22, 456, "ESC CANCEL", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_hotkeys(SDL_Renderer *renderer, const GuiState *state)
{
    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    unsigned first = 0u;
    unsigned row;
    unsigned total = INTEGRAL_N64_RUNTIME_HOTKEY_COUNT + 1u;
    if (state->selected_row >= GUI_KEYMAP_VISIBLE_ROWS)
        first = state->selected_row - GUI_KEYMAP_VISIBLE_ROWS + 1u;
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);
    integral_n64_runtime_gui_draw_text(renderer, 22, 20, "EMULATOR KEYS", 3, title);
    for (row = 0u; row < GUI_KEYMAP_VISIBLE_ROWS && first + row < total; ++row) {
        unsigned entry = first + row;
        char binding_name[64];
        const char *entry_label;
        int y = 72 + (int)row * 39;
        bool active = entry == state->selected_row;
        if (entry < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT) {
            entry_label = integral_n64_runtime_hotkey_labels[entry];
            integral_n64_runtime_hotkey_name(&state->hotkeys.entries[entry], binding_name,
                                  sizeof(binding_name));
        }
        else {
            entry_label = "BACK";
            snprintf(binding_name, sizeof(binding_name), "PRESS A");
        }
        if (active) {
            SDL_Rect highlight = {14, y - 7, GUI_WIDTH - 28, 29};
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_RenderFillRect(renderer, &highlight);
            integral_n64_runtime_gui_draw_text(renderer, 24, y, ">", 1, selected);
        }
        integral_n64_runtime_gui_draw_text_fit(renderer, 42, y, entry_label, 1,
                                   active ? selected : label, 158);
        integral_n64_runtime_gui_draw_text_fit(renderer, 214, y, binding_name, 1, value,
                                   GUI_WIDTH - 230);
    }
    integral_n64_runtime_gui_draw_text_fit(
        renderer, 22, 402,
        state->hotkey_wait_release ? "RELEASE INPUT" :
        (state->hotkey_capturing ? "PRESS PHYSICAL KEY / JOY-CON INPUT" :
                                  "ENTER SET / DELETE UNDEFINE"),
        1, muted, GUI_WIDTH - 44);
    integral_n64_runtime_gui_draw_text(renderer, 22, 438, "DEFAULT: ALL UNDEFINED", 1, muted);
    integral_n64_runtime_gui_draw_text(renderer, 22, 456, "ESC BACK", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_current(SDL_Renderer *renderer, const GuiState *state)
{
    if (state->screen == GUI_SCREEN_MAIN) draw_menu(renderer, state);
    else if (state->screen == GUI_SCREEN_KEY_CONFIG) draw_key_config(renderer, state);
    else if (state->screen == GUI_SCREEN_HOTKEYS) draw_hotkeys(renderer, state);
    else draw_submenu(renderer, state);
}

static void begin_key_config(GuiState *state, unsigned controller)
{
    state->key_config_controller = controller;
    state->pending_keymap = state->keymaps[controller];
    state->key_config_step = 0u;
    state->key_config_wait_release = false;
    state->screen = GUI_SCREEN_KEY_CONFIG;
    snprintf(state->status, sizeof(state->status), "PRESS INPUT FOR D-PAD RIGHT");
}

static void finish_key_config(GuiState *state)
{
    unsigned controller = state->key_config_controller;
    unsigned binding;
    bool has_joystick = false;
    for (binding = 0u; binding < INTEGRAL_N64_RUNTIME_KEY_BINDINGS; ++binding) {
        if (state->pending_keymap.bindings[binding].kind !=
            INTEGRAL_N64_RUNTIME_BINDING_SCANCODE) has_joystick = true;
    }
    if (!has_joystick) state->pending_keymap.device = -1;
    state->keymaps[controller] = state->pending_keymap;
    state->controller_modes[controller] = 1;
    state->screen = GUI_SCREEN_CONTROLLERS;
    state->selected_row = controller;
    state->key_config_wait_release = false;
    snprintf(state->status, sizeof(state->status), "CONTROLLER %u CONFIG SAVED",
             controller + 1u);
    save_config(state);
}

static void activate(GuiState *state)
{
    if (state->selected_row == 1u) {
        state->screen = GUI_SCREEN_CONTROLLERS;
        state->selected_row = 0u;
    }
    else if (state->selected_row == 2u) {
        state->screen = GUI_SCREEN_TRANSFER;
        state->selected_row = 0u;
    }
    else if (state->selected_row == 3u) {
        state->screen = GUI_SCREEN_HOTKEYS;
        state->selected_row = 0u;
    }
}

static void activate_submenu(GuiState *state)
{
    if (state->screen == GUI_SCREEN_CONTROLLERS && state->selected_row < 4u) {
        begin_key_config(state, state->selected_row);
    }
    else if (state->selected_row == 4u) {
        state->screen = GUI_SCREEN_MAIN;
        state->selected_row = 0u;
    }
}

static unsigned current_row_count(const GuiState *state)
{
    if (state->screen == GUI_SCREEN_MAIN) return GUI_MAIN_ROWS;
    if (state->screen == GUI_SCREEN_HOTKEYS)
        return INTEGRAL_N64_RUNTIME_HOTKEY_COUNT + 1u;
    return GUI_SUBMENU_ROWS;
}

static void change_selected(GuiState *state, int direction)
{
    if (state->screen == GUI_SCREEN_CONTROLLERS && state->selected_row < 4u) {
        state->controller_modes[state->selected_row] =
            state->controller_modes[state->selected_row] == 1 ? 2 : 1;
        snprintf(state->status, sizeof(state->status),
                 "CONTROLLER PROFILE CHANGED");
        save_config(state);
        return;
    }
    if (state->screen == GUI_SCREEN_TRANSFER && state->selected_row < 4u) {
        unsigned limit = state->carts.count + 1u;
        unsigned *choice = &state->slot_choices[state->selected_row];
        if (direction < 0)
            *choice = *choice == 0u ? limit - 1u : *choice - 1u;
        else
            *choice = (*choice + 1u) % limit;
        snprintf(state->status, sizeof(state->status),
                 "TRANSFER CART SELECTED");
        save_config(state);
        return;
    }
    if (state->screen == GUI_SCREEN_MAIN && state->selected_row == 0u &&
        state->rom_count > 0u) {
        if (direction < 0) {
            state->rom_index = state->rom_index == 0u
                                   ? state->rom_count - 1u
                                   : state->rom_index - 1u;
        }
        else {
            state->rom_index = (state->rom_index + 1u) % state->rom_count;
        }
        snprintf(state->status, sizeof(state->status), "ROM SELECTED");
        save_config(state);
    }
}

static int save_screenshot(SDL_Renderer *renderer, const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, GUI_WIDTH, GUI_HEIGHT,
                                                          32, SDL_PIXELFORMAT_ARGB8888);
    int result;
    if (!surface) return -1;
    result = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_ARGB8888,
                                  surface->pixels, surface->pitch);
    if (result == 0) result = SDL_SaveBMP(surface, path);
    SDL_FreeSurface(surface);
    return result;
}

int integral_n64_runtime_gui_run(const char *program_path, const char *smoke_path,
                      int smoke_screen)
{
    (void)program_path;
    GuiState state;
    SDL_Window *window;
    SDL_Renderer *renderer;
    bool running = true;
    int window_flags = smoke_path ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
    memset(&state, 0, sizeof(state));
    for (unsigned controller = 0u; controller < 4u; ++controller) {
        state.controller_modes[controller] = 2;
        integral_n64_runtime_keymap_defaults(&state.keymaps[controller]);
    }
    integral_n64_runtime_hotkeys_defaults(&state.hotkeys);
    if (smoke_screen == 1) state.screen = GUI_SCREEN_CONTROLLERS;
    else if (smoke_screen == 2) {
        state.screen = GUI_SCREEN_TRANSFER;
        state.selected_row = 4u;
    }
    else if (smoke_screen == 3) {
        state.screen = GUI_SCREEN_KEY_CONFIG;
        state.pending_keymap = state.keymaps[0];
        state.key_config_controller = 0u;
        state.key_config_step = 12u;
    }
    else if (smoke_screen == 4) {
        state.screen = GUI_SCREEN_HOTKEYS;
        state.selected_row = 8u;
    }
    snprintf(state.status, sizeof(state.status), "READY");
    scan_roms(&state);
    integral_n64_runtime_cart_catalog_scan(&state.carts);
    if (smoke_screen == 2 && state.carts.count > 0u) {
        state.slot_choices[0] = 1u;
    }
    if (!smoke_path) load_config(&state);
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_JOYSTICK |
                 SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "N64 Runtime GUI: SDL init failed: %s\n", SDL_GetError());
        return 30;
    }
    open_joysticks(&state);
    window = SDL_CreateWindow("INTEGRAL EMULATOR - N64 Runtime", SDL_WINDOWPOS_CENTERED,
                              SDL_WINDOWPOS_CENTERED, GUI_WIDTH, GUI_HEIGHT,
                              window_flags);
    if (!window) {
        fprintf(stderr, "N64 Runtime GUI: window failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 31;
    }
    SDL_RaiseWindow(window);
    (void)SDL_SetWindowInputFocus(window);
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!renderer) {
        fprintf(stderr, "N64 Runtime GUI: renderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 32;
    }
    draw_current(renderer, &state);
    if (smoke_path) {
        int result = save_screenshot(renderer, smoke_path);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        close_joysticks(&state);
        SDL_Quit();
        if (result != 0) {
            fprintf(stderr, "N64 Runtime GUI: screenshot failed: %s\n", SDL_GetError());
            return 33;
        }
        printf("N64 Runtime GUI: screenshot saved to %s\n", smoke_path);
        return 0;
    }

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_JOYDEVICEADDED) {
                open_joysticks(&state);
                continue;
            }
            if (event.type == SDL_JOYDEVICEREMOVED) {
                close_removed_joystick(&state, event.jdevice.which);
                continue;
            }
            if (state.screen == GUI_SCREEN_HOTKEYS) {
                unsigned total = INTEGRAL_N64_RUNTIME_HOTKEY_COUNT + 1u;
                if (event.type == SDL_QUIT) running = false;
                else if (state.hotkey_wait_release) {
                    if (integral_n64_runtime_keymap_released(
                            &event, &state.hotkey_captured_binding)) {
                        state.hotkey_wait_release = false;
                        state.hotkey_capturing = false;
                        save_config(&state);
                    }
                }
                else if (state.hotkey_capturing) {
                    IntegralN64RuntimeBinding captured;
                    int device = -1;
                    if (integral_n64_runtime_keymap_capture(&event, &captured, &device)) {
                        IntegralN64RuntimeHotkey *entry =
                            &state.hotkeys.entries[state.selected_row];
                        entry->binding = captured;
                        entry->device = captured.kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE
                                            ? -1 : device;
                        state.hotkey_captured_binding = captured;
                        state.hotkey_wait_release = true;
                    }
                }
                else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                    SDL_Keycode key = event.key.keysym.sym;
                    if (key == SDLK_ESCAPE) {
                        state.screen = GUI_SCREEN_MAIN;
                        state.selected_row = 0u;
                    }
                    else if (key == SDLK_UP) {
                        state.selected_row = state.selected_row == 0u
                                                 ? total - 1u
                                                 : state.selected_row - 1u;
                    }
                    else if (key == SDLK_DOWN) {
                        state.selected_row = (state.selected_row + 1u) % total;
                    }
                    else if ((key == SDLK_DELETE || key == SDLK_BACKSPACE) &&
                             state.selected_row < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT) {
                        memset(&state.hotkeys.entries[state.selected_row], 0,
                               sizeof(state.hotkeys.entries[state.selected_row]));
                        state.hotkeys.entries[state.selected_row].device = -1;
                        save_config(&state);
                    }
                    else if (key == SDLK_RETURN || key == SDLK_SPACE) {
                        if (state.selected_row == INTEGRAL_N64_RUNTIME_HOTKEY_COUNT) {
                            state.screen = GUI_SCREEN_MAIN;
                            state.selected_row = 0u;
                        }
                        else state.hotkey_capturing = true;
                    }
                }
                continue;
            }
            if (state.screen == GUI_SCREEN_KEY_CONFIG) {
                if (event.type == SDL_QUIT) {
                    running = false;
                }
                else if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                         event.key.keysym.sym == SDLK_ESCAPE &&
                         !state.key_config_wait_release) {
                    state.screen = GUI_SCREEN_CONTROLLERS;
                    state.selected_row = state.key_config_controller;
                    snprintf(state.status, sizeof(state.status),
                             "CONTROLLER CONFIG CANCELLED");
                }
                else if (state.key_config_wait_release) {
                    if (integral_n64_runtime_keymap_released(&event,
                                                  &state.captured_binding)) {
                        state.key_config_wait_release = false;
                        ++state.key_config_step;
                        if (state.key_config_step >= INTEGRAL_N64_RUNTIME_KEY_BINDINGS)
                            finish_key_config(&state);
                    }
                }
                else {
                    IntegralN64RuntimeBinding captured;
                    int device = state.pending_keymap.device;
                    if (integral_n64_runtime_keymap_capture(&event, &captured, &device)) {
                        state.pending_keymap.bindings[state.key_config_step] = captured;
                        if (captured.kind != INTEGRAL_N64_RUNTIME_BINDING_SCANCODE)
                            state.pending_keymap.device = device;
                        state.captured_binding = captured;
                        state.key_config_wait_release = true;
                    }
                }
                continue;
            }
            if (event.type == SDL_QUIT) {
                running = false;
            }
            else if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                if (state.screen == GUI_SCREEN_MAIN) running = false;
                else {
                    state.screen = GUI_SCREEN_MAIN;
                    state.selected_row = 0u;
                }
            }
            else if (event.type == SDL_KEYDOWN && !event.key.repeat) {
                if (event.key.keysym.sym == SDLK_UP) {
                    unsigned rows = current_row_count(&state);
                    state.selected_row = state.selected_row == 0u ? rows - 1u : state.selected_row - 1u;
                }
                else if (event.key.keysym.sym == SDLK_DOWN) {
                    state.selected_row = (state.selected_row + 1u) %
                                         current_row_count(&state);
                }
                else if (event.key.keysym.sym == SDLK_LEFT) {
                    change_selected(&state, -1);
                }
                else if (event.key.keysym.sym == SDLK_RIGHT) {
                    change_selected(&state, 1);
                }
                else if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_SPACE) {
                    if (state.screen == GUI_SCREEN_MAIN) {
                        activate(&state);
                    }
                    else activate_submenu(&state);
                }
            }
        }
        draw_current(renderer, &state);
        SDL_Delay(16u);
    }
    save_config(&state);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    close_joysticks(&state);
    SDL_Quit();
    return 0;
}

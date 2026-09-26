/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_ui_common.h"

#include <stdio.h>
#include <string.h>

#include "sdl_text.h"
#include "sdl_unicode_text.h"

static const IntegralClientUiTheme UI_THEME = {
    .background = {20, 24, 28, 255},
    .panel_fill = {8, 12, 16, 255},
    .panel_border = {55, 64, 70, 255},
    .label = {160, 180, 196, 255},
    .value = {238, 238, 238, 255},
    .title = {238, 238, 220, 255},
    .selected = {86, 162, 126, 255},
    .selected_bright = {86, 220, 150, 255},
    .muted = {112, 122, 130, 255},
    .body_text = {185, 205, 216, 255},
    .warning = {236, 142, 108, 255},
    .warning_error = {230, 92, 76, 255},
    .cursor = {230, 92, 76, 255},
    .selection_background = {38, 72, 62, 255},
    .active_background = {112, 38, 44, 255},
    .modal_background = {10, 14, 18, 245},
};

const IntegralClientUiTheme *integral_client_ui_theme(void)
{
    return &UI_THEME;
}

static void set_draw_color(SDL_Renderer *renderer, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
}

void integral_client_ui_clear_screen(SDL_Renderer *renderer)
{
    set_draw_color(renderer, UI_THEME.background);
    SDL_RenderClear(renderer);
}

void integral_client_ui_draw_selection(SDL_Renderer *renderer,
                                      SDL_Rect bounds,
                                      int cursor_x,
                                      int cursor_y,
                                      int cursor_scale,
                                      bool editing)
{
    integral_client_ui_fill_selection(renderer, bounds, editing);
    integral_sdl_draw_text(renderer, cursor_x, cursor_y, ">", cursor_scale, UI_THEME.selected);
}

void integral_client_ui_fill_selection(SDL_Renderer *renderer, SDL_Rect bounds, bool editing)
{
    set_draw_color(renderer, editing ? UI_THEME.active_background : UI_THEME.selection_background);
    SDL_RenderFillRect(renderer, &bounds);
}

static bool contains_utf8(const char *text)
{
    for (const unsigned char *p = (const unsigned char *)text; *p; p++)
        if (*p >= 128) return true;
    return false;
}

void integral_client_ui_draw_text_fit(SDL_Renderer *renderer, int x, int y,
                                      const char *text, int scale, SDL_Color color, int width)
{
    if (contains_utf8(text))
        integral_sdl_draw_utf8_text(renderer, x, y, text, scale == 1 ? 12 : 8 * scale, color, width);
    else
        integral_sdl_draw_text_fit(renderer, x, y, text, scale, color, width);
}

void integral_client_ui_draw_panel(SDL_Renderer *renderer, int x, int y, int w, int h, SDL_Color border)
{
    SDL_Rect fill = {.x = x, .y = y, .w = w, .h = h};
    set_draw_color(renderer, UI_THEME.panel_fill);
    SDL_RenderFillRect(renderer, &fill);
    set_draw_color(renderer, border);
    SDL_RenderDrawRect(renderer, &fill);
}

void integral_client_ui_draw_field(SDL_Renderer *renderer,
                                   int y,
                                   const char *label,
                                   const char *value,
                                   bool selected,
                                   bool editing)
{
    SDL_Color box_color = selected ? UI_THEME.selected : UI_THEME.panel_border;

    if (selected) {
        SDL_Rect highlight = {.x = 14, .y = y - 8, .w = INTEGRAL_CLIENT_UI_WIDTH - 28, .h = 50};
        integral_client_ui_draw_selection(renderer, highlight, 24, y + 8, 2, editing);
    }

    integral_sdl_draw_text(renderer, 48, y, label, 2, selected ? UI_THEME.selected : UI_THEME.label);
    set_draw_color(renderer, box_color);
    SDL_Rect box = {.x = 48, .y = y + 22, .w = 384, .h = 24};
    SDL_RenderDrawRect(renderer, &box);
    integral_sdl_draw_text_fit(renderer, 56, y + 28, value[0] ? value : "<EMPTY>", 1, UI_THEME.value, 368);
    if (selected && editing) {
        set_draw_color(renderer, UI_THEME.cursor);
        SDL_Rect cursor = {.x = 420, .y = y + 26, .w = 3, .h = 17};
        SDL_RenderFillRect(renderer, &cursor);
    }
}

void integral_client_ui_draw_header(SDL_Renderer *renderer,
                                    const char *subtitle,
                                    const char *login_id,
                                    const char *server_url,
                                    const char *version)
{
    integral_sdl_draw_text(renderer, 22, 20, "INTEGRAL EMULATOR", 3, UI_THEME.title);
    enum { VERSION_DISPLAY_CHARS = 10 };
    char version_text[VERSION_DISPLAY_CHARS + 5];
    snprintf(version_text, sizeof(version_text), "VER %.*s", VERSION_DISPLAY_CHARS, version);
    integral_sdl_draw_text(renderer, 330, 30, version_text, 1, UI_THEME.muted);
    if (login_id && login_id[0] != '\0') {
        char user_text[96];
        snprintf(user_text, sizeof(user_text), "ID %s", login_id);
        integral_sdl_draw_text_fit(renderer, 330, 50, user_text, 1, UI_THEME.label, 128);
    }
    if (server_url && server_url[0] != '\0') {
        char server_text[160];
        snprintf(server_text, sizeof(server_text), "SERVER %s", server_url);
        integral_client_ui_draw_marquee_text_fit(renderer, 330, 68, server_text, 1, UI_THEME.label, 128, 0u);
    }
    integral_sdl_draw_text(renderer, 24, 62, subtitle, 2, UI_THEME.muted);
}

void integral_client_ui_draw_marquee_text_fit(SDL_Renderer *renderer,
                                               int x,
                                               int y,
                                               const char *text,
                                               int scale,
                                               SDL_Color color,
                                               int max_width,
                                               unsigned phase_seed)
{
    if (max_width <= 0 || scale <= 0) {
        return;
    }
    if (contains_utf8(text)) {
        integral_sdl_draw_utf8_scrolled(renderer, x, y, text, scale == 1 ? 12 : 8 * scale,
                                       color, max_width, SDL_GetTicks() + phase_seed * 37u, false);
        return;
    }
    size_t visible_chars = (size_t)(max_width / (6 * scale));
    size_t len = strlen(text);
    if (visible_chars == 0) {
        return;
    }
    if (len <= visible_chars) {
        integral_sdl_draw_text(renderer, x, y, text, scale, color);
        return;
    }

    size_t max_offset = len - visible_chars;
    const Uint32 hold_ms = 1000u;
    const Uint32 step_ms = 120u;
    Uint32 scroll_ms = (Uint32)(max_offset * step_ms);
    Uint32 cycle_ms = hold_ms + scroll_ms + hold_ms;
    Uint32 tick = (SDL_GetTicks() + phase_seed * 37u) % cycle_ms;
    size_t offset = 0;
    if (tick < hold_ms) {
        offset = 0;
    }
    else if (tick < hold_ms + scroll_ms) {
        offset = (size_t)((tick - hold_ms) / step_ms);
        if (offset > max_offset) {
            offset = max_offset;
        }
    }
    else {
        offset = max_offset;
    }
    char window[256];
    if (visible_chars >= sizeof(window)) {
        visible_chars = sizeof(window) - 1;
    }
    for (size_t i = 0; i < visible_chars; i++) {
        window[i] = text[offset + i];
    }
    window[visible_chars] = '\0';
    integral_sdl_draw_text(renderer, x, y, window, scale, color);
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "menu_draw.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "sdl_text.h"
#include "string_util.h"

void integral_gb_runtime_app_draw_text(SDL_Renderer *renderer,
                             int x,
                             int y,
                             const char *text,
                             int scale,
                             SDL_Color color)
{
    integral_gb_runtime_sdl_draw_text(renderer, x, y, text, scale, color);
}

void integral_gb_runtime_app_draw_text_fit(SDL_Renderer *renderer,
                                 int x,
                                 int y,
                                 const char *text,
                                 int scale,
                                 SDL_Color color,
                                 int max_width)
{
    integral_gb_runtime_sdl_draw_text_fit(renderer, x, y, text, scale, color, max_width);
}

static const char *capture_button_name(unsigned step)
{
    static const char *names[] = {
        "A",
        "B",
        "SELECT",
        "START",
        "RIGHT",
        "LEFT",
        "UP",
        "DOWN",
    };
    return step < sizeof(names) / sizeof(names[0]) ? names[step] : "";
}

static void draw_key_config_capture_overlay(SDL_Renderer *renderer, const IntegralGBRuntimeMenu *menu)
{
    if (menu->key_config_target == KEY_CONFIG_NONE) {
        return;
    }

    SDL_Color title = {238, 238, 220, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    SDL_SetRenderDrawColor(renderer, 8, 12, 16, 235);
    SDL_Rect rect = {.x = 42, .y = 150, .w = 396, .h = 190};
    SDL_RenderFillRect(renderer, &rect);
    integral_gb_runtime_app_draw_text(renderer, 78, 184, "KEY CONFIG", 4, title);
    if (menu->key_config_target == KEY_CONFIG_UTILS) {
        const char *prompt = "PRESS TURBO";
        if (menu->key_config_step == 0) {
            prompt = "PRESS FAST";
        }
        else if (menu->key_config_step == 1) {
            prompt = "PRESS SHOT";
        }
        else if (menu->key_config_step == 2) {
            prompt = "PRESS QUIT";
        }
        else if (menu->key_config_step == 3) {
            prompt = "PRESS TURBO";
        }
        else {
            prompt = "PRESS RESET";
        }
        integral_gb_runtime_app_draw_text(renderer,
                                78,
                                254,
                                prompt,
                                3,
                                selected);
    }
    else {
        char prompt[64];
        snprintf(prompt, sizeof(prompt), "PRESS %s", capture_button_name(menu->key_config_step));
        integral_gb_runtime_app_draw_text(renderer, 78, 254, prompt, 3, selected);
    }
    integral_gb_runtime_app_draw_text(renderer, 78, 310, "ESC CANCELS", 2, muted);
}

static void key_spec_to_names(const char *spec, char names[8][64])
{
    for (unsigned i = 0; i < 8; i++) {
        names[i][0] = '\0';
    }
    char copy[256];
    (void)integral_gb_runtime_copy_text(copy, sizeof(copy), spec);
    unsigned count = 0;
    char *saveptr = NULL;
    for (char *token = strtok_r(copy, ",", &saveptr);
         token && count < 8;
         token = strtok_r(NULL, ",", &saveptr)) {
        while (*token == ' ') {
            token++;
        }
        (void)integral_gb_runtime_copy_text(names[count++], sizeof(names[0]), token);
    }
}

static void format_slot_key_summary(const char *spec,
                                    char *line1,
                                    size_t line1_size,
                                    char *line2,
                                    size_t line2_size)
{
    char names[8][64];
    key_spec_to_names(spec, names);
    snprintf(line1,
             line1_size,
             "RIGHT=[%s],LEFT=[%s],UP=[%s],DOWN=[%s]",
             names[0][0] ? names[0] : "UNKNOWN",
             names[1][0] ? names[1] : "UNKNOWN",
             names[2][0] ? names[2] : "UNKNOWN",
             names[3][0] ? names[3] : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "A=[%s],B=[%s],SELECT=[%s],START=[%s]",
             names[4][0] ? names[4] : "UNKNOWN",
             names[5][0] ? names[5] : "UNKNOWN",
             names[6][0] ? names[6] : "UNKNOWN",
             names[7][0] ? names[7] : "UNKNOWN");
}

static void format_util_key_summary(const IntegralGBRuntimeMenu *menu,
                                    char *line1,
                                    size_t line1_size,
                                    char *line2,
                                    size_t line2_size,
                                    char *line3,
                                    size_t line3_size)
{
    snprintf(line1,
             line1_size,
             "FAST = [%s],SCREENSHOT = [%s]",
             menu->fast_key[0] ? menu->fast_key : "UNKNOWN",
             menu->screenshot_key[0] ? menu->screenshot_key : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "ESCAPE = [%s],TURBO = [%s]",
             menu->escape_key[0] ? menu->escape_key : "UNKNOWN",
             menu->turbo_hold_key[0] ? menu->turbo_hold_key : "UNKNOWN");
    snprintf(line3,
             line3_size,
             "RESET = [%s]",
             menu->reset_key[0] ? menu->reset_key : "UNKNOWN");
}

void integral_gb_runtime_menu_draw_key_config_menu(SDL_Renderer *renderer, const IntegralGBRuntimeMenu *menu, unsigned selected_row)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    integral_gb_runtime_app_draw_text(renderer, 22, 20, "KEY CONFIG", 4, title);
    integral_gb_runtime_app_draw_text(renderer, 330, 26, "VER " INTEGRAL_GB_RUNTIME_VERSION, 2, muted);

    char slot1_line1[192];
    char slot1_line2[192];
    char slot2_line1[192];
    char slot2_line2[192];
    char util_line1[192];
    char util_line2[192];
    char util_line3[192];
    format_slot_key_summary(menu->slot1_keys, slot1_line1, sizeof(slot1_line1), slot1_line2, sizeof(slot1_line2));
    format_slot_key_summary(menu->slot2_keys, slot2_line1, sizeof(slot2_line1), slot2_line2, sizeof(slot2_line2));
    format_util_key_summary(menu,
                            util_line1,
                            sizeof(util_line1),
                            util_line2,
                            sizeof(util_line2),
                            util_line3,
                            sizeof(util_line3));

    const char *labels[] = {"SLOT 1 KEYS", "SLOT 2 KEYS", "UTIL KEYS", "RESET KEYS", "BACK"};
    const char *values1[] = {slot1_line1, slot2_line1, util_line1, "DEFAULTS", "RETURN MENU"};
    const char *values2[] = {slot1_line2, slot2_line2, util_line2, "", ""};
    const char *values3[] = {"", "", util_line3, "", ""};

    for (unsigned i = 0; i < INTEGRAL_GB_RUNTIME_KEY_CONFIG_MENU_ROWS; i++) {
        int y = 76 + (int)i * 68;
        if (selected_row == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 28, .h = 62};
            SDL_RenderFillRect(renderer, &rect);
            integral_gb_runtime_app_draw_text(renderer, 24, y, ">", 2, selected);
        }
        integral_gb_runtime_app_draw_text(renderer, 44, y, labels[i], 2, selected_row == i ? selected : label);
        integral_gb_runtime_app_draw_text_fit(renderer, 44, y + 24, values1[i], 1, value, INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 70);
        if (values2[i][0] != '\0') {
            integral_gb_runtime_app_draw_text_fit(renderer, 44, y + 40, values2[i], 1, value, INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 70);
        }
        if (values3[i][0] != '\0') {
            integral_gb_runtime_app_draw_text_fit(renderer, 44, y + 54, values3[i], 1, value, INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 70);
        }
    }

    draw_key_config_capture_overlay(renderer, menu);

    integral_gb_runtime_app_draw_text(renderer, 22, 432, "ENTER SELECT  ARROWS MOVE", 1, muted);
    integral_gb_runtime_app_draw_text_fit(renderer, 252, 452, menu->status, 1, muted, INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 274);
    integral_gb_runtime_app_draw_text(renderer, 22, 452, "ESC BACK", 1, muted);
    SDL_RenderPresent(renderer);
}

static void draw_quit_confirm_overlay(SDL_Renderer *renderer, const IntegralGBRuntimeMenu *menu)
{
    if (!menu->quit_confirm) {
        return;
    }

    SDL_Color title = {238, 238, 220, 255};
    SDL_Color text = {238, 238, 238, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color selected = {86, 162, 126, 255};

    SDL_SetRenderDrawColor(renderer, 8, 12, 16, 235);
    SDL_Rect rect = {.x = 42, .y = 154, .w = 396, .h = 170};
    SDL_RenderFillRect(renderer, &rect);
    integral_gb_runtime_app_draw_text(renderer, 78, 190, "QUIT FAMILY GBC?", 3, title);
    integral_gb_runtime_app_draw_text(renderer, 112, 254, menu->quit_confirm_yes ? "> YES" : "  YES", 3,
                            menu->quit_confirm_yes ? selected : label);
    integral_gb_runtime_app_draw_text(renderer, 274, 254, !menu->quit_confirm_yes ? "> NO" : "  NO", 3,
                            !menu->quit_confirm_yes ? selected : label);
    integral_gb_runtime_app_draw_text(renderer, 78, 302, "A ENTER SELECT", 1, text);
}

void integral_gb_runtime_menu_draw(SDL_Renderer *renderer, const IntegralGBRuntimeMenu *menu)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color title = {238, 238, 220, 255};
    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};

    integral_gb_runtime_app_draw_text(renderer, 22, 20, "FAMILY GBC", 4, title);
    integral_gb_runtime_app_draw_text(renderer, 330, 26, "VER " INTEGRAL_GB_RUNTIME_VERSION, 2, muted);

    const char *labels[] = {
        "START",
        "SLOT1 CART",
        "SLOT2 CART",
        "MODE",
        "SPEED",
        "DISCOVER",
        "HOST",
        "PORT",
        "CLIENT CART",
        "RTC OFFSET",
    };
    const char *slot2_cart = menu->slot2_rom[0] == '\0' ? "<CLIENT UPLOAD>" : menu->slot2_rom;
    const char *auto_discover = menu->client_auto_discover ? "AUTOMATIC" : "MANUAL";
    const char *slot2_source = menu->client_sends_slot2_paths
                                   ? "UPLOAD SLOT1"
                                   : "RECEIVE ONLY";
    char rtc_offset[32];
    char speed_value[16];
    snprintf(speed_value, sizeof(speed_value), "x%u", menu->speed_multiplier);
    integral_gb_runtime_menu_format_rtc_offset(menu->rtc_offset_minutes, rtc_offset, sizeof(rtc_offset));
    bool self_features = menu->mode == INTEGRAL_GB_RUNTIME_MODE_SELF;
    const char *speed_display = self_features ? speed_value : "SELF ONLY";
    const char *rtc_offset_value = self_features ? rtc_offset : "SELF ONLY";
    const char *values[] = {
        "PRESS A",
        menu->slot1_rom,
        slot2_cart,
        integral_gb_runtime_menu_mode_name(menu->mode),
        speed_display,
        auto_discover,
        menu->host,
        menu->port,
        slot2_source,
        rtc_offset_value,
    };

    for (unsigned i = 0; i < INTEGRAL_GB_RUNTIME_MENU_ROWS; i++) {
        int y = 72 + (int)i * 28 + (i > 0 ? 14 : 0);
        if (menu->selected_row == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 8, .w = INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 28, .h = 28};
            SDL_RenderFillRect(renderer, &rect);
            integral_gb_runtime_app_draw_text(renderer, 24, y, ">", 2, selected);
        }
        integral_gb_runtime_app_draw_text(renderer, 44, y, labels[i], 2, menu->selected_row == i ? selected : label);
        int value_scale = 2;
        int value_x = 184;
        integral_gb_runtime_app_draw_text_fit(renderer,
                                    value_x,
                                    y + (value_scale == 1 ? 4 : 0),
                                    values[i],
                                    value_scale,
                                    value,
                                    INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - value_x - 18);
    }

    draw_key_config_capture_overlay(renderer, menu);
    draw_quit_confirm_overlay(renderer, menu);

    integral_gb_runtime_app_draw_text(renderer, 22, 432, "ENTER SELECT  ARROWS MOVE CHANGE", 1, muted);
    integral_gb_runtime_app_draw_text(renderer, 22, 452, "F2 EDIT   ESC QUIT", 1, muted);
    integral_gb_runtime_app_draw_text_fit(renderer,
                                318,
                                452,
                                menu->editing ? "EDITING TEXT" : menu->status,
                                1,
                                menu->editing ? selected : muted,
                                INTEGRAL_GB_RUNTIME_APP_WINDOW_WIDTH - 340);
    SDL_RenderPresent(renderer);
}

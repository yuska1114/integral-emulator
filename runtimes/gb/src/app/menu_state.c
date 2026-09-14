/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "menu_state.h"

#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "display_scale.h"

const char *integral_gb_runtime_menu_mode_name(IntegralGBRuntimeMode mode)
{
    switch (mode) {
        case INTEGRAL_GB_RUNTIME_MODE_SELF:
            return "SELF";
        case INTEGRAL_GB_RUNTIME_MODE_SERVER1:
            return "SERVER1";
        case INTEGRAL_GB_RUNTIME_MODE_SERVER2:
            return "SERVER2";
        case INTEGRAL_GB_RUNTIME_MODE_CLIENT:
            return "CLIENT";
        case INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE:
            return "LAN REMOTE";
        case INTEGRAL_GB_RUNTIME_MODE_SCREENSHOT_VIEW:
            return "SCREENSHOT VIEW";
        case INTEGRAL_GB_RUNTIME_MODE_KEY_CONFIG:
            return "KEY CONFIG";
    }
    return "SELF";
}

static bool mode_uses_self_features(IntegralGBRuntimeMode mode)
{
    return mode == INTEGRAL_GB_RUNTIME_MODE_SELF;
}

char *integral_gb_runtime_menu_row_value(IntegralGBRuntimeMenu *menu, unsigned row)
{
    switch (row) {
        case 1:
            return menu->slot1_rom;
        case 2:
            return menu->slot2_rom;
        case 6:
            return menu->host;
        case 7:
            return menu->port;
        default:
            return NULL;
    }
}

size_t integral_gb_runtime_menu_row_value_capacity(unsigned row)
{
    if (row == 6) {
        return 128;
    }
    if (row == 7) {
        return 16;
    }
    return INTEGRAL_GB_RUNTIME_MENU_PATH_MAX;
}

void integral_gb_runtime_menu_append_text(char *value, size_t capacity, const char *text)
{
    size_t length = strlen(value);
    if (length + 1 >= capacity) {
        return;
    }
    strncat(value, text, capacity - length - 1);
}

void integral_gb_runtime_menu_cycle_selected(IntegralGBRuntimeMenu *menu, int direction)
{
    if (menu->selected_row == 1) {
        integral_gb_runtime_choice_list_cycle(&menu->roms, menu->slot1_rom, sizeof(menu->slot1_rom), direction);
    }
    else if (menu->selected_row == 2) {
        integral_gb_runtime_choice_list_cycle_optional(&menu->roms, menu->slot2_rom, sizeof(menu->slot2_rom), direction);
    }
    else if (menu->selected_row == 3) {
        IntegralGBRuntimeMode previous = menu->mode;
        int mode = (int)menu->mode + direction;
        if (mode < 0) {
            mode = 6;
        }
        if (mode > 6) {
            mode = 0;
        }
        menu->mode = (IntegralGBRuntimeMode)mode;
        if (previous != INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE &&
            menu->mode == INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE &&
            strcmp(menu->port, "25100") == 0) {
            snprintf(menu->port, sizeof(menu->port), "%u", INTEGRAL_GB_RUNTIME_LAN_REMOTE_DEFAULT_PORT);
        }
        else if (previous == INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE &&
                 menu->mode != INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE &&
                 strcmp(menu->port, "25170") == 0) {
            snprintf(menu->port, sizeof(menu->port), "%u", INTEGRAL_GB_RUNTIME_DEFAULT_PORT);
        }
    }
    else if (menu->selected_row == 4) {
        if (!mode_uses_self_features(menu->mode)) {
            snprintf(menu->status, sizeof(menu->status), "SPEED SELF ONLY");
            return;
        }
        int speed = (int)menu->speed_multiplier + direction;
        if (speed < 1) {
            speed = 4;
        }
        if (speed > 4) {
            speed = 1;
        }
        menu->speed_multiplier = (unsigned)speed;
    }
    else if (menu->selected_row == 5) {
        menu->client_auto_discover = !menu->client_auto_discover;
    }
    else if (menu->selected_row == 8) {
        menu->client_sends_slot2_paths = !menu->client_sends_slot2_paths;
    }
    else if (menu->selected_row == 9) {
        if (!mode_uses_self_features(menu->mode)) {
            snprintf(menu->status, sizeof(menu->status), "RTC OFFSET SELF ONLY");
            return;
        }
        if (direction > 0) {
            if (menu->rtc_offset_minutes < 24 * 60) {
                menu->rtc_offset_minutes++;
            }
        }
        else {
            if (menu->rtc_offset_minutes > -(24 * 60)) {
                menu->rtc_offset_minutes--;
            }
        }
    }
    else if (menu->selected_row == 10) {
        int scale = (int)menu->display_scale + direction;
        if (scale < INTEGRAL_DISPLAY_SCALE_AUTO) {
            scale = INTEGRAL_DISPLAY_SCALE_MAX;
        }
        if (scale > INTEGRAL_DISPLAY_SCALE_MAX) {
            scale = INTEGRAL_DISPLAY_SCALE_AUTO;
        }
        menu->display_scale = (unsigned)scale;
        menu->display_scale_dirty = true;
    }
}

void integral_gb_runtime_menu_format_rtc_offset(int offset_minutes, char *out, size_t out_size)
{
    char sign = '+';
    int absolute = offset_minutes;
    if (offset_minutes < 0) {
        sign = '-';
        absolute = -offset_minutes;
    }
    snprintf(out, out_size, "%c%02d:%02d", sign, absolute / 60, absolute % 60);
}

bool integral_gb_runtime_menu_validate_launch_selection(IntegralGBRuntimeMenu *menu)
{
    if (!integral_gb_runtime_menu_rom_file_exists_in_roms(menu->slot1_rom)) {
        snprintf(menu->status, sizeof(menu->status), "SLOT1 MUST BE A FILE IN ROMS/");
        return false;
    }
    if (menu->mode == INTEGRAL_GB_RUNTIME_MODE_LAN_REMOTE) {
        return true;
    }
    if (menu->slot2_rom[0] != '\0' && !integral_gb_runtime_menu_rom_file_exists_in_roms(menu->slot2_rom)) {
        snprintf(menu->status, sizeof(menu->status), "SLOT2 MUST BE A FILE IN ROMS/");
        return false;
    }
    if (menu->slot2_rom[0] != '\0' && strcmp(menu->slot1_rom, menu->slot2_rom) == 0) {
        snprintf(menu->status, sizeof(menu->status), "SLOT1 AND SLOT2 CART MUST DIFFER");
        return false;
    }
    return true;
}

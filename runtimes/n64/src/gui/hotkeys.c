/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/hotkeys.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const integral_n64_runtime_hotkey_labels[INTEGRAL_N64_RUNTIME_HOTKEY_COUNT] = {
    "STOP", "FULLSCREEN", "SAVE STATE", "LOAD STATE", "NEXT SLOT",
    "RESET", "SPEED DOWN", "SPEED UP", "SCREENSHOT", "PAUSE",
    "MUTE", "VOLUME UP", "VOLUME DOWN", "FAST FORWARD",
    "SPEED LIMITER", "FRAME ADVANCE", "GAMESHARK",
    "SELECT SLOT 0", "SELECT SLOT 1", "SELECT SLOT 2", "SELECT SLOT 3",
    "SELECT SLOT 4", "SELECT SLOT 5", "SELECT SLOT 6", "SELECT SLOT 7",
    "SELECT SLOT 8", "SELECT SLOT 9",
};

const char *const integral_n64_runtime_hotkey_core_names[INTEGRAL_N64_RUNTIME_HOTKEY_COUNT] = {
    "Stop", "Fullscreen", "Save State", "Load State", "Increment Slot",
    "Reset", "Speed Down", "Speed Up", "Screenshot", "Pause", "Mute",
    "Increase Volume", "Decrease Volume", "Fast Forward",
    "Speed Limiter Toggle", "Frame Advance", "Gameshark",
    "Slot 0", "Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5",
    "Slot 6", "Slot 7", "Slot 8", "Slot 9",
};

void integral_n64_runtime_hotkeys_defaults(IntegralN64RuntimeHotkeys *hotkeys)
{
    int i;
    memset(hotkeys, 0, sizeof(*hotkeys));
    for (i = 0; i < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT; ++i)
        hotkeys->entries[i].device = -1;
}

bool integral_n64_runtime_hotkeys_parse(const char *text, IntegralN64RuntimeHotkeys *hotkeys)
{
    const char *cursor = text;
    int i;
    if (!text || !hotkeys) return false;
    for (i = 0; i < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT; ++i) {
        char *end;
        long kind = strtol(cursor, &end, 10);
        long index;
        long direction;
        long device;
        if (end == cursor || *end != ':') return false;
        cursor = end + 1;
        index = strtol(cursor, &end, 10);
        if (end == cursor || *end != ':') return false;
        cursor = end + 1;
        direction = strtol(cursor, &end, 10);
        if (end == cursor || *end != ':') return false;
        cursor = end + 1;
        device = strtol(cursor, &end, 10);
        if (end == cursor ||
            (i + 1 < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT ? *end != ',' : *end != '\0'))
            return false;
        if (kind < INTEGRAL_N64_RUNTIME_BINDING_NONE || kind > INTEGRAL_N64_RUNTIME_BINDING_HAT ||
            index < 0 || index > 65535 || direction < -128 || direction > 128 ||
            device < -1 || device > 255) return false;
        hotkeys->entries[i].binding.kind = (IntegralN64RuntimeBindingKind)kind;
        hotkeys->entries[i].binding.index = (int)index;
        hotkeys->entries[i].binding.direction = (int)direction;
        hotkeys->entries[i].device = (int)device;
        cursor = end + (i + 1 < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT ? 1 : 0);
    }
    return true;
}

void integral_n64_runtime_hotkeys_format(const IntegralN64RuntimeHotkeys *hotkeys, char *output,
                              size_t capacity)
{
    size_t used = 0u;
    int i;
    if (!hotkeys || !output || capacity == 0u) return;
    output[0] = '\0';
    for (i = 0; i < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT; ++i) {
        const IntegralN64RuntimeHotkey *entry = &hotkeys->entries[i];
        int length = snprintf(output + used, capacity - used, "%s%d:%d:%d:%d",
                              i ? "," : "", (int)entry->binding.kind,
                              entry->binding.index, entry->binding.direction,
                              entry->device);
        if (length < 0 || (size_t)length >= capacity - used) {
            output[capacity - 1u] = '\0';
            return;
        }
        used += (size_t)length;
    }
}

void integral_n64_runtime_hotkey_name(const IntegralN64RuntimeHotkey *hotkey, char *output,
                           size_t capacity)
{
    if (!hotkey || hotkey->binding.kind == INTEGRAL_N64_RUNTIME_BINDING_NONE) {
        if (output && capacity > 0u) snprintf(output, capacity, "UNDEFINED");
        return;
    }
    integral_n64_runtime_binding_name(&hotkey->binding, output, capacity);
}

void integral_n64_runtime_hotkey_joy_mapping(const IntegralN64RuntimeHotkey *hotkey, char *output,
                                  size_t capacity)
{
    const IntegralN64RuntimeBinding *binding;
    if (!output || capacity == 0u) return;
    output[0] = '\0';
    if (!hotkey || hotkey->device < 0) return;
    binding = &hotkey->binding;
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON)
        snprintf(output, capacity, "J%dB%d", hotkey->device, binding->index);
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS)
        snprintf(output, capacity, "J%dA%d%c", hotkey->device, binding->index,
                 binding->direction < 0 ? '-' : '+');
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_HAT)
        snprintf(output, capacity, "J%dH%dV%d", hotkey->device, binding->index,
                 binding->direction);
}

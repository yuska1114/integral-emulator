/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_GUI_HOTKEYS_H
#define INTEGRAL_N64_RUNTIME_GUI_HOTKEYS_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>

#include "gui/keymap.h"

enum { INTEGRAL_N64_RUNTIME_HOTKEY_COUNT = 27, INTEGRAL_N64_RUNTIME_HOTKEY_TEXT_MAX = 640 };

typedef struct IntegralN64RuntimeHotkey {
    IntegralN64RuntimeBinding binding;
    int device;
} IntegralN64RuntimeHotkey;

typedef struct IntegralN64RuntimeHotkeys {
    IntegralN64RuntimeHotkey entries[INTEGRAL_N64_RUNTIME_HOTKEY_COUNT];
} IntegralN64RuntimeHotkeys;

extern const char *const integral_n64_runtime_hotkey_labels[INTEGRAL_N64_RUNTIME_HOTKEY_COUNT];
extern const char *const integral_n64_runtime_hotkey_core_names[INTEGRAL_N64_RUNTIME_HOTKEY_COUNT];

void integral_n64_runtime_hotkeys_defaults(IntegralN64RuntimeHotkeys *hotkeys);
bool integral_n64_runtime_hotkeys_parse(const char *text, IntegralN64RuntimeHotkeys *hotkeys);
void integral_n64_runtime_hotkeys_format(const IntegralN64RuntimeHotkeys *hotkeys, char *output,
                              size_t capacity);
void integral_n64_runtime_hotkey_name(const IntegralN64RuntimeHotkey *hotkey, char *output,
                           size_t capacity);
void integral_n64_runtime_hotkey_joy_mapping(const IntegralN64RuntimeHotkey *hotkey, char *output,
                                  size_t capacity);

#endif

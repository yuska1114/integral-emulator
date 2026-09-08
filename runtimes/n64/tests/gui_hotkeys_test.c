/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED 1
#include <SDL.h>
#ifdef main
#undef main
#endif

#include "gui/hotkeys.h"

int main(void)
{
    IntegralN64RuntimeHotkeys original;
    IntegralN64RuntimeHotkeys parsed;
    char text[INTEGRAL_N64_RUNTIME_HOTKEY_TEXT_MAX];
    char name[64];
    char joy[64];
    int i;
    integral_n64_runtime_hotkeys_defaults(&original);
    for (i = 0; i < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT; ++i) {
        assert(original.entries[i].binding.kind == INTEGRAL_N64_RUNTIME_BINDING_NONE);
        assert(original.entries[i].device == -1);
        integral_n64_runtime_hotkey_name(&original.entries[i], name, sizeof(name));
        assert(strcmp(name, "UNDEFINED") == 0);
    }
    original.entries[0].binding.kind = INTEGRAL_N64_RUNTIME_BINDING_SCANCODE;
    original.entries[0].binding.index = SDL_SCANCODE_INTERNATIONAL1;
    original.entries[8].binding.kind = INTEGRAL_N64_RUNTIME_BINDING_BUTTON;
    original.entries[8].binding.index = 4;
    original.entries[8].device = 1;
    original.entries[9].binding.kind = INTEGRAL_N64_RUNTIME_BINDING_AXIS;
    original.entries[9].binding.index = 2;
    original.entries[9].binding.direction = -1;
    original.entries[9].device = 0;
    integral_n64_runtime_hotkeys_format(&original, text, sizeof(text));
    assert(integral_n64_runtime_hotkeys_parse(text, &parsed));
    assert(parsed.entries[0].binding.index == SDL_SCANCODE_INTERNATIONAL1);
    integral_n64_runtime_hotkey_joy_mapping(&parsed.entries[8], joy, sizeof(joy));
    assert(strcmp(joy, "J1B4") == 0);
    integral_n64_runtime_hotkey_joy_mapping(&parsed.entries[9], joy, sizeof(joy));
    assert(strcmp(joy, "J0A2-") == 0);
    puts("gui emulator hotkey tests passed");
    return 0;
}

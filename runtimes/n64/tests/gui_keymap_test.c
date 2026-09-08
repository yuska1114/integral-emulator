/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SDL_MAIN_HANDLED 1
#include <SDL.h>
#ifdef main
#undef main
#endif

#include "gui/keymap.h"

static void test_jis_physical_scancodes(void)
{
    const SDL_Scancode jis[] = {
        SDL_SCANCODE_NONUSHASH,
        SDL_SCANCODE_NONUSBACKSLASH,
        SDL_SCANCODE_INTERNATIONAL1,
        SDL_SCANCODE_INTERNATIONAL2,
        SDL_SCANCODE_INTERNATIONAL3,
        SDL_SCANCODE_INTERNATIONAL4,
        SDL_SCANCODE_INTERNATIONAL5,
        SDL_SCANCODE_INTERNATIONAL6,
        SDL_SCANCODE_INTERNATIONAL7,
        SDL_SCANCODE_INTERNATIONAL8,
        SDL_SCANCODE_INTERNATIONAL9,
        SDL_SCANCODE_LANG1,
        SDL_SCANCODE_LANG2,
        SDL_SCANCODE_LANG3,
        SDL_SCANCODE_LANG4,
        SDL_SCANCODE_LANG5,
        SDL_SCANCODE_LANG6,
        SDL_SCANCODE_LANG7,
        SDL_SCANCODE_LANG8,
        SDL_SCANCODE_LANG9,
    };
    size_t i;
    for (i = 0u; i < sizeof(jis) / sizeof(jis[0]); ++i) {
        SDL_Event event;
        IntegralN64RuntimeBinding binding;
        int device = 99;
        char mupen[64];
        memset(&event, 0, sizeof(event));
        event.type = SDL_KEYDOWN;
        event.key.keysym.scancode = jis[i];
        assert(integral_n64_runtime_keymap_capture(&event, &binding, &device));
        assert(binding.kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE);
        assert(binding.index == (int)jis[i]);
        assert(device == -1);
        integral_n64_runtime_keymap_mupen_button(&binding, mupen, sizeof(mupen));
        assert(strncmp(mupen, "scancode(", 9u) == 0);
    }
}

static void test_round_trip_and_mixed_axis(void)
{
    IntegralN64RuntimeKeymap original;
    IntegralN64RuntimeKeymap parsed;
    char text[INTEGRAL_N64_RUNTIME_KEYMAP_TEXT_MAX];
    char axis[128];
    integral_n64_runtime_keymap_defaults(&original);
    original.device = 2;
    original.bindings[0].kind = INTEGRAL_N64_RUNTIME_BINDING_BUTTON;
    original.bindings[0].index = 7;
    original.bindings[14].kind = INTEGRAL_N64_RUNTIME_BINDING_SCANCODE;
    original.bindings[14].index = SDL_SCANCODE_INTERNATIONAL1;
    original.bindings[15].kind = INTEGRAL_N64_RUNTIME_BINDING_AXIS;
    original.bindings[15].index = 0;
    original.bindings[15].direction = 1;
    integral_n64_runtime_keymap_format(&original, text, sizeof(text));
    assert(integral_n64_runtime_keymap_parse(text, &parsed));
    assert(parsed.device == 2);
    assert(parsed.bindings[0].kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON);
    assert(parsed.bindings[14].index == SDL_SCANCODE_INTERNATIONAL1);
    integral_n64_runtime_keymap_mupen_axis(&parsed.bindings[14], &parsed.bindings[15],
                                axis, sizeof(axis));
    assert(strstr(axis, "scancode(") != NULL);
    assert(strstr(axis, "axis(") != NULL);
}

static void test_jis_names_are_stable(void)
{
    IntegralN64RuntimeBinding ke = {INTEGRAL_N64_RUNTIME_BINDING_SCANCODE,
                           SDL_SCANCODE_APOSTROPHE, 0};
    IntegralN64RuntimeBinding mu = {INTEGRAL_N64_RUNTIME_BINDING_SCANCODE,
                           SDL_SCANCODE_NONUSHASH, 0};
    IntegralN64RuntimeBinding unknown = {INTEGRAL_N64_RUNTIME_BINDING_SCANCODE, 9999, 0};
    char name[64];
    integral_n64_runtime_binding_name(&ke, name, sizeof(name));
    assert(strcmp(name, "JIS KE") == 0);
    integral_n64_runtime_binding_name(&mu, name, sizeof(name));
    assert(strcmp(name, "JIS MU") == 0);
    integral_n64_runtime_binding_name(&unknown, name, sizeof(name));
    assert(strcmp(name, "SCANCODE 9999") == 0);
}

static void test_direction_order_is_consistent(void)
{
    IntegralN64RuntimeKeymap map;
    integral_n64_runtime_keymap_defaults(&map);

    assert(strcmp(integral_n64_runtime_key_binding_labels[0], "D-PAD RIGHT") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[1], "D-PAD LEFT") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[2], "D-PAD UP") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[3], "D-PAD DOWN") == 0);

    assert(strcmp(integral_n64_runtime_key_binding_labels[8], "C RIGHT") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[9], "C LEFT") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[10], "C UP") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[11], "C DOWN") == 0);

    assert(strcmp(integral_n64_runtime_key_binding_labels[14], "ANALOG RIGHT") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[15], "ANALOG LEFT") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[16], "ANALOG UP") == 0);
    assert(strcmp(integral_n64_runtime_key_binding_labels[17], "ANALOG DOWN") == 0);

    assert(strcmp(integral_n64_runtime_mupen_button_names[0], "DPad R") == 0);
    assert(strcmp(integral_n64_runtime_mupen_button_names[1], "DPad L") == 0);
    assert(strcmp(integral_n64_runtime_mupen_button_names[2], "DPad U") == 0);
    assert(strcmp(integral_n64_runtime_mupen_button_names[3], "DPad D") == 0);

    assert(strcmp(integral_n64_runtime_mupen_button_names[8], "C Button R") == 0);
    assert(strcmp(integral_n64_runtime_mupen_button_names[9], "C Button L") == 0);
    assert(strcmp(integral_n64_runtime_mupen_button_names[10], "C Button U") == 0);
    assert(strcmp(integral_n64_runtime_mupen_button_names[11], "C Button D") == 0);

    assert(map.bindings[0].index == SDL_SCANCODE_D);
    assert(map.bindings[1].index == SDL_SCANCODE_A);
    assert(map.bindings[2].index == SDL_SCANCODE_W);
    assert(map.bindings[3].index == SDL_SCANCODE_S);
    assert(map.bindings[8].index == SDL_SCANCODE_L);
    assert(map.bindings[9].index == SDL_SCANCODE_J);
    assert(map.bindings[10].index == SDL_SCANCODE_I);
    assert(map.bindings[11].index == SDL_SCANCODE_K);
    assert(map.bindings[14].index == SDL_SCANCODE_RIGHT);
    assert(map.bindings[15].index == SDL_SCANCODE_LEFT);
    assert(map.bindings[16].index == SDL_SCANCODE_UP);
    assert(map.bindings[17].index == SDL_SCANCODE_DOWN);
}

static void test_joycon_binding_formats(void)
{
    IntegralN64RuntimeBinding button = {INTEGRAL_N64_RUNTIME_BINDING_BUTTON, 3, 0};
    IntegralN64RuntimeBinding axis_negative = {INTEGRAL_N64_RUNTIME_BINDING_AXIS, 1, -1};
    IntegralN64RuntimeBinding axis_positive = {INTEGRAL_N64_RUNTIME_BINDING_AXIS, 1, 1};
    IntegralN64RuntimeBinding hat = {INTEGRAL_N64_RUNTIME_BINDING_HAT, 0, SDL_HAT_LEFT};
    char output[128];
    integral_n64_runtime_keymap_mupen_button(&button, output, sizeof(output));
    assert(strcmp(output, "button(3)") == 0);
    integral_n64_runtime_keymap_mupen_button(&hat, output, sizeof(output));
    assert(strcmp(output, "hat(0 LEFT)") == 0);
    integral_n64_runtime_keymap_mupen_axis(&axis_negative, &axis_positive, output,
                                sizeof(output));
    assert(strcmp(output, "axis(1-,1+)") == 0);
}

int main(void)
{
    test_jis_physical_scancodes();
    test_round_trip_and_mixed_axis();
    test_jis_names_are_stable();
    test_direction_order_is_consistent();
    test_joycon_binding_formats();
    puts("gui keymap tests passed");
    return 0;
}

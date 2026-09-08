/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "key_config.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { fprintf(stderr, "FAIL: %s\n", message); result = 1; goto cleanup; } \
} while (0)

static SDL_Event joystick_axis_event(SDL_JoystickID instance, Uint8 axis, Sint16 value)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_JOYAXISMOTION;
    event.jaxis.which = instance;
    event.jaxis.axis = axis;
    event.jaxis.value = value;
    return event;
}

static SDL_Event joystick_hat_event(SDL_JoystickID instance, Uint8 hat, Uint8 value)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = SDL_JOYHATMOTION;
    event.jhat.which = instance;
    event.jhat.hat = hat;
    event.jhat.value = value;
    return event;
}

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int result = 0;
    int virtual_a = -1;
    int virtual_b = -1;
    SDL_Joystick *joystick_a = NULL;
    SDL_Joystick *joystick_b = NULL;
    if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL init failed: %s\n", SDL_GetError());
        return 1;
    }
    virtual_a = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 8, 1);
    CHECK(virtual_a >= 0, "attach first virtual Joy-Con-equivalent device");
    char guid[33];
    char mapping[512];
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(virtual_a), guid, sizeof(guid));
    snprintf(mapping, sizeof(mapping),
             "%s,Virtual Joy-Con,a:b0,b:b1,x:b2,y:b3,back:b4,start:b5,"
             "leftshoulder:b6,rightshoulder:b7,leftx:a0,lefty:a1,"
             "dpup:h0.1,dpdown:h0.4,dpleft:h0.8,dpright:h0.2,",
             guid);
    CHECK(SDL_GameControllerAddMapping(mapping) >= 0 && SDL_IsGameController(virtual_a),
          "virtual Joy-Con is exposed through SDL GameController");
    CHECK(integral_gb_runtime_key_config_open_game_controllers() >= 1,
          "open first virtual device");
    virtual_b = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 8, 1);
    CHECK(virtual_b >= 0, "hot-plug second virtual Joy-Con-equivalent device");
    SDL_Event added;
    SDL_zero(added);
    added.type = SDL_JOYDEVICEADDED;
    added.jdevice.which = virtual_b;
    integral_gb_runtime_key_config_handle_device_event(&added);
    joystick_a = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(virtual_a));
    joystick_b = SDL_JoystickFromInstanceID(SDL_JoystickGetDeviceInstanceID(virtual_b));
    CHECK(joystick_a && joystick_b, "resolve virtual joystick handles");
    SDL_JoystickID instance_a = SDL_JoystickInstanceID(joystick_a);
    SDL_JoystickID instance_b = SDL_JoystickInstanceID(joystick_b);

    SDL_Event a_down;
    SDL_zero(a_down);
    a_down.type = SDL_JOYBUTTONDOWN;
    a_down.jbutton.which = instance_a;
    a_down.jbutton.button = 2;
    a_down.jbutton.state = SDL_PRESSED;
    SDL_Event b_down = a_down;
    b_down.jbutton.which = instance_b;
    SDL_Keycode a_button = integral_gb_runtime_key_config_code_from_event(&a_down);
    SDL_Keycode b_button = integral_gb_runtime_key_config_code_from_event(&b_down);
    CHECK(a_button != SDLK_UNKNOWN && b_button != SDLK_UNKNOWN && a_button != b_button,
          "same button on two devices has distinct bindings");
    const char *a_name_value = integral_gb_runtime_key_config_key_name(a_button);
    char a_name[96];
    snprintf(a_name, sizeof(a_name), "%s", a_name_value);
    CHECK(strncmp(a_name, "JOY@", 4) == 0, "saved raw Joystick binding contains device identity");
    CHECK(integral_gb_runtime_key_config_key_from_name(a_name) == a_button,
          "button binding round trip");
    bool pressed = false;
    CHECK(integral_gb_runtime_key_config_binding_matches_event(a_button, &a_down, &pressed) && pressed,
          "bound device button press accepted");
    CHECK(!integral_gb_runtime_key_config_binding_matches_event(a_button, &b_down, &pressed),
          "other device button rejected");
    SDL_Event a_up = a_down;
    a_up.type = SDL_JOYBUTTONUP;
    a_up.jbutton.state = SDL_RELEASED;
    CHECK(integral_gb_runtime_key_config_binding_matches_event(a_button, &a_up, &pressed) && !pressed,
          "button release accepted");

    SDL_Event axis = joystick_axis_event(instance_a, 1, -24000);
    SDL_Keycode axis_binding = integral_gb_runtime_key_config_code_from_event(&axis);
    CHECK(axis_binding != SDLK_UNKNOWN, "axis captured");
    CHECK(integral_gb_runtime_key_config_binding_matches_event(axis_binding, &axis, &pressed) && pressed,
          "axis press accepted");
    axis.jaxis.value = 0;
    CHECK(integral_gb_runtime_key_config_binding_matches_event(axis_binding, &axis, &pressed) && !pressed,
          "axis neutral releases binding");

    SDL_Event hat = joystick_hat_event(instance_a, 0, SDL_HAT_UP);
    SDL_Keycode hat_binding = integral_gb_runtime_key_config_code_from_event(&hat);
    CHECK(hat_binding != SDLK_UNKNOWN, "hat captured");
    CHECK(integral_gb_runtime_key_config_binding_matches_event(hat_binding, &hat, &pressed) && pressed,
          "hat direction accepted");
    hat.jhat.value = SDL_HAT_CENTERED;
    CHECK(integral_gb_runtime_key_config_binding_matches_event(hat_binding, &hat, &pressed) && !pressed,
          "hat center releases binding");

    IntegralGBRuntimeKeyConfig config;
    char spec[1536];
    char axis_name[96];
    char hat_name[96];
    snprintf(axis_name, sizeof(axis_name), "%s",
             integral_gb_runtime_key_config_key_name(axis_binding));
    snprintf(hat_name, sizeof(hat_name), "%s",
             integral_gb_runtime_key_config_key_name(hat_binding));
    snprintf(spec, sizeof(spec), "%s,%s,%s,%s,%s,%s,%s,%s",
             a_name, a_name, axis_name, hat_name,
             a_name, a_name, a_name, a_name);
    CHECK(integral_gb_runtime_key_config_parse(&config, spec) == 0,
          "device-qualified GB key specification parses");
    char described[1536];
    integral_gb_runtime_key_config_describe(&config, described, sizeof(described));
    CHECK(strcmp(spec, described) == 0, "device-qualified GB key specification persists exactly");

    int device = -1, kind = 0, index = -1, direction = 0;
    CHECK(integral_gb_runtime_key_config_binding_to_n64(a_button, &device, &kind, &index, &direction),
          "binding converts for N64 Runtime");
    CHECK(device >= 0 && kind == 2 && index == 2 && direction == 0,
          "N64 Runtime receives the exact captured raw button");
    SDL_Event semantic_button;
    SDL_zero(semantic_button);
    semantic_button.type = SDL_CONTROLLERBUTTONDOWN;
    semantic_button.cbutton.which = instance_a;
    semantic_button.cbutton.button = SDL_CONTROLLER_BUTTON_A;
    CHECK(integral_gb_runtime_key_config_code_from_event(&semantic_button) == SDLK_UNKNOWN,
          "semantic GameController face label cannot replace raw capture");

cleanup:
    integral_gb_runtime_key_config_close_game_controllers();
    if (virtual_b >= 0) (void)SDL_JoystickDetachVirtual(virtual_b);
    if (virtual_a >= 0) (void)SDL_JoystickDetachVirtual(virtual_a);
    SDL_Quit();
    if (result == 0) printf("GB Runtime controller input test passed\n");
    return result;
}

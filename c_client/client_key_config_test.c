/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_key_config.h"
#include "../runtimes/gb/src/common/key_config.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "key editor line %d: %s\n", __LINE__, #x); return 1; } } while (0)
static IntegralClientKeyEditor editor;
static IntegralConfigKeys keys;
static char path[1024], status[192];
static bool press(SDL_Keycode sym, SDL_Scancode scan, unsigned repeat)
{
    SDL_KeyboardEvent event = {0};
    event.type = SDL_KEYDOWN; event.keysym.sym = sym; event.keysym.scancode = scan; event.repeat = repeat;
    return integral_client_key_editor_keyboard(&editor, &keys, path, status, sizeof(status), &event);
}
static void controller(SDL_Event *event)
{
    integral_client_key_editor_controller(&editor, &keys, path, status, sizeof(status), event);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    snprintf(path, sizeof(path), "%s/keys.conf", argv[1]);
    CHECK(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0);
    integral_keys_defaults(&keys);
    editor.page = KEY_CONFIG_PAGE_GB;
    CHECK(!press(SDLK_UP, SDL_SCANCODE_UP, 0) && editor.key_selected == 3);
    CHECK(!press(SDLK_DOWN, SDL_SCANCODE_DOWN, 0) && editor.key_selected == 0);
    press(SDLK_DOWN, SDL_SCANCODE_DOWN, 1); CHECK(editor.key_selected == 0);
    CHECK(!press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0));
    CHECK(editor.key_capture_target == KEY_CAPTURE_SLOT1);
    press(SDLK_UNKNOWN, SDL_SCANCODE_UNKNOWN, 0);
    CHECK(editor.key_capture_step == 0 && strcmp(status, "UNKNOWN KEY") == 0);
    for (unsigned i = 0; i < 8; i++) press(SDLK_a + (int)i, SDL_SCANCODE_A + i, 0);
    CHECK(editor.key_capture_target == KEY_CAPTURE_NONE && strcmp(status, "KEY CONFIG SAVED") == 0);
    CHECK(strcmp(keys.slot1, "E,F,G,H,A,B,C,D") == 0);
    IntegralConfigKeys restored = {0};
    CHECK(integral_config_load_keys(path, &restored) == 0 && strcmp(restored.slot1, keys.slot1) == 0);
    editor.key_selected = 1; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    press(SDLK_z, SDL_SCANCODE_Z, 0);
    CHECK(!press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 0));
    CHECK(editor.key_capture_target == KEY_CAPTURE_NONE && strcmp(status, "KEY CONFIG CANCELED") == 0);
    CHECK(strstr(keys.slot2, ",Z,") != NULL); /* Existing partial edits stay in memory, not saved. */
    CHECK(integral_config_load_keys(path, &restored) == 0 && strstr(restored.slot2, ",G,") != NULL);
    CHECK(press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 0));
    editor.key_selected = 3;
    CHECK(press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0) && strcmp(status, "SETTINGS") == 0);

    editor.page = KEY_CONFIG_PAGE_N64;
    editor.key_selected = 0;
    CHECK(!press(SDLK_LEFT, SDL_SCANCODE_LEFT, 0) && editor.n64_controller_index == 3u);
    CHECK(!press(SDLK_RIGHT, SDL_SCANCODE_RIGHT, 0) && editor.n64_controller_index == 0u);
    CHECK(!press(SDLK_RIGHT, SDL_SCANCODE_RIGHT, 0) && editor.n64_controller_index == 1u);
    CHECK(!press(SDLK_RIGHT, SDL_SCANCODE_RIGHT, 0) && editor.n64_controller_index == 2u);
    CHECK(!press(SDLK_RIGHT, SDL_SCANCODE_RIGHT, 0) && editor.n64_controller_index == 3u);
    CHECK(!press(SDLK_RIGHT, SDL_SCANCODE_RIGHT, 0) && editor.n64_controller_index == 0u);
    CHECK(!press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0));
    CHECK(editor.key_capture_target == KEY_CAPTURE_NONE && editor.key_selected == 0u);
    editor.key_selected = 1; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    for (unsigned i = 0; i < 18; i++) press(SDLK_z, SDL_SCANCODE_A + i, 0);
    CHECK(strcmp(keys.n64_p1, "A,B,C,D,E,F,H,G,I,J,K,L,M,N,O,P,Q,R") == 0);
    CHECK(editor.key_capture_target == KEY_CAPTURE_NONE);
    CHECK(integral_config_load_keys(path, &restored) == 0 && strcmp(restored.n64_p1, keys.n64_p1) == 0);
    editor.n64_controller_index = 1u;
    editor.key_selected = 1; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    for (unsigned i = 0; i < 18; i++) press(SDLK_z, SDL_SCANCODE_F1 + i, 0);
    CHECK(keys.n64_p2[0] != '\0');
    CHECK(strcmp(keys.n64_p1, "A,B,C,D,E,F,H,G,I,J,K,L,M,N,O,P,Q,R") == 0);
    CHECK(integral_config_load_keys(path, &restored) == 0 && strcmp(restored.n64_p2, keys.n64_p2) == 0);
    editor.key_selected = 2; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    CHECK(keys.n64_p2[0] == '\0');
    CHECK(integral_config_load_keys(path, &restored) == 0 && restored.n64_p2[0] == '\0');
    editor.page = KEY_CONFIG_PAGE_UTIL;
    editor.key_selected = 0; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    press(SDLK_b, SDL_SCANCODE_B, 0);
    CHECK(editor.key_capture_step == 0 && !strcmp(status, "KEY ALREADY ASSIGNED"));
    press(SDLK_e, SDL_SCANCODE_E, 0);
    CHECK(editor.key_capture_step == 0 && !strcmp(status, "KEY ALREADY ASSIGNED"));
    for (unsigned i = 0; i < 5; i++) press(SDLK_F1 + (int)i, SDL_SCANCODE_F1 + i, 0);
    CHECK(strcmp(keys.fast, "F1") == 0 && strcmp(keys.reset, "F5") == 0);
    editor.key_selected = 1; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    CHECK(editor.key_capture_target == KEY_CAPTURE_CLIENT_ALIAS);
    press(SDLK_LEFT, SDL_SCANCODE_LEFT, 0);
    CHECK(editor.key_capture_step == 0 && !strcmp(status, "RESERVED CLIENT KEY"));
    press(SDLK_F2, SDL_SCANCODE_F2, 0);
    CHECK(editor.key_capture_step == 0 && !strcmp(status, "RESERVED CLIENT KEY"));
    press(SDLK_m, SDL_SCANCODE_M, 0);
    CHECK(editor.key_capture_step == 1 && !strcmp(keys.client_alias_right, "M"));
    press(SDLK_m, SDL_SCANCODE_M, 0);
    CHECK(editor.key_capture_step == 1 && !strcmp(status, "KEY ALREADY ASSIGNED"));
    press(SDLK_n, SDL_SCANCODE_N, 0);
    press(SDLK_q, SDL_SCANCODE_Q, 0);
    press(SDLK_y, SDL_SCANCODE_Y, 0);
    press(SDLK_u, SDL_SCANCODE_U, 0);
    press(SDLK_v, SDL_SCANCODE_V, 0);
    CHECK(editor.key_capture_target == KEY_CAPTURE_NONE);
    CHECK(!strcmp(keys.client_alias_escape, "V"));
    SDL_KeyboardEvent raw = {0}, translated = {0};
    raw.type = SDL_KEYDOWN; raw.keysym.sym = SDLK_m; raw.keysym.scancode = SDL_SCANCODE_M;
    CHECK(integral_client_alias_keyboard_event(&keys, &raw, &translated));
    CHECK(translated.keysym.sym == SDLK_RIGHT);
    IntegralConfigKeys defaults; integral_keys_defaults(&defaults);
    editor.key_selected = 2; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    CHECK(strcmp(keys.fast, defaults.fast) == 0 && strcmp(keys.reset, defaults.reset) == 0);
    CHECK(keys.client_alias_right[0] == '\0' && keys.client_alias_escape[0] == '\0');
    CHECK(strcmp(keys.slot1, "E,F,G,H,A,B,C,D") == 0);
    CHECK(strcmp(keys.n64_p1, "A,B,C,D,E,F,H,G,I,J,K,L,M,N,O,P,Q,R") == 0);
    editor.page = KEY_CONFIG_PAGE_GB;
    editor.key_selected = 2; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    CHECK(strcmp(keys.slot1, defaults.slot1) == 0 && strcmp(keys.slot2, defaults.slot2) == 0);
    CHECK(strcmp(keys.n64_p1, "A,B,C,D,E,F,H,G,I,J,K,L,M,N,O,P,Q,R") == 0);
    editor.page = KEY_CONFIG_PAGE_N64;
    editor.n64_controller_index = 0u;
    editor.key_selected = 2; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    CHECK(strcmp(keys.n64_p1, defaults.n64_p1) == 0);

    int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 16, 1);
    CHECK(device >= 0 && integral_gb_runtime_key_config_open_game_controllers() >= 1);
    SDL_Event event = {0}; event.type = SDL_JOYBUTTONDOWN;
    event.jbutton.which = SDL_JoystickGetDeviceInstanceID(device); event.jbutton.button = 0; event.jbutton.state = SDL_PRESSED;
    editor.page = KEY_CONFIG_PAGE_GB;
    editor.key_selected = 0; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    controller(&event); CHECK(editor.key_capture_step == 1 && editor.key_capture_wait_release);
    controller(&event); CHECK(editor.key_capture_step == 1);
    event.type = SDL_JOYBUTTONUP; event.jbutton.state = SDL_RELEASED;
    controller(&event); CHECK(!editor.key_capture_wait_release && editor.key_capture_step == 1);
    for (unsigned i = 1; i < 8; i++) {
        event.type = SDL_JOYBUTTONDOWN; event.jbutton.button = i; event.jbutton.state = SDL_PRESSED;
        controller(&event);
        event.type = SDL_JOYBUTTONUP; event.jbutton.state = SDL_RELEASED; controller(&event);
    }
    CHECK(editor.key_capture_target == KEY_CAPTURE_NONE && !editor.key_capture_wait_release);
    CHECK(integral_config_load_keys(path, &restored) == 0 && strcmp(restored.slot1, keys.slot1) == 0);
    editor.page = KEY_CONFIG_PAGE_UTIL;
    editor.key_selected = 0; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    event.type = SDL_JOYBUTTONDOWN; event.jbutton.state = SDL_PRESSED; controller(&event);
    CHECK(editor.key_capture_step == 0 && !strcmp(status, "KEY ALREADY ASSIGNED"));
    event.type = SDL_JOYBUTTONUP; event.jbutton.state = SDL_RELEASED; controller(&event);
    event.type = SDL_JOYBUTTONDOWN; event.jbutton.button = 8; event.jbutton.state = SDL_PRESSED; controller(&event);
    CHECK(editor.key_capture_step == 1 && editor.key_capture_wait_release);
    controller(&event); CHECK(editor.key_capture_step == 1);
    press(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE, 0);
    integral_gb_runtime_key_config_close_game_controllers();
    CHECK(SDL_JoystickDetachVirtual(device) == 0);
    snprintf(path, sizeof(path), "%s/keys.conf/blocked.conf", argv[1]);
    editor.page = KEY_CONFIG_PAGE_UTIL;
    editor.key_selected = 2; press(SDLK_RETURN, SDL_SCANCODE_RETURN, 0);
    CHECK(strcmp(status, "KEY CONFIG SAVE FAILED") == 0);
    snprintf(path, sizeof(path), "%s/keys.conf", argv[1]);
    CHECK(remove(path) == 0);
    SDL_Quit();
    puts("key editor test passed");
    return 0;
}

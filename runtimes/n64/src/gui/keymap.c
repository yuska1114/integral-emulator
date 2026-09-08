/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/keymap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { AXIS_CAPTURE_THRESHOLD = 16000, AXIS_RELEASE_THRESHOLD = 8000 };

const char *const integral_n64_runtime_key_binding_labels[INTEGRAL_N64_RUNTIME_KEY_BINDINGS] = {
    "D-PAD RIGHT", "D-PAD LEFT", "D-PAD UP", "D-PAD DOWN",
    "START", "Z TRIGGER", "B BUTTON", "A BUTTON",
    "C RIGHT", "C LEFT", "C UP", "C DOWN",
    "R TRIGGER", "L TRIGGER", "ANALOG RIGHT", "ANALOG LEFT",
    "ANALOG UP", "ANALOG DOWN",
};

const char *const integral_n64_runtime_mupen_button_names[14] = {
    "DPad R", "DPad L", "DPad U", "DPad D", "Start", "Z Trig",
    "B Button", "A Button", "C Button R", "C Button L", "C Button U",
    "C Button D", "R Trig", "L Trig",
};

static const SDL_Scancode default_scancodes[INTEGRAL_N64_RUNTIME_KEY_BINDINGS] = {
    SDL_SCANCODE_D, SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S,
    SDL_SCANCODE_RETURN, SDL_SCANCODE_Z, SDL_SCANCODE_LCTRL,
    SDL_SCANCODE_LSHIFT, SDL_SCANCODE_L, SDL_SCANCODE_J, SDL_SCANCODE_I,
    SDL_SCANCODE_K, SDL_SCANCODE_C, SDL_SCANCODE_X, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN,
};

static const char *jis_physical_scancode_name(SDL_Scancode scancode)
{
    switch (scancode) {
    case SDL_SCANCODE_GRAVE: return "JIS ZENKAKU";
    case SDL_SCANCODE_MINUS: return "JIS MINUS";
    case SDL_SCANCODE_EQUALS: return "JIS CARET";
    case SDL_SCANCODE_LEFTBRACKET: return "JIS AT";
    case SDL_SCANCODE_RIGHTBRACKET: return "JIS BRACKET";
    case SDL_SCANCODE_SEMICOLON: return "JIS RE";
    case SDL_SCANCODE_APOSTROPHE: return "JIS KE";
    case SDL_SCANCODE_NONUSHASH: return "JIS MU";
    case SDL_SCANCODE_NONUSBACKSLASH: return "JIS RO";
    case SDL_SCANCODE_INTERNATIONAL1: return "JIS RO";
    case SDL_SCANCODE_INTERNATIONAL2: return "JIS KANA";
    case SDL_SCANCODE_INTERNATIONAL3: return "JIS YEN";
    case SDL_SCANCODE_INTERNATIONAL4: return "JIS HENKAN";
    case SDL_SCANCODE_INTERNATIONAL5: return "JIS MUHENKAN";
    case SDL_SCANCODE_LANG1: return "JIS KANA MODE";
    case SDL_SCANCODE_LANG2: return "JIS EISU";
    default: return NULL;
    }
}

static int joystick_device(SDL_JoystickID instance)
{
    int count = SDL_NumJoysticks();
    int index;
    for (index = 0; index < count; ++index) {
        if (SDL_JoystickGetDeviceInstanceID(index) == instance) return index;
    }
    return -1;
}

void integral_n64_runtime_keymap_defaults(IntegralN64RuntimeKeymap *map)
{
    int i;
    memset(map, 0, sizeof(*map));
    map->device = -1;
    for (i = 0; i < INTEGRAL_N64_RUNTIME_KEY_BINDINGS; ++i) {
        map->bindings[i].kind = INTEGRAL_N64_RUNTIME_BINDING_SCANCODE;
        map->bindings[i].index = (int)default_scancodes[i];
    }
}

bool integral_n64_runtime_keymap_capture(const SDL_Event *event, IntegralN64RuntimeBinding *binding,
                              int *device)
{
    if (!event || !binding || !device) return false;
    memset(binding, 0, sizeof(*binding));
    if (event->type == SDL_KEYDOWN && !event->key.repeat) {
        binding->kind = INTEGRAL_N64_RUNTIME_BINDING_SCANCODE;
        binding->index = (int)event->key.keysym.scancode;
        *device = -1;
        return binding->index > SDL_SCANCODE_UNKNOWN;
    }
    if (event->type == SDL_JOYBUTTONDOWN) {
        binding->kind = INTEGRAL_N64_RUNTIME_BINDING_BUTTON;
        binding->index = event->jbutton.button;
        *device = joystick_device(event->jbutton.which);
        return *device >= 0;
    }
    if (event->type == SDL_JOYAXISMOTION &&
        abs(event->jaxis.value) >= AXIS_CAPTURE_THRESHOLD) {
        binding->kind = INTEGRAL_N64_RUNTIME_BINDING_AXIS;
        binding->index = event->jaxis.axis;
        binding->direction = event->jaxis.value < 0 ? -1 : 1;
        *device = joystick_device(event->jaxis.which);
        return *device >= 0;
    }
    if (event->type == SDL_JOYHATMOTION && event->jhat.value != SDL_HAT_CENTERED) {
        binding->kind = INTEGRAL_N64_RUNTIME_BINDING_HAT;
        binding->index = event->jhat.hat;
        if (event->jhat.value & SDL_HAT_UP) binding->direction = SDL_HAT_UP;
        else if (event->jhat.value & SDL_HAT_DOWN) binding->direction = SDL_HAT_DOWN;
        else if (event->jhat.value & SDL_HAT_LEFT) binding->direction = SDL_HAT_LEFT;
        else binding->direction = SDL_HAT_RIGHT;
        *device = joystick_device(event->jhat.which);
        return *device >= 0;
    }
    return false;
}

bool integral_n64_runtime_keymap_released(const SDL_Event *event,
                               const IntegralN64RuntimeBinding *binding)
{
    if (!event || !binding) return false;
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE)
        return event->type == SDL_KEYUP &&
               (int)event->key.keysym.scancode == binding->index;
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON)
        return event->type == SDL_JOYBUTTONUP &&
               event->jbutton.button == binding->index;
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS)
        return event->type == SDL_JOYAXISMOTION &&
               event->jaxis.axis == binding->index &&
               abs(event->jaxis.value) <= AXIS_RELEASE_THRESHOLD;
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_HAT)
        return event->type == SDL_JOYHATMOTION &&
               event->jhat.hat == binding->index &&
               event->jhat.value == SDL_HAT_CENTERED;
    return true;
}

static const char *hat_name(int direction)
{
    if (direction == SDL_HAT_UP) return "UP";
    if (direction == SDL_HAT_DOWN) return "DOWN";
    if (direction == SDL_HAT_LEFT) return "LEFT";
    return "RIGHT";
}

void integral_n64_runtime_binding_name(const IntegralN64RuntimeBinding *binding, char *output,
                            size_t capacity)
{
    const char *name;
    if (!output || capacity == 0u) return;
    if (!binding) {
        output[0] = '\0';
        return;
    }
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE) {
        SDL_Scancode scancode = (SDL_Scancode)binding->index;
        const char *jis_name = jis_physical_scancode_name(scancode);
        if (jis_name) snprintf(output, capacity, "%s", jis_name);
        else {
            name = SDL_GetScancodeName(scancode);
            if (name && name[0] && strcmp(name, "?") != 0)
                snprintf(output, capacity, "%s", name);
            else
                snprintf(output, capacity, "SCANCODE %d", binding->index);
        }
    }
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON)
        snprintf(output, capacity, "JOY BUTTON %d", binding->index);
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS)
        snprintf(output, capacity, "JOY AXIS %d %c", binding->index,
                 binding->direction < 0 ? '-' : '+');
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_HAT)
        snprintf(output, capacity, "JOY HAT %d %s", binding->index,
                 hat_name(binding->direction));
    else snprintf(output, capacity, "UNASSIGNED");
}

bool integral_n64_runtime_keymap_parse(const char *text, IntegralN64RuntimeKeymap *map)
{
    const char *cursor = text;
    char *end;
    int i;
    long device;
    if (!text || !map) return false;
    device = strtol(cursor, &end, 10);
    if (end == cursor || *end != '|') return false;
    map->device = (int)device;
    cursor = end + 1;
    for (i = 0; i < INTEGRAL_N64_RUNTIME_KEY_BINDINGS; ++i) {
        long kind = strtol(cursor, &end, 10);
        long index;
        long direction;
        if (end == cursor || *end != ':') return false;
        cursor = end + 1;
        index = strtol(cursor, &end, 10);
        if (end == cursor || *end != ':') return false;
        cursor = end + 1;
        direction = strtol(cursor, &end, 10);
        if (end == cursor || (i + 1 < INTEGRAL_N64_RUNTIME_KEY_BINDINGS ? *end != ',' : *end != '\0'))
            return false;
        if (kind < INTEGRAL_N64_RUNTIME_BINDING_NONE || kind > INTEGRAL_N64_RUNTIME_BINDING_HAT ||
            index < 0 || index > 65535 || direction < -128 || direction > 128)
            return false;
        map->bindings[i].kind = (IntegralN64RuntimeBindingKind)kind;
        map->bindings[i].index = (int)index;
        map->bindings[i].direction = (int)direction;
        cursor = end + (i + 1 < INTEGRAL_N64_RUNTIME_KEY_BINDINGS ? 1 : 0);
    }
    return true;
}

void integral_n64_runtime_keymap_format(const IntegralN64RuntimeKeymap *map, char *output,
                             size_t capacity)
{
    size_t used;
    int i;
    if (!map || !output || capacity == 0u) return;
    used = (size_t)snprintf(output, capacity, "%d|", map->device);
    if (used >= capacity) return;
    for (i = 0; i < INTEGRAL_N64_RUNTIME_KEY_BINDINGS; ++i) {
        int length = snprintf(output + used, capacity - used, "%s%d:%d:%d",
                              i ? "," : "", (int)map->bindings[i].kind,
                              map->bindings[i].index, map->bindings[i].direction);
        if (length < 0 || (size_t)length >= capacity - used) {
            output[capacity - 1u] = '\0';
            return;
        }
        used += (size_t)length;
    }
}

void integral_n64_runtime_keymap_mupen_button(const IntegralN64RuntimeBinding *binding,
                                   char *output, size_t capacity)
{
    if (!binding || !output || capacity == 0u) return;
    if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE)
        snprintf(output, capacity, "scancode(%d)", binding->index);
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON)
        snprintf(output, capacity, "button(%d)", binding->index);
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS)
        snprintf(output, capacity, "axis(%d%c)", binding->index,
                 binding->direction < 0 ? '-' : '+');
    else if (binding->kind == INTEGRAL_N64_RUNTIME_BINDING_HAT)
        snprintf(output, capacity, "hat(%d %s)", binding->index,
                 hat_name(binding->direction));
    else output[0] = '\0';
}

void integral_n64_runtime_keymap_mupen_axis(const IntegralN64RuntimeBinding *negative,
                                 const IntegralN64RuntimeBinding *positive,
                                 char *output, size_t capacity)
{
    size_t used = 0u;
    int length;
    if (!negative || !positive || !output || capacity == 0u) return;
    output[0] = '\0';
#define APPEND(...) do { \
    length = snprintf(output + used, capacity - used, __VA_ARGS__); \
    if (length < 0 || (size_t)length >= capacity - used) return; \
    used += (size_t)length; \
} while (0)
    if (negative->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE ||
        positive->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE)
        APPEND("scancode(%d,%d) ",
               negative->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE ? negative->index : -1,
               positive->kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE ? positive->index : -1);
    if (negative->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON ||
        positive->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON)
        APPEND("button(%d,%d) ",
               negative->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON ? negative->index : -1,
               positive->kind == INTEGRAL_N64_RUNTIME_BINDING_BUTTON ? positive->index : -1);
    if (negative->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS ||
        positive->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS)
        APPEND("axis(%d%c,%d%c) ",
               negative->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS ? negative->index : -1,
               negative->direction < 0 ? '-' : '+',
               positive->kind == INTEGRAL_N64_RUNTIME_BINDING_AXIS ? positive->index : -1,
               positive->direction < 0 ? '-' : '+');
    if (negative->kind == INTEGRAL_N64_RUNTIME_BINDING_HAT &&
        positive->kind == INTEGRAL_N64_RUNTIME_BINDING_HAT &&
        negative->index == positive->index)
        APPEND("hat(%d %s %s) ", negative->index,
               hat_name(negative->direction), hat_name(positive->direction));
    if (used > 0u) output[used - 1u] = '\0';
#undef APPEND
}

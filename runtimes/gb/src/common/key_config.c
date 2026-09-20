/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "key_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"
#include "string_util.h"

#define CONTROLLER_BUTTON_BASE (-100000)
#define CONTROLLER_AXIS_POS_BASE (-101000)
#define CONTROLLER_AXIS_NEG_BASE (-102000)
#define JOYSTICK_BUTTON_BASE (-103000)
#define JOYSTICK_AXIS_POS_BASE (-104000)
#define JOYSTICK_AXIS_NEG_BASE (-105000)
#define JOYSTICK_HAT_BASE (-106000)
#define SCOPED_BINDING_BASE (-2000000)
#define SCOPED_BINDING_STRIDE 10000
#define SCOPED_CONTROLLER_BUTTON 1
#define SCOPED_CONTROLLER_AXIS_POS 2
#define SCOPED_CONTROLLER_AXIS_NEG 3
#define SCOPED_JOYSTICK_BUTTON 4
#define SCOPED_JOYSTICK_AXIS_POS 5
#define SCOPED_JOYSTICK_AXIS_NEG 6
#define SCOPED_JOYSTICK_HAT 7
#define INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES 16
#define INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES 64
#define INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_BUTTONS 128
#define INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES 32
#define INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_HATS 16
#define INTEGRAL_GB_RUNTIME_DEVICE_ID_MAX 40

typedef struct OpenInputDevice {
    SDL_GameController *controller;
    SDL_Joystick *joystick;
    SDL_JoystickID instance_id;
    int device_index;
    uint64_t fingerprint;
    unsigned ordinal;
    char stable_id[INTEGRAL_GB_RUNTIME_DEVICE_ID_MAX];
} OpenInputDevice;

typedef struct BindingDevice {
    bool used;
    char stable_id[INTEGRAL_GB_RUNTIME_DEVICE_ID_MAX];
} BindingDevice;

static OpenInputDevice open_devices[INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES];
static BindingDevice binding_devices[INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES];

static uint64_t hash_text(uint64_t hash, const char *text)
{
    const unsigned char *cursor = (const unsigned char *)(text ? text : "");
    while (*cursor) {
        hash ^= *cursor++;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t joystick_fingerprint(SDL_Joystick *joystick)
{
    char guid[33];
    char numeric[96];
    SDL_JoystickGUID value = SDL_JoystickGetGUID(joystick);
    SDL_JoystickGetGUIDString(value, guid, sizeof(guid));
    snprintf(numeric,
             sizeof(numeric),
             "%04x:%04x:%04x:%s",
             SDL_JoystickGetVendor(joystick),
             SDL_JoystickGetProduct(joystick),
             SDL_JoystickGetProductVersion(joystick),
             SDL_JoystickGetSerial(joystick) ? SDL_JoystickGetSerial(joystick) : "");
    uint64_t hash = hash_text(hash_text(UINT64_C(1469598103934665603), guid), numeric);
    hash = hash_text(hash, SDL_JoystickName(joystick));
#if SDL_VERSION_ATLEAST(2, 24, 0)
    hash = hash_text(hash, SDL_JoystickPath(joystick));
#endif
    return hash;
}

static OpenInputDevice *open_device_for_instance(SDL_JoystickID instance_id)
{
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES; i++) {
        if (open_devices[i].joystick && open_devices[i].instance_id == instance_id) {
            return &open_devices[i];
        }
    }
    return NULL;
}

static OpenInputDevice *open_device_for_stable_id(const char *stable_id)
{
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES; i++) {
        if (open_devices[i].joystick && strcmp(open_devices[i].stable_id, stable_id) == 0) {
            return &open_devices[i];
        }
    }
    return NULL;
}

static int binding_device_slot(const char *stable_id, bool create)
{
    for (int i = 0; i < INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES; i++) {
        if (binding_devices[i].used && strcmp(binding_devices[i].stable_id, stable_id) == 0) {
            return i;
        }
    }
    if (!create) {
        return -1;
    }
    for (int i = 0; i < INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES; i++) {
        if (!binding_devices[i].used) {
            binding_devices[i].used = true;
            snprintf(binding_devices[i].stable_id, sizeof(binding_devices[i].stable_id), "%s", stable_id);
            return i;
        }
    }
    return -1;
}

static SDL_Keycode scoped_code(int device_slot, int kind, int control)
{
    if (device_slot < 0 || device_slot >= INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES ||
        kind < SCOPED_CONTROLLER_BUTTON || kind > SCOPED_JOYSTICK_HAT ||
        control < 0 || control >= 1000) {
        return SDLK_UNKNOWN;
    }
    return (SDL_Keycode)(SCOPED_BINDING_BASE - device_slot * SCOPED_BINDING_STRIDE - kind * 1000 - control);
}

static bool scoped_decode(SDL_Keycode code, int *device_slot, int *kind, int *control)
{
    if (code > SCOPED_BINDING_BASE ||
        code <= SCOPED_BINDING_BASE - INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES * SCOPED_BINDING_STRIDE) {
        return false;
    }
    int packed = SCOPED_BINDING_BASE - code;
    int slot = packed / SCOPED_BINDING_STRIDE;
    int rest = packed % SCOPED_BINDING_STRIDE;
    int decoded_kind = rest / 1000;
    int decoded_control = rest % 1000;
    if (slot < 0 || slot >= INTEGRAL_GB_RUNTIME_MAX_BINDING_DEVICES ||
        !binding_devices[slot].used || decoded_kind < SCOPED_CONTROLLER_BUTTON ||
        decoded_kind > SCOPED_JOYSTICK_HAT) {
        return false;
    }
    if (device_slot) *device_slot = slot;
    if (kind) *kind = decoded_kind;
    if (control) *control = decoded_control;
    return true;
}

static const char *controller_button_name(SDL_GameControllerButton button)
{
    switch (button) {
        case SDL_CONTROLLER_BUTTON_A:
            return "PAD_A";
        case SDL_CONTROLLER_BUTTON_B:
            return "PAD_B";
        case SDL_CONTROLLER_BUTTON_X:
            return "PAD_X";
        case SDL_CONTROLLER_BUTTON_Y:
            return "PAD_Y";
        case SDL_CONTROLLER_BUTTON_BACK:
            return "PAD_BACK";
        case SDL_CONTROLLER_BUTTON_GUIDE:
            return "PAD_GUIDE";
        case SDL_CONTROLLER_BUTTON_START:
            return "PAD_START";
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            return "PAD_LSTICK";
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            return "PAD_RSTICK";
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            return "PAD_LSHOULDER";
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            return "PAD_RSHOULDER";
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            return "PAD_DPAD_UP";
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            return "PAD_DPAD_DOWN";
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            return "PAD_DPAD_LEFT";
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            return "PAD_DPAD_RIGHT";
        default:
            return NULL;
    }
}

static const char *controller_axis_name(SDL_GameControllerAxis axis)
{
    switch (axis) {
        case SDL_CONTROLLER_AXIS_LEFTX:
            return "LX";
        case SDL_CONTROLLER_AXIS_LEFTY:
            return "LY";
        case SDL_CONTROLLER_AXIS_RIGHTX:
            return "RX";
        case SDL_CONTROLLER_AXIS_RIGHTY:
            return "RY";
        case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
            return "LT";
        case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
            return "RT";
        default:
            return NULL;
    }
}

SDL_Keycode integral_gb_runtime_key_config_code_from_controller_button(SDL_GameControllerButton button)
{
    if (button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX || !controller_button_name(button)) {
        return SDLK_UNKNOWN;
    }
    return (SDL_Keycode)(CONTROLLER_BUTTON_BASE - button);
}

SDL_Keycode integral_gb_runtime_key_config_code_from_controller_axis(SDL_GameControllerAxis axis, int direction)
{
    if (axis < 0 || axis >= SDL_CONTROLLER_AXIS_MAX || !controller_axis_name(axis) || direction == 0) {
        return SDLK_UNKNOWN;
    }
    return (SDL_Keycode)((direction > 0 ? CONTROLLER_AXIS_POS_BASE : CONTROLLER_AXIS_NEG_BASE) - axis);
}

bool integral_gb_runtime_key_config_is_controller_code(SDL_Keycode key)
{
    return scoped_decode(key, NULL, NULL, NULL) ||
           (key <= CONTROLLER_BUTTON_BASE && key > CONTROLLER_BUTTON_BASE - SDL_CONTROLLER_BUTTON_MAX) ||
           (key <= CONTROLLER_AXIS_POS_BASE && key > CONTROLLER_AXIS_POS_BASE - SDL_CONTROLLER_AXIS_MAX) ||
           (key <= CONTROLLER_AXIS_NEG_BASE && key > CONTROLLER_AXIS_NEG_BASE - SDL_CONTROLLER_AXIS_MAX) ||
           (key <= JOYSTICK_BUTTON_BASE && key > JOYSTICK_BUTTON_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_BUTTONS) ||
           (key <= JOYSTICK_AXIS_POS_BASE && key > JOYSTICK_AXIS_POS_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES) ||
           (key <= JOYSTICK_AXIS_NEG_BASE && key > JOYSTICK_AXIS_NEG_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES) ||
           (key <= JOYSTICK_HAT_BASE &&
            key > JOYSTICK_HAT_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_HATS * 4);
}

static bool joystick_instance_open(SDL_JoystickID instance_id)
{
    return open_device_for_instance(instance_id) != NULL;
}

static OpenInputDevice *unused_open_device(void)
{
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES; i++) {
        if (!open_devices[i].joystick) return &open_devices[i];
    }
    return NULL;
}

static void remember_device(SDL_GameController *controller, SDL_Joystick *joystick, int device_index)
{
    OpenInputDevice *entry = unused_open_device();
    if (!entry || !joystick) return;
    memset(entry, 0, sizeof(*entry));
    entry->controller = controller;
    entry->joystick = joystick;
    entry->instance_id = SDL_JoystickInstanceID(joystick);
    entry->device_index = device_index;
    entry->fingerprint = joystick_fingerprint(joystick);
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES; i++) {
        if (open_devices[i].joystick && &open_devices[i] != entry &&
            open_devices[i].fingerprint == entry->fingerprint &&
            open_devices[i].ordinal >= entry->ordinal) {
            entry->ordinal = open_devices[i].ordinal + 1u;
        }
    }
    snprintf(entry->stable_id,
             sizeof(entry->stable_id),
             "%016llx-%u",
             (unsigned long long)entry->fingerprint,
             entry->ordinal);
}

int integral_gb_runtime_key_config_open_game_controllers(void)
{
    int opened = 0;
    SDL_GameControllerEventState(SDL_ENABLE);
    SDL_JoystickEventState(SDL_ENABLE);
    int joystick_count = SDL_NumJoysticks();
    for (int i = 0; i < joystick_count; i++) {
        SDL_JoystickID instance_id = SDL_JoystickGetDeviceInstanceID(i);
        if (instance_id >= 0 && joystick_instance_open(instance_id)) {
            continue;
        }
        if (SDL_IsGameController(i)) {
            SDL_GameController *controller = SDL_GameControllerOpen(i);
            if (controller) {
                remember_device(controller, SDL_GameControllerGetJoystick(controller), i);
                opened++;
            }
            continue;
        }
        SDL_Joystick *joystick = SDL_JoystickOpen(i);
        if (joystick) {
            remember_device(NULL, joystick, i);
            opened++;
        }
    }
    return opened;
}

void integral_gb_runtime_key_config_close_game_controllers(void)
{
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES; i++) {
        if (open_devices[i].controller) {
            SDL_GameControllerClose(open_devices[i].controller);
        }
        else if (open_devices[i].joystick) {
            SDL_JoystickClose(open_devices[i].joystick);
        }
        memset(&open_devices[i], 0, sizeof(open_devices[i]));
    }
}

void integral_gb_runtime_key_config_handle_device_event(const SDL_Event *event)
{
    if (!event) return;
    if (event->type == SDL_CONTROLLERDEVICEADDED || event->type == SDL_JOYDEVICEADDED) {
        (void)integral_gb_runtime_key_config_open_game_controllers();
        return;
    }
    if (event->type == SDL_CONTROLLERDEVICEREMOVED || event->type == SDL_JOYDEVICEREMOVED) {
        OpenInputDevice *entry = open_device_for_instance(event->jdevice.which);
        if (!entry) return;
        if (entry->controller) SDL_GameControllerClose(entry->controller);
        else if (entry->joystick) SDL_JoystickClose(entry->joystick);
        memset(entry, 0, sizeof(*entry));
    }
}

static bool controller_code_to_button(SDL_Keycode key, SDL_GameControllerButton *button)
{
    if (key <= CONTROLLER_BUTTON_BASE && key > CONTROLLER_BUTTON_BASE - SDL_CONTROLLER_BUTTON_MAX) {
        *button = (SDL_GameControllerButton)(CONTROLLER_BUTTON_BASE - key);
        return controller_button_name(*button) != NULL;
    }
    return false;
}

static bool controller_code_to_axis(SDL_Keycode key, SDL_GameControllerAxis *axis, int *direction)
{
    if (key <= CONTROLLER_AXIS_POS_BASE && key > CONTROLLER_AXIS_POS_BASE - SDL_CONTROLLER_AXIS_MAX) {
        *axis = (SDL_GameControllerAxis)(CONTROLLER_AXIS_POS_BASE - key);
        *direction = 1;
        return controller_axis_name(*axis) != NULL;
    }
    if (key <= CONTROLLER_AXIS_NEG_BASE && key > CONTROLLER_AXIS_NEG_BASE - SDL_CONTROLLER_AXIS_MAX) {
        *axis = (SDL_GameControllerAxis)(CONTROLLER_AXIS_NEG_BASE - key);
        *direction = -1;
        return controller_axis_name(*axis) != NULL;
    }
    return false;
}

static bool joystick_code_to_button(SDL_Keycode key, int *button)
{
    if (key <= JOYSTICK_BUTTON_BASE && key > JOYSTICK_BUTTON_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_BUTTONS) {
        *button = JOYSTICK_BUTTON_BASE - key;
        return true;
    }
    return false;
}

static bool joystick_code_to_axis(SDL_Keycode key, int *axis, int *direction)
{
    if (key <= JOYSTICK_AXIS_POS_BASE && key > JOYSTICK_AXIS_POS_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES) {
        *axis = JOYSTICK_AXIS_POS_BASE - key;
        *direction = 1;
        return true;
    }
    if (key <= JOYSTICK_AXIS_NEG_BASE && key > JOYSTICK_AXIS_NEG_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES) {
        *axis = JOYSTICK_AXIS_NEG_BASE - key;
        *direction = -1;
        return true;
    }
    return false;
}

static int hat_direction_index(int direction)
{
    if (direction == SDL_HAT_UP) return 0;
    if (direction == SDL_HAT_RIGHT) return 1;
    if (direction == SDL_HAT_DOWN) return 2;
    if (direction == SDL_HAT_LEFT) return 3;
    return -1;
}

static int hat_direction_from_index(int index)
{
    static const int directions[] = {SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT};
    return index >= 0 && index < 4 ? directions[index] : SDL_HAT_CENTERED;
}

static bool joystick_code_to_hat(SDL_Keycode key, int *hat, int *direction)
{
    if (key <= JOYSTICK_HAT_BASE &&
        key > JOYSTICK_HAT_BASE - INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_HATS * 4) {
        int packed = JOYSTICK_HAT_BASE - key;
        *hat = packed / 4;
        *direction = hat_direction_from_index(packed % 4);
        return *direction != SDL_HAT_CENTERED;
    }
    return false;
}

static SDL_Keycode joystick_button_code_from_index(int button)
{
    if (button < 0 || button >= INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_BUTTONS) {
        return SDLK_UNKNOWN;
    }
    return (SDL_Keycode)(JOYSTICK_BUTTON_BASE - button);
}

static SDL_Keycode joystick_axis_code_from_index(int axis, int direction)
{
    if (axis < 0 || axis >= INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES || direction == 0) {
        return SDLK_UNKNOWN;
    }
    return (SDL_Keycode)((direction > 0 ? JOYSTICK_AXIS_POS_BASE : JOYSTICK_AXIS_NEG_BASE) - axis);
}

SDL_Keycode integral_gb_runtime_key_config_code_from_joystick_button(int button)
{
    return joystick_button_code_from_index(button);
}

SDL_Keycode integral_gb_runtime_key_config_code_from_joystick_axis(int axis, int direction)
{
    return joystick_axis_code_from_index(axis, direction);
}

SDL_Keycode integral_gb_runtime_key_config_code_from_joystick_hat(int hat, int direction)
{
    int direction_index = hat_direction_index(direction);
    if (hat < 0 || hat >= INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_HATS || direction_index < 0) {
        return SDLK_UNKNOWN;
    }
    return (SDL_Keycode)(JOYSTICK_HAT_BASE - hat * 4 - direction_index);
}

static bool parse_nonnegative_int(const char *text, int *out)
{
    char *end = NULL;
    long value = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || value < 0 || value > 100000L) {
        return false;
    }
    *out = (int)value;
    return true;
}

static bool format_prefixed_suffixed_name(char *dest,
                                          size_t dest_size,
                                          const char *prefix,
                                          const char *name,
                                          const char *suffix)
{
    size_t prefix_len = strlen(prefix);
    size_t name_len = strlen(name);
    size_t suffix_len = strlen(suffix);
    if (prefix_len + name_len + suffix_len + 1u > dest_size) {
        if (dest_size > 0) {
            dest[0] = '\0';
        }
        return false;
    }
    size_t offset = 0;
    memcpy(dest + offset, prefix, prefix_len);
    offset += prefix_len;
    memcpy(dest + offset, name, name_len);
    offset += name_len;
    memcpy(dest + offset, suffix, suffix_len + 1u);
    return true;
}

static SDL_Keycode scoped_key_from_name(const char *token)
{
    bool controller = strncmp(token, "PAD@", 4) == 0;
    bool joystick = strncmp(token, "JOY@", 4) == 0;
    if (!controller && !joystick) return SDLK_UNKNOWN;
    const char *separator = strchr(token + 4, ':');
    if (!separator || separator == token + 4 || separator[1] == '\0') return SDLK_UNKNOWN;
    char stable_id[INTEGRAL_GB_RUNTIME_DEVICE_ID_MAX];
    size_t stable_id_length = (size_t)(separator - (token + 4));
    if (stable_id_length >= sizeof(stable_id)) return SDLK_UNKNOWN;
    memcpy(stable_id, token + 4, stable_id_length);
    stable_id[stable_id_length] = '\0';
    int slot = binding_device_slot(stable_id, true);
    if (slot < 0) return SDLK_UNKNOWN;
    const char *control = separator + 1;
    if (controller) {
        for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
            const char *name = controller_button_name((SDL_GameControllerButton)i);
            if (name && strncmp(name, "PAD_", 4) == 0 && strcmp(control, name + 4) == 0) {
                return scoped_code(slot, SCOPED_CONTROLLER_BUTTON, i);
            }
        }
        for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
            const char *name = controller_axis_name((SDL_GameControllerAxis)i);
            if (!name) continue;
            char positive[16];
            char negative[16];
            snprintf(positive, sizeof(positive), "%s+", name);
            snprintf(negative, sizeof(negative), "%s-", name);
            if (strcmp(control, positive) == 0) return scoped_code(slot, SCOPED_CONTROLLER_AXIS_POS, i);
            if (strcmp(control, negative) == 0) return scoped_code(slot, SCOPED_CONTROLLER_AXIS_NEG, i);
        }
        return SDLK_UNKNOWN;
    }
    if (control[0] == 'B') {
        int button = 0;
        if (parse_nonnegative_int(control + 1, &button) && button < INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_BUTTONS) {
            return scoped_code(slot, SCOPED_JOYSTICK_BUTTON, button);
        }
    }
    if (control[0] == 'A') {
        size_t length = strlen(control);
        if (length >= 3 && (control[length - 1] == '+' || control[length - 1] == '-')) {
            char number[16];
            if (length - 2 < sizeof(number)) {
                memcpy(number, control + 1, length - 2);
                number[length - 2] = '\0';
                int axis = 0;
                if (parse_nonnegative_int(number, &axis) && axis < INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_AXES) {
                    return scoped_code(slot,
                                       control[length - 1] == '+' ? SCOPED_JOYSTICK_AXIS_POS : SCOPED_JOYSTICK_AXIS_NEG,
                                       axis);
                }
            }
        }
    }
    if (control[0] == 'H') {
        const char *direction_text = NULL;
        int direction = SDL_HAT_CENTERED;
        const char *names[] = {"UP", "RIGHT", "DOWN", "LEFT"};
        const int directions[] = {SDL_HAT_UP, SDL_HAT_RIGHT, SDL_HAT_DOWN, SDL_HAT_LEFT};
        for (size_t i = 0; i < 4; i++) {
            size_t control_length = strlen(control);
            size_t name_length = strlen(names[i]);
            if (control_length > name_length && strcmp(control + control_length - name_length, names[i]) == 0) {
                direction_text = control + control_length - name_length;
                direction = directions[i];
                break;
            }
        }
        if (direction_text) {
            char number[16];
            size_t number_length = (size_t)(direction_text - (control + 1));
            if (number_length > 0 && number_length < sizeof(number)) {
                memcpy(number, control + 1, number_length);
                number[number_length] = '\0';
                int hat = 0;
                int direction_index = hat_direction_index(direction);
                if (parse_nonnegative_int(number, &hat) && hat < INTEGRAL_GB_RUNTIME_MAX_JOYSTICK_HATS) {
                    return scoped_code(slot, SCOPED_JOYSTICK_HAT, hat * 4 + direction_index);
                }
            }
        }
    }
    return SDLK_UNKNOWN;
}

SDL_Keycode integral_gb_runtime_key_config_key_from_name(const char *token)
{
    SDL_Keycode scoped = scoped_key_from_name(token);
    if (scoped != SDLK_UNKNOWN) return scoped;
    for (int i = 0; i < SDL_CONTROLLER_BUTTON_MAX; i++) {
        const char *name = controller_button_name((SDL_GameControllerButton)i);
        if (name && strcmp(token, name) == 0) {
            return integral_gb_runtime_key_config_code_from_controller_button((SDL_GameControllerButton)i);
        }
    }
    for (int i = 0; i < SDL_CONTROLLER_AXIS_MAX; i++) {
        const char *name = controller_axis_name((SDL_GameControllerAxis)i);
        char axis_positive[32];
        char axis_negative[32];
        if (!name) {
            continue;
        }
        if (!format_prefixed_suffixed_name(axis_positive, sizeof(axis_positive), "PAD_", name, "_POS") ||
            !format_prefixed_suffixed_name(axis_negative, sizeof(axis_negative), "PAD_", name, "_NEG")) {
            continue;
        }
        if (strcmp(token, axis_positive) == 0) {
            return integral_gb_runtime_key_config_code_from_controller_axis((SDL_GameControllerAxis)i, 1);
        }
        if (strcmp(token, axis_negative) == 0) {
            return integral_gb_runtime_key_config_code_from_controller_axis((SDL_GameControllerAxis)i, -1);
        }
    }
    if (strncmp(token, "JOY_BUTTON_", 11) == 0) {
        int button = 0;
        if (parse_nonnegative_int(token + 11, &button)) {
            return joystick_button_code_from_index(button);
        }
    }
    if (strncmp(token, "JOY_AXIS_", 9) == 0) {
        const char *axis_text = token + 9;
        char *suffix = strstr(axis_text, "_POS");
        int direction = 1;
        if (!suffix) {
            suffix = strstr(axis_text, "_NEG");
            direction = -1;
        }
        if (suffix && suffix[4] == '\0') {
            char number[16];
            size_t number_len = (size_t)(suffix - axis_text);
            if (number_len > 0 && number_len < sizeof(number)) {
                memcpy(number, axis_text, number_len);
                number[number_len] = '\0';
                int axis = 0;
                if (parse_nonnegative_int(number, &axis)) {
                    return joystick_axis_code_from_index(axis, direction);
                }
            }
        }
    }
    if (strncmp(token, "JOY_HAT_", 8) == 0) {
        const char *hat_text = token + 8;
        const char *separator = strchr(hat_text, '_');
        if (separator) {
            char number[16];
            size_t number_length = (size_t)(separator - hat_text);
            if (number_length > 0 && number_length < sizeof(number)) {
                memcpy(number, hat_text, number_length);
                number[number_length] = '\0';
                int hat = 0;
                int direction = strcmp(separator + 1, "UP") == 0 ? SDL_HAT_UP :
                                strcmp(separator + 1, "RIGHT") == 0 ? SDL_HAT_RIGHT :
                                strcmp(separator + 1, "DOWN") == 0 ? SDL_HAT_DOWN :
                                strcmp(separator + 1, "LEFT") == 0 ? SDL_HAT_LEFT : SDL_HAT_CENTERED;
                if (parse_nonnegative_int(number, &hat) && direction != SDL_HAT_CENTERED) {
                    return integral_gb_runtime_key_config_code_from_joystick_hat(hat, direction);
                }
            }
        }
    }
    if (strcmp(token, "RSHIFT") == 0 || strcmp(token, "RIGHTSHIFT") == 0) {
        return SDLK_RSHIFT;
    }
    if (strcmp(token, "RETURN") == 0 || strcmp(token, "ENTER") == 0) {
        return SDLK_RETURN;
    }
    if (strcmp(token, "UP") == 0) {
        return SDLK_UP;
    }
    if (strcmp(token, "DOWN") == 0) {
        return SDLK_DOWN;
    }
    if (strcmp(token, "LEFT") == 0) {
        return SDLK_LEFT;
    }
    if (strcmp(token, "RIGHT") == 0) {
        return SDLK_RIGHT;
    }
    return SDL_GetKeyFromName(token);
}

const char *integral_gb_runtime_key_config_key_name(SDL_Keycode key)
{
    static char controller_names[8][96];
    static unsigned controller_name_index = 0;
    SDL_GameControllerButton button;
    SDL_GameControllerAxis axis;
    int joystick_index = 0;
    int direction = 0;
    int device_slot = -1;
    int scoped_kind = 0;
    int scoped_control = 0;
    if (scoped_decode(key, &device_slot, &scoped_kind, &scoped_control)) {
        char *controller_name = controller_names[controller_name_index++ % 8];
        const char *device_id = binding_devices[device_slot].stable_id;
        if (scoped_kind == SCOPED_CONTROLLER_BUTTON) {
            const char *name = controller_button_name((SDL_GameControllerButton)scoped_control);
            snprintf(controller_name, sizeof(controller_names[0]), "PAD@%s:%s", device_id,
                     name && strncmp(name, "PAD_", 4) == 0 ? name + 4 : "UNKNOWN");
        }
        else if (scoped_kind == SCOPED_CONTROLLER_AXIS_POS || scoped_kind == SCOPED_CONTROLLER_AXIS_NEG) {
            const char *name = controller_axis_name((SDL_GameControllerAxis)scoped_control);
            snprintf(controller_name, sizeof(controller_names[0]), "PAD@%s:%s%c", device_id,
                     name ? name : "AXIS",
                     scoped_kind == SCOPED_CONTROLLER_AXIS_POS ? '+' : '-');
        }
        else if (scoped_kind == SCOPED_JOYSTICK_BUTTON) {
            snprintf(controller_name, sizeof(controller_names[0]), "JOY@%s:B%d", device_id, scoped_control);
        }
        else if (scoped_kind == SCOPED_JOYSTICK_AXIS_POS || scoped_kind == SCOPED_JOYSTICK_AXIS_NEG) {
            snprintf(controller_name, sizeof(controller_names[0]), "JOY@%s:A%d%c", device_id,
                     scoped_control,
                     scoped_kind == SCOPED_JOYSTICK_AXIS_POS ? '+' : '-');
        }
        else {
            int hat = scoped_control / 4;
            int hat_direction = hat_direction_from_index(scoped_control % 4);
            const char *hat_name = hat_direction == SDL_HAT_UP ? "UP" :
                                   hat_direction == SDL_HAT_RIGHT ? "RIGHT" :
                                   hat_direction == SDL_HAT_DOWN ? "DOWN" : "LEFT";
            snprintf(controller_name, sizeof(controller_names[0]), "JOY@%s:H%d%s", device_id, hat, hat_name);
        }
        return controller_name;
    }
    if (controller_code_to_button(key, &button)) {
        const char *name = controller_button_name(button);
        return name ? name : "UNKNOWN";
    }
    if (controller_code_to_axis(key, &axis, &direction)) {
        const char *name = controller_axis_name(axis);
        char *controller_name = controller_names[controller_name_index++ % 8];
        (void)format_prefixed_suffixed_name(controller_name,
                                            sizeof(controller_names[0]),
                                            "PAD_",
                                            name ? name : "AXIS",
                                            direction > 0 ? "_POS" : "_NEG");
        return controller_name;
    }
    if (joystick_code_to_button(key, &joystick_index)) {
        char *controller_name = controller_names[controller_name_index++ % 8];
        snprintf(controller_name, sizeof(controller_names[0]), "JOY_BUTTON_%d", joystick_index);
        return controller_name;
    }
    if (joystick_code_to_axis(key, &joystick_index, &direction)) {
        char *controller_name = controller_names[controller_name_index++ % 8];
        snprintf(controller_name,
                 sizeof(controller_names[0]),
                 "JOY_AXIS_%d_%s",
                 joystick_index,
                 direction > 0 ? "POS" : "NEG");
        return controller_name;
    }
    int hat = 0;
    if (joystick_code_to_hat(key, &hat, &direction)) {
        char *controller_name = controller_names[controller_name_index++ % 8];
        const char *hat_name = direction == SDL_HAT_UP ? "UP" :
                               direction == SDL_HAT_RIGHT ? "RIGHT" :
                               direction == SDL_HAT_DOWN ? "DOWN" : "LEFT";
        snprintf(controller_name, sizeof(controller_names[0]), "JOY_HAT_%d_%s", hat, hat_name);
        return controller_name;
    }
    if (key == SDLK_RSHIFT) {
        return "RSHIFT";
    }
    if (key == SDLK_RETURN) {
        return "RETURN";
    }
    return SDL_GetKeyName(key);
}

SDL_Keycode integral_gb_runtime_key_config_fast_default(void)
{
    return SDLK_f;
}

SDL_Keycode integral_gb_runtime_key_config_screenshot_default(void)
{
    return SDLK_p;
}

SDL_Keycode integral_gb_runtime_key_config_escape_default(void)
{
    return SDLK_ESCAPE;
}

SDL_Keycode integral_gb_runtime_key_config_turbo_hold_default(void)
{
    return SDLK_b;
}

SDL_Keycode integral_gb_runtime_key_config_reset_default(void)
{
    return SDLK_o;
}

void integral_gb_runtime_key_config_slot1_default(IntegralGBRuntimeKeyConfig *config)
{
    config->right = SDLK_RIGHT;
    config->left = SDLK_LEFT;
    config->up = SDLK_UP;
    config->down = SDLK_DOWN;
    config->a = SDLK_z;
    config->b = SDLK_x;
    config->select = SDLK_RSHIFT;
    config->start = SDLK_RETURN;
}

void integral_gb_runtime_key_config_slot2_default(IntegralGBRuntimeKeyConfig *config)
{
    config->right = SDLK_d;
    config->left = SDLK_a;
    config->up = SDLK_w;
    config->down = SDLK_s;
    config->a = SDLK_g;
    config->b = SDLK_h;
    config->select = SDLK_r;
    config->start = SDLK_t;
}

int integral_gb_runtime_key_config_parse(IntegralGBRuntimeKeyConfig *config, const char *spec)
{
    char copy[1536];
    if (!integral_gb_runtime_copy_text(copy, sizeof(copy), spec)) {
        return -1;
    }

    SDL_Keycode keys[8];
    unsigned count = 0;
    char *saveptr = NULL;
    for (char *token = strtok_r(copy, ",", &saveptr);
         token;
         token = strtok_r(NULL, ",", &saveptr)) {
        while (*token == ' ') {
            token++;
        }
        if (count >= 8) {
            return -1;
        }
        keys[count] = integral_gb_runtime_key_config_key_from_name(token);
        if (keys[count] == SDLK_UNKNOWN) {
            return -1;
        }
        count++;
    }
    if (count != 8) {
        return -1;
    }

    config->right = keys[0];
    config->left = keys[1];
    config->up = keys[2];
    config->down = keys[3];
    config->a = keys[4];
    config->b = keys[5];
    config->select = keys[6];
    config->start = keys[7];
    return 0;
}

void integral_gb_runtime_key_config_describe(const IntegralGBRuntimeKeyConfig *config, char *out, size_t out_size)
{
    char names[8][96];
    const SDL_Keycode keys[] = {
        config->right,
        config->left,
        config->up,
        config->down,
        config->a,
        config->b,
        config->select,
        config->start,
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        const char *name = integral_gb_runtime_key_config_key_name(keys[i]);
        (void)integral_gb_runtime_copy_text(names[i], sizeof(names[i]), name ? name : "UNKNOWN");
    }
    snprintf(out,
             out_size,
             "%s,%s,%s,%s,%s,%s,%s,%s",
             names[0],
             names[1],
             names[2],
             names[3],
             names[4],
             names[5],
             names[6],
             names[7]);
}

uint8_t integral_gb_runtime_key_config_button_for_key(const IntegralGBRuntimeKeyConfig *config, SDL_Keycode key)
{
    if (key == config->right) {
        return INTEGRAL_GB_RUNTIME_BTN_RIGHT;
    }
    if (key == config->left) {
        return INTEGRAL_GB_RUNTIME_BTN_LEFT;
    }
    if (key == config->up) {
        return INTEGRAL_GB_RUNTIME_BTN_UP;
    }
    if (key == config->down) {
        return INTEGRAL_GB_RUNTIME_BTN_DOWN;
    }
    if (key == config->a) {
        return INTEGRAL_GB_RUNTIME_BTN_A;
    }
    if (key == config->b) {
        return INTEGRAL_GB_RUNTIME_BTN_B;
    }
    if (key == config->select) {
        return INTEGRAL_GB_RUNTIME_BTN_SELECT;
    }
    if (key == config->start) {
        return INTEGRAL_GB_RUNTIME_BTN_START;
    }
    return 0;
}

static SDL_Keycode scoped_event_code(SDL_JoystickID instance_id, int kind, int control)
{
    OpenInputDevice *device = open_device_for_instance(instance_id);
    if (!device) return SDLK_UNKNOWN;
    int slot = binding_device_slot(device->stable_id, true);
    return scoped_code(slot, kind, control);
}

SDL_Keycode integral_gb_runtime_key_config_code_from_event(const SDL_Event *event)
{
    if (!event) return SDLK_UNKNOWN;
    if (event->type == SDL_KEYDOWN && !event->key.repeat) return event->key.keysym.sym;
    /*
     * Capture physical Joystick controls, not GameController's semantic A/B/X/Y
     * names. Every SDL GameController owns a Joystick and emits these raw
     * events; persisting the raw control makes the binding independent of face
     * button conventions and is exactly what Mupen's manual map consumes.
     */
    if (event->type == SDL_JOYBUTTONDOWN) {
        return scoped_event_code(event->jbutton.which, SCOPED_JOYSTICK_BUTTON, event->jbutton.button);
    }
    if (event->type == SDL_JOYAXISMOTION &&
        abs(event->jaxis.value) >= INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD) {
        return scoped_event_code(event->jaxis.which,
                                 event->jaxis.value > 0 ? SCOPED_JOYSTICK_AXIS_POS : SCOPED_JOYSTICK_AXIS_NEG,
                                 event->jaxis.axis);
    }
    if (event->type == SDL_JOYHATMOTION && event->jhat.value != SDL_HAT_CENTERED) {
        int direction = (event->jhat.value & SDL_HAT_UP) ? SDL_HAT_UP :
                        (event->jhat.value & SDL_HAT_RIGHT) ? SDL_HAT_RIGHT :
                        (event->jhat.value & SDL_HAT_DOWN) ? SDL_HAT_DOWN : SDL_HAT_LEFT;
        return scoped_event_code(event->jhat.which,
                                 SCOPED_JOYSTICK_HAT,
                                 event->jhat.hat * 4 + hat_direction_index(direction));
    }
    return SDLK_UNKNOWN;
}

static bool event_matches_scoped_device(int device_slot, SDL_JoystickID instance_id)
{
    OpenInputDevice *device = open_device_for_instance(instance_id);
    return device && strcmp(device->stable_id, binding_devices[device_slot].stable_id) == 0;
}

bool integral_gb_runtime_key_config_binding_rising(SDL_Keycode binding,
                                                  const SDL_Event *event, bool *held)
{
    bool pressed = false;
    if (!held || !integral_gb_runtime_key_config_binding_matches_event(binding, event, &pressed)) return false;
    bool rising = pressed && !*held;
    *held = pressed;
    return rising;
}

bool integral_gb_runtime_key_config_binding_matches_event(SDL_Keycode binding,
                                                const SDL_Event *event,
                                                bool *pressed)
{
    if (!event) {
        return false;
    }
    int device_slot = -1;
    int scoped_kind = 0;
    int scoped_control = 0;
    bool scoped = scoped_decode(binding, &device_slot, &scoped_kind, &scoped_control);
    if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) && !integral_gb_runtime_key_config_is_controller_code(binding)) {
        if (event->key.repeat) {
            return false;
        }
        if (event->key.keysym.sym != binding) {
            return false;
        }
        if (pressed) {
            *pressed = event->type == SDL_KEYDOWN;
        }
        return true;
    }
    if (scoped && event->type == SDL_CONTROLLERBUTTONDOWN) {
        if (scoped_kind != SCOPED_CONTROLLER_BUTTON ||
            event->cbutton.button != (uint8_t)scoped_control ||
            !event_matches_scoped_device(device_slot, event->cbutton.which)) return false;
        if (pressed) *pressed = true;
        return true;
    }
    if (scoped && event->type == SDL_CONTROLLERBUTTONUP) {
        if (scoped_kind != SCOPED_CONTROLLER_BUTTON ||
            event->cbutton.button != (uint8_t)scoped_control ||
            !event_matches_scoped_device(device_slot, event->cbutton.which)) return false;
        if (pressed) *pressed = false;
        return true;
    }
    if (scoped && event->type == SDL_CONTROLLERAXISMOTION) {
        if ((scoped_kind != SCOPED_CONTROLLER_AXIS_POS && scoped_kind != SCOPED_CONTROLLER_AXIS_NEG) ||
            event->caxis.axis != (uint8_t)scoped_control ||
            !event_matches_scoped_device(device_slot, event->caxis.which)) return false;
        if (pressed) *pressed = scoped_kind == SCOPED_CONTROLLER_AXIS_POS
                                    ? event->caxis.value > INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD
                                    : event->caxis.value < -INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD;
        return true;
    }
    if (scoped && (event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP)) {
        if (scoped_kind != SCOPED_JOYSTICK_BUTTON || event->jbutton.button != (uint8_t)scoped_control ||
            !event_matches_scoped_device(device_slot, event->jbutton.which)) return false;
        if (pressed) *pressed = event->type == SDL_JOYBUTTONDOWN;
        return true;
    }
    if (scoped && event->type == SDL_JOYAXISMOTION) {
        if ((scoped_kind != SCOPED_JOYSTICK_AXIS_POS && scoped_kind != SCOPED_JOYSTICK_AXIS_NEG) ||
            event->jaxis.axis != (uint8_t)scoped_control ||
            !event_matches_scoped_device(device_slot, event->jaxis.which)) return false;
        if (pressed) *pressed = scoped_kind == SCOPED_JOYSTICK_AXIS_POS
                                    ? event->jaxis.value > INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD
                                    : event->jaxis.value < -INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD;
        return true;
    }
    if (scoped && event->type == SDL_JOYHATMOTION) {
        int hat = scoped_control / 4;
        int direction = hat_direction_from_index(scoped_control % 4);
        if (scoped_kind != SCOPED_JOYSTICK_HAT || event->jhat.hat != (uint8_t)hat ||
            !event_matches_scoped_device(device_slot, event->jhat.which)) return false;
        if (pressed) *pressed = (event->jhat.value & direction) != 0;
        return true;
    }
    if (event->type == SDL_CONTROLLERBUTTONDOWN || event->type == SDL_CONTROLLERBUTTONUP) {
        SDL_GameControllerButton button;
        if (!controller_code_to_button(binding, &button) ||
            event->cbutton.button != (uint8_t)button) {
            return false;
        }
        if (pressed) {
            *pressed = event->type == SDL_CONTROLLERBUTTONDOWN;
        }
        return true;
    }
    if (event->type == SDL_CONTROLLERAXISMOTION) {
        SDL_GameControllerAxis axis;
        int direction = 0;
        if (!controller_code_to_axis(binding, &axis, &direction) ||
            event->caxis.axis != (uint8_t)axis) {
            return false;
        }
        int value = event->caxis.value;
        if (pressed) {
            *pressed = direction > 0 ? value > INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD
                                     : value < -INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD;
        }
        return true;
    }
    if (event->type == SDL_JOYBUTTONDOWN || event->type == SDL_JOYBUTTONUP) {
        int button = 0;
        if (!joystick_code_to_button(binding, &button) ||
            event->jbutton.button != (uint8_t)button) {
            return false;
        }
        if (pressed) {
            *pressed = event->type == SDL_JOYBUTTONDOWN;
        }
        return true;
    }
    if (event->type == SDL_JOYAXISMOTION) {
        int axis = 0;
        int direction = 0;
        if (!joystick_code_to_axis(binding, &axis, &direction) ||
            event->jaxis.axis != (uint8_t)axis) {
            return false;
        }
        int value = event->jaxis.value;
        if (pressed) {
            *pressed = direction > 0 ? value > INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD
                                     : value < -INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD;
        }
        return true;
    }
    if (event->type == SDL_JOYHATMOTION) {
        int hat = 0;
        int direction = 0;
        if (!joystick_code_to_hat(binding, &hat, &direction) || event->jhat.hat != (uint8_t)hat) {
            return false;
        }
        if (pressed) *pressed = (event->jhat.value & direction) != 0;
        return true;
    }
    return false;
}

static int current_device_index(SDL_JoystickID instance_id)
{
    int count = SDL_NumJoysticks();
    for (int i = 0; i < count; i++) {
        if (SDL_JoystickGetDeviceInstanceID(i) == instance_id) return i;
    }
    return -1;
}

static OpenInputDevice *device_for_binding(int device_slot, bool controller_required)
{
    if (device_slot >= 0) {
        OpenInputDevice *device = open_device_for_stable_id(binding_devices[device_slot].stable_id);
        if (device && (!controller_required || device->controller)) return device;
        return NULL;
    }
    for (size_t i = 0; i < INTEGRAL_GB_RUNTIME_MAX_OPEN_DEVICES; i++) {
        if (open_devices[i].joystick && (!controller_required || open_devices[i].controller)) {
            return &open_devices[i];
        }
    }
    return NULL;
}

bool integral_gb_runtime_key_config_binding_pressed(SDL_Keycode binding)
{
    int device_slot = -1;
    int kind = 0;
    int control = 0;
    bool scoped = scoped_decode(binding, &device_slot, &kind, &control);
    SDL_GameControllerButton controller_button;
    SDL_GameControllerAxis controller_axis;
    int index = 0;
    int direction = 0;
    if (!integral_gb_runtime_key_config_is_controller_code(binding)) {
        SDL_Scancode scancode = SDL_GetScancodeFromKey(binding);
        const Uint8 *keyboard = SDL_GetKeyboardState(NULL);
        return scancode > SDL_SCANCODE_UNKNOWN && scancode < SDL_NUM_SCANCODES && keyboard[scancode] != 0;
    }
    if (!scoped) {
        if (controller_code_to_button(binding, &controller_button)) {
            kind = SCOPED_CONTROLLER_BUTTON;
            control = controller_button;
        }
        else if (controller_code_to_axis(binding, &controller_axis, &direction)) {
            kind = direction > 0 ? SCOPED_CONTROLLER_AXIS_POS : SCOPED_CONTROLLER_AXIS_NEG;
            control = controller_axis;
        }
        else if (joystick_code_to_button(binding, &index)) {
            kind = SCOPED_JOYSTICK_BUTTON;
            control = index;
        }
        else if (joystick_code_to_axis(binding, &index, &direction)) {
            kind = direction > 0 ? SCOPED_JOYSTICK_AXIS_POS : SCOPED_JOYSTICK_AXIS_NEG;
            control = index;
        }
        else if (joystick_code_to_hat(binding, &index, &direction)) {
            kind = SCOPED_JOYSTICK_HAT;
            control = index * 4 + hat_direction_index(direction);
        }
    }
    bool controller_required = kind >= SCOPED_CONTROLLER_BUTTON && kind <= SCOPED_CONTROLLER_AXIS_NEG;
    OpenInputDevice *device = device_for_binding(device_slot, controller_required);
    if (!device) return false;
    if (kind == SCOPED_CONTROLLER_BUTTON) {
        return SDL_GameControllerGetButton(device->controller, (SDL_GameControllerButton)control) != 0;
    }
    if (kind == SCOPED_CONTROLLER_AXIS_POS || kind == SCOPED_CONTROLLER_AXIS_NEG) {
        Sint16 value = SDL_GameControllerGetAxis(device->controller, (SDL_GameControllerAxis)control);
        return kind == SCOPED_CONTROLLER_AXIS_POS ? value > INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD
                                                   : value < -INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD;
    }
    if (kind == SCOPED_JOYSTICK_BUTTON) return SDL_JoystickGetButton(device->joystick, control) != 0;
    if (kind == SCOPED_JOYSTICK_AXIS_POS || kind == SCOPED_JOYSTICK_AXIS_NEG) {
        Sint16 value = SDL_JoystickGetAxis(device->joystick, control);
        return kind == SCOPED_JOYSTICK_AXIS_POS ? value > INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD
                                                 : value < -INTEGRAL_GB_RUNTIME_CONTROLLER_AXIS_THRESHOLD;
    }
    if (kind == SCOPED_JOYSTICK_HAT) {
        int hat = control / 4;
        int hat_direction = hat_direction_from_index(control % 4);
        return (SDL_JoystickGetHat(device->joystick, hat) & hat_direction) != 0;
    }
    return false;
}

static bool controller_bind_to_n64(OpenInputDevice *device,
                                   SDL_GameControllerButton button,
                                   SDL_GameControllerAxis axis,
                                   bool is_axis,
                                   int configured_direction,
                                   int *kind,
                                   int *index,
                                   int *direction)
{
    SDL_GameControllerButtonBind bind = is_axis
        ? SDL_GameControllerGetBindForAxis(device->controller, axis)
        : SDL_GameControllerGetBindForButton(device->controller, button);
    if (bind.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) {
        *kind = 2;
        *index = bind.value.button;
        *direction = 0;
        return true;
    }
    if (bind.bindType == SDL_CONTROLLER_BINDTYPE_AXIS) {
        *kind = 3;
        *index = bind.value.axis;
        *direction = is_axis ? configured_direction : 1;
        return true;
    }
    if (bind.bindType == SDL_CONTROLLER_BINDTYPE_HAT) {
        *kind = 4;
        *index = bind.value.hat.hat;
        *direction = bind.value.hat.hat_mask;
        return true;
    }
    return false;
}

bool integral_gb_runtime_key_config_binding_to_n64(SDL_Keycode binding,
                                                   int *device_index,
                                                   int *kind,
                                                   int *index,
                                                   int *direction)
{
    if (!device_index || !kind || !index || !direction) return false;
    int device_slot = -1;
    int scoped_kind = 0;
    int control = 0;
    bool scoped = scoped_decode(binding, &device_slot, &scoped_kind, &control);
    if (!integral_gb_runtime_key_config_is_controller_code(binding)) {
        SDL_Scancode scancode = SDL_GetScancodeFromKey(binding);
        if (scancode <= SDL_SCANCODE_UNKNOWN) return false;
        *device_index = -1;
        *kind = 1;
        *index = scancode;
        *direction = 0;
        return true;
    }
    SDL_GameControllerButton controller_button;
    SDL_GameControllerAxis controller_axis;
    int raw_index = 0;
    int raw_direction = 0;
    if (!scoped) {
        if (controller_code_to_button(binding, &controller_button)) {
            scoped_kind = SCOPED_CONTROLLER_BUTTON;
            control = controller_button;
        }
        else if (controller_code_to_axis(binding, &controller_axis, &raw_direction)) {
            scoped_kind = raw_direction > 0 ? SCOPED_CONTROLLER_AXIS_POS : SCOPED_CONTROLLER_AXIS_NEG;
            control = controller_axis;
        }
        else if (joystick_code_to_button(binding, &raw_index)) {
            scoped_kind = SCOPED_JOYSTICK_BUTTON;
            control = raw_index;
        }
        else if (joystick_code_to_axis(binding, &raw_index, &raw_direction)) {
            scoped_kind = raw_direction > 0 ? SCOPED_JOYSTICK_AXIS_POS : SCOPED_JOYSTICK_AXIS_NEG;
            control = raw_index;
        }
        else if (joystick_code_to_hat(binding, &raw_index, &raw_direction)) {
            scoped_kind = SCOPED_JOYSTICK_HAT;
            control = raw_index * 4 + hat_direction_index(raw_direction);
        }
    }
    bool controller_required = scoped_kind >= SCOPED_CONTROLLER_BUTTON && scoped_kind <= SCOPED_CONTROLLER_AXIS_NEG;
    OpenInputDevice *device = device_for_binding(device_slot, controller_required);
    if (!device) return false;
    *device_index = current_device_index(device->instance_id);
    if (*device_index < 0) return false;
    if (scoped_kind == SCOPED_CONTROLLER_BUTTON) {
        return controller_bind_to_n64(device, (SDL_GameControllerButton)control,
                                      SDL_CONTROLLER_AXIS_INVALID, false, 0, kind, index, direction);
    }
    if (scoped_kind == SCOPED_CONTROLLER_AXIS_POS || scoped_kind == SCOPED_CONTROLLER_AXIS_NEG) {
        return controller_bind_to_n64(device, SDL_CONTROLLER_BUTTON_INVALID,
                                      (SDL_GameControllerAxis)control, true,
                                      scoped_kind == SCOPED_CONTROLLER_AXIS_POS ? 1 : -1,
                                      kind, index, direction);
    }
    if (scoped_kind == SCOPED_JOYSTICK_BUTTON) {
        *kind = 2; *index = control; *direction = 0; return true;
    }
    if (scoped_kind == SCOPED_JOYSTICK_AXIS_POS || scoped_kind == SCOPED_JOYSTICK_AXIS_NEG) {
        *kind = 3; *index = control;
        *direction = scoped_kind == SCOPED_JOYSTICK_AXIS_POS ? 1 : -1; return true;
    }
    if (scoped_kind == SCOPED_JOYSTICK_HAT) {
        *kind = 4; *index = control / 4; *direction = hat_direction_from_index(control % 4); return true;
    }
    return false;
}

uint8_t integral_gb_runtime_key_config_button_for_event(const IntegralGBRuntimeKeyConfig *config,
                                              const SDL_Event *event,
                                              bool *pressed)
{
    uint8_t press_mask = 0;
    uint8_t release_mask = 0;
    integral_gb_runtime_key_config_buttons_for_event(config, event, &press_mask, &release_mask);
    if (press_mask != 0) {
        if (pressed) {
            *pressed = true;
        }
        return press_mask;
    }
    if (release_mask != 0) {
        if (pressed) {
            *pressed = false;
        }
        return release_mask;
    }
    return 0;
}

void integral_gb_runtime_key_config_buttons_for_event(const IntegralGBRuntimeKeyConfig *config,
                                            const SDL_Event *event,
                                            uint8_t *press_mask,
                                            uint8_t *release_mask)
{
    struct Binding {
        SDL_Keycode binding;
        uint8_t button;
    } bindings[] = {
        {config->right, INTEGRAL_GB_RUNTIME_BTN_RIGHT},
        {config->left, INTEGRAL_GB_RUNTIME_BTN_LEFT},
        {config->up, INTEGRAL_GB_RUNTIME_BTN_UP},
        {config->down, INTEGRAL_GB_RUNTIME_BTN_DOWN},
        {config->a, INTEGRAL_GB_RUNTIME_BTN_A},
        {config->b, INTEGRAL_GB_RUNTIME_BTN_B},
        {config->select, INTEGRAL_GB_RUNTIME_BTN_SELECT},
        {config->start, INTEGRAL_GB_RUNTIME_BTN_START},
    };

    uint8_t pressed_buttons = 0;
    uint8_t released_buttons = 0;
    for (size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); i++) {
        bool pressed = false;
        if (integral_gb_runtime_key_config_binding_matches_event(bindings[i].binding, event, &pressed)) {
            if (pressed) {
                pressed_buttons |= bindings[i].button;
            }
            else {
                released_buttons |= bindings[i].button;
            }
        }
    }
    if (press_mask) {
        *press_mask = pressed_buttons;
    }
    if (release_mask) {
        *release_mask = released_buttons;
    }
}

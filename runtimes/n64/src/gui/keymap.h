/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_GUI_KEYMAP_H
#define INTEGRAL_N64_RUNTIME_GUI_KEYMAP_H

#include <stdbool.h>
#include <stddef.h>

#include <SDL.h>

enum { INTEGRAL_N64_RUNTIME_KEY_BINDINGS = 18, INTEGRAL_N64_RUNTIME_KEYMAP_TEXT_MAX = 512 };

typedef enum IntegralN64RuntimeBindingKind {
    INTEGRAL_N64_RUNTIME_BINDING_NONE,
    INTEGRAL_N64_RUNTIME_BINDING_SCANCODE,
    INTEGRAL_N64_RUNTIME_BINDING_BUTTON,
    INTEGRAL_N64_RUNTIME_BINDING_AXIS,
    INTEGRAL_N64_RUNTIME_BINDING_HAT,
} IntegralN64RuntimeBindingKind;

typedef struct IntegralN64RuntimeBinding {
    IntegralN64RuntimeBindingKind kind;
    int index;
    int direction;
} IntegralN64RuntimeBinding;

typedef struct IntegralN64RuntimeKeymap {
    IntegralN64RuntimeBinding bindings[INTEGRAL_N64_RUNTIME_KEY_BINDINGS];
    int device;
} IntegralN64RuntimeKeymap;

extern const char *const integral_n64_runtime_key_binding_labels[INTEGRAL_N64_RUNTIME_KEY_BINDINGS];
extern const char *const integral_n64_runtime_mupen_button_names[14];

void integral_n64_runtime_keymap_defaults(IntegralN64RuntimeKeymap *map);
bool integral_n64_runtime_keymap_capture(const SDL_Event *event, IntegralN64RuntimeBinding *binding,
                              int *device);
bool integral_n64_runtime_keymap_released(const SDL_Event *event,
                               const IntegralN64RuntimeBinding *binding);
void integral_n64_runtime_binding_name(const IntegralN64RuntimeBinding *binding, char *output,
                            size_t capacity);
bool integral_n64_runtime_keymap_parse(const char *text, IntegralN64RuntimeKeymap *map);
void integral_n64_runtime_keymap_format(const IntegralN64RuntimeKeymap *map, char *output,
                             size_t capacity);
void integral_n64_runtime_keymap_mupen_button(const IntegralN64RuntimeBinding *binding,
                                   char *output, size_t capacity);
void integral_n64_runtime_keymap_mupen_axis(const IntegralN64RuntimeBinding *negative,
                                 const IntegralN64RuntimeBinding *positive,
                                 char *output, size_t capacity);

#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_KEY_CONFIG_H
#define INTEGRAL_CLIENT_KEY_CONFIG_H
#include "client_config.h"
#include <SDL.h>
#include <stdbool.h>

#define INTEGRAL_GB_KEY_ROWS 4
#define INTEGRAL_N64_KEY_ROWS 4
#define INTEGRAL_UTIL_KEY_ROWS 3
#define INTEGRAL_KEY_BUTTONS 8
#define INTEGRAL_N64_RUNTIME_KEY_BUTTONS 18
#define INTEGRAL_UTIL_KEYS 5

typedef enum KeyCaptureTarget {
    KEY_CAPTURE_NONE,
    KEY_CAPTURE_SLOT1,
    KEY_CAPTURE_SLOT2,
    KEY_CAPTURE_N64,
    KEY_CAPTURE_UTILS,
} KeyCaptureTarget;
typedef enum KeyConfigPage {
    KEY_CONFIG_PAGE_GB,
    KEY_CONFIG_PAGE_N64,
    KEY_CONFIG_PAGE_UTIL,
} KeyConfigPage;
typedef struct IntegralClientKeyEditor {
    unsigned key_selected;
    KeyConfigPage page;
    unsigned n64_controller_index;
    KeyCaptureTarget key_capture_target;
    unsigned key_capture_step;
    bool key_capture_wait_release;
    SDL_Keycode key_capture_release_binding;
} IntegralClientKeyEditor;

/* True requests return to the main menu; this module never changes screens. */
bool integral_client_key_editor_keyboard(IntegralClientKeyEditor *, IntegralConfigKeys *,
    const char *config_path, char *status, size_t status_size, const SDL_KeyboardEvent *);
void integral_client_key_editor_controller(IntegralClientKeyEditor *, IntegralConfigKeys *,
    const char *config_path, char *status, size_t status_size, const SDL_Event *);

void key_spec_to_names_count(const char *, char names[][INTEGRAL_CONFIG_KEY_NAME_MAX], unsigned);
void key_spec_to_names(const char *, char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX]);
const char *key_config_step_label(KeyCaptureTarget, unsigned);
unsigned n64_key_spec_index_for_capture_step(unsigned);
void format_slot_key_summary(const char *, char *, size_t, char *, size_t);
void format_util_key_summary(const IntegralConfigKeys *, char *, size_t, char *, size_t, char *, size_t);
void format_n64_key_summary(const char *, char *, size_t, char *, size_t, char *, size_t);
#endif

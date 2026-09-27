/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_key_config.h"
#include "client_input_alias.h"
#include "../runtimes/gb/src/common/key_config.h"
#include <stdio.h>
#include <string.h>

typedef struct KeyEditContext {
    IntegralClientKeyEditor *editor;
    IntegralConfigKeys *keys;
    const char *config_path;
    char *status;
    size_t status_size;
} KeyEditContext;

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}


static void append_text(char *dest, size_t dest_size, const char *src)
{
    size_t len = strlen(dest);
    if (len >= dest_size) {
        return;
    }
    copy_text(dest + len, dest_size - len, src);
}


static void key_name_from_sdl(SDL_Keycode key, char *out, size_t out_size)
{
    if (key == SDLK_RSHIFT) {
        copy_text(out, out_size, "RSHIFT");
        return;
    }
    if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
        copy_text(out, out_size, "RETURN");
        return;
    }
    const char *name = SDL_GetKeyName(key);
    copy_text(out, out_size, name && name[0] ? name : "UNKNOWN");
}

static void scancode_name_from_sdl(SDL_Scancode scancode, char *out, size_t out_size)
{
    const char *name = SDL_GetScancodeName(scancode);
    if (name && name[0] && strcmp(name, "?") != 0) {
        copy_text(out, out_size, name);
        return;
    }
    snprintf(out, out_size, "SCANCODE %d", (int)scancode);
}


void key_spec_to_names_count(const char *spec,
                                    char names[][INTEGRAL_CONFIG_KEY_NAME_MAX],
                                    unsigned max_names)
{
    for (unsigned i = 0; i < max_names; i++) {
        names[i][0] = '\0';
    }
    char copy[INTEGRAL_CONFIG_KEY_SPEC_MAX];
    copy_text(copy, sizeof(copy), spec);
    unsigned count = 0;
    char *saveptr = NULL;
    for (char *token = strtok_r(copy, ",", &saveptr);
         token && count < max_names;
         token = strtok_r(NULL, ",", &saveptr)) {
        while (*token == ' ') {
            token++;
        }
        copy_text(names[count++], INTEGRAL_CONFIG_KEY_NAME_MAX, token);
    }
}

void key_spec_to_names(const char *spec, char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX])
{
    key_spec_to_names_count(spec, names, INTEGRAL_KEY_BUTTONS);
}

void format_slot_key_summary(const char *spec,
                                    char *line1,
                                    size_t line1_size,
                                    char *line2,
                                    size_t line2_size)
{
    char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    key_spec_to_names(spec, names);
    snprintf(line1,
             line1_size,
             "RIGHT=[%s],LEFT=[%s],UP=[%s],DOWN=[%s]",
             names[0][0] ? names[0] : "UNKNOWN",
             names[1][0] ? names[1] : "UNKNOWN",
             names[2][0] ? names[2] : "UNKNOWN",
             names[3][0] ? names[3] : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "A=[%s],B=[%s],SELECT=[%s],START=[%s]",
             names[4][0] ? names[4] : "UNKNOWN",
             names[5][0] ? names[5] : "UNKNOWN",
             names[6][0] ? names[6] : "UNKNOWN",
             names[7][0] ? names[7] : "UNKNOWN");
}

void format_util_key_summary(const IntegralConfigKeys *keys,
                                    char *line1,
                                    size_t line1_size,
                                    char *line2,
                                    size_t line2_size,
                                    char *line3,
                                    size_t line3_size)
{
    snprintf(line1,
             line1_size,
             "FAST = [%s],SCREENSHOT = [%s]",
             keys->fast[0] ? keys->fast : "UNKNOWN",
             keys->screenshot[0] ? keys->screenshot : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "ESCAPE = [%s],TURBO = [%s]",
             keys->escape[0] ? keys->escape : "UNKNOWN",
             keys->turbo_hold[0] ? keys->turbo_hold : "UNKNOWN");
    snprintf(line3,
             line3_size,
             "RESET = [%s]",
             keys->reset[0] ? keys->reset : "UNKNOWN");
}

void format_client_alias_summary(const IntegralConfigKeys *keys,
                                        char *line1,
                                        size_t line1_size,
                                        char *line2,
                                        size_t line2_size)
{
    snprintf(line1,
             line1_size,
             "RIGHT=[%s],LEFT=[%s],UP=[%s],DOWN=[%s]",
             keys->client_alias_right[0] ? keys->client_alias_right : "NONE",
             keys->client_alias_left[0] ? keys->client_alias_left : "NONE",
             keys->client_alias_up[0] ? keys->client_alias_up : "NONE",
             keys->client_alias_down[0] ? keys->client_alias_down : "NONE");
    snprintf(line2,
             line2_size,
             "ENTER=[%s],ESCAPE=[%s]",
             keys->client_alias_enter[0] ? keys->client_alias_enter : "NONE",
             keys->client_alias_escape[0] ? keys->client_alias_escape : "NONE");
}

void format_n64_key_summary(const char *spec,
                                   char *line1,
                                   size_t line1_size,
                                   char *line2,
                                   size_t line2_size,
                                   char *line3,
                                   size_t line3_size)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    key_spec_to_names_count(spec, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    snprintf(line1,
             line1_size,
             "D=[%s/%s/%s/%s] START=[%s]",
             names[0][0] ? names[0] : "UNKNOWN",
             names[1][0] ? names[1] : "UNKNOWN",
             names[2][0] ? names[2] : "UNKNOWN",
             names[3][0] ? names[3] : "UNKNOWN",
             names[4][0] ? names[4] : "UNKNOWN");
    snprintf(line2,
             line2_size,
             "Z=[%s],B=[%s],A=[%s],C=[%s/%s/%s/%s]",
             names[5][0] ? names[5] : "UNKNOWN",
             names[6][0] ? names[6] : "UNKNOWN",
             names[7][0] ? names[7] : "UNKNOWN",
             names[8][0] ? names[8] : "UNKNOWN",
             names[9][0] ? names[9] : "UNKNOWN",
             names[10][0] ? names[10] : "UNKNOWN",
             names[11][0] ? names[11] : "UNKNOWN");
    snprintf(line3,
             line3_size,
             "R=[%s],L=[%s],ANALOG=[%s/%s/%s/%s]",
             names[12][0] ? names[12] : "UNKNOWN",
             names[13][0] ? names[13] : "UNKNOWN",
             names[14][0] ? names[14] : "UNKNOWN",
             names[15][0] ? names[15] : "UNKNOWN",
             names[16][0] ? names[16] : "UNKNOWN",
             names[17][0] ? names[17] : "UNKNOWN");
}

static void key_names_to_spec(char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX], char *out, size_t out_size)
{
    out[0] = '\0';
    for (unsigned i = 0; i < INTEGRAL_KEY_BUTTONS; i++) {
        if (i > 0) {
            append_text(out, out_size, ",");
        }
        append_text(out, out_size, names[i][0] ? names[i] : "UNKNOWN");
    }
}

static void key_names_to_spec_count(char names[][INTEGRAL_CONFIG_KEY_NAME_MAX],
                                    unsigned count,
                                    char *out,
                                    size_t out_size)
{
    out[0] = '\0';
    for (unsigned i = 0; i < count; i++) {
        if (i > 0) {
            append_text(out, out_size, ",");
        }
        append_text(out, out_size, names[i][0] ? names[i] : "UNKNOWN");
    }
}

const char *key_config_step_label(KeyCaptureTarget target, unsigned step)
{
    static const char *buttons[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN"};
    static const char *n64_buttons[] = {
        "D-PAD RIGHT", "D-PAD LEFT", "D-PAD UP", "D-PAD DOWN",
        "START", "Z TRIGGER", "A BUTTON", "B BUTTON",
        "C RIGHT", "C LEFT", "C UP", "C DOWN",
        "R TRIGGER", "L TRIGGER", "ANALOG RIGHT", "ANALOG LEFT",
        "ANALOG UP", "ANALOG DOWN",
    };
    static const char *utils[] = {"FAST", "SCREENSHOT", "ESCAPE", "TURBO HOLD", "RESET"};
    static const char *client_aliases[] = {
        "RIGHT ALIAS", "LEFT ALIAS", "UP ALIAS", "DOWN ALIAS", "ENTER ALIAS", "ESCAPE ALIAS"
    };
    if (target == KEY_CAPTURE_CLIENT_ALIAS) {
        return step < INTEGRAL_CLIENT_ALIAS_KEYS ? client_aliases[step] : "DONE";
    }
    if (target == KEY_CAPTURE_UTILS) {
        return step < INTEGRAL_UTIL_KEYS ? utils[step] : "DONE";
    }
    if (target == KEY_CAPTURE_N64) {
        return step < INTEGRAL_N64_RUNTIME_KEY_BUTTONS ? n64_buttons[step] : "DONE";
    }
    return step < INTEGRAL_KEY_BUTTONS ? buttons[step] : "DONE";
}

static unsigned key_spec_index_for_capture_step(unsigned step)
{
    static const unsigned order[] = {4, 5, 6, 7, 0, 1, 2, 3};
    return step < INTEGRAL_KEY_BUTTONS ? order[step] : 0;
}

unsigned n64_key_spec_index_for_capture_step(unsigned step)
{
    if (step == 6u) return 7u; /* present A before B; canonical index 7 is A */
    if (step == 7u) return 6u; /* canonical index 6 is B */
    return step;
}

static void move_key_selection(KeyEditContext *state, int delta)
{
    unsigned row_count = state->editor->page == KEY_CONFIG_PAGE_GB ? INTEGRAL_GB_KEY_ROWS :
        (state->editor->page == KEY_CONFIG_PAGE_N64 ? INTEGRAL_N64_KEY_ROWS : INTEGRAL_UTIL_KEY_ROWS);
    int selected = (int)state->editor->key_selected + delta;
    if (selected < 0) {
        selected = (int)row_count - 1;
    }
    if (selected >= (int)row_count) {
        selected = 0;
    }
    state->editor->key_selected = (unsigned)selected;
}

static char *n64_key_spec_for_controller(IntegralConfigKeys *keys, unsigned controller_index)
{
    switch (controller_index) {
        case 1u:
            return keys->n64_p2;
        case 2u:
            return keys->n64_p3;
        case 3u:
            return keys->n64_p4;
        default:
            return keys->n64_p1;
    }
}

static const char *client_alias_name(const IntegralConfigKeys *keys, unsigned index)
{
    switch (index) {
        case 0u: return keys->client_alias_right;
        case 1u: return keys->client_alias_left;
        case 2u: return keys->client_alias_up;
        case 3u: return keys->client_alias_down;
        case 4u: return keys->client_alias_enter;
        default: return keys->client_alias_escape;
    }
}

static char *client_alias_name_mut(IntegralConfigKeys *keys, unsigned index)
{
    switch (index) {
        case 0u: return keys->client_alias_right;
        case 1u: return keys->client_alias_left;
        case 2u: return keys->client_alias_up;
        case 3u: return keys->client_alias_down;
        case 4u: return keys->client_alias_enter;
        default: return keys->client_alias_escape;
    }
}

static void save_key_config(KeyEditContext *state)
{
    if (integral_config_save_keys(state->config_path, state->keys) == 0) {
        copy_text(state->status, state->status_size, "KEY CONFIG SAVED");
    }
    else {
        copy_text(state->status, state->status_size, "KEY CONFIG SAVE FAILED");
    }
}

static void begin_key_capture(KeyEditContext *state, KeyCaptureTarget target)
{
    state->editor->key_capture_target = target;
    state->editor->key_capture_step = 0;
    state->editor->key_capture_wait_release = false;
    state->editor->key_capture_release_binding = SDLK_UNKNOWN;
    if (target == KEY_CAPTURE_N64) {
        const char *source_spec = n64_key_spec_for_controller(
            state->keys, state->editor->n64_controller_index);
        key_spec_to_names_count(source_spec,
                                state->editor->n64_capture_names,
                                INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    }
    copy_text(state->status, state->status_size, "PRESS KEY OR JOY-CON INPUT");
}

static void finish_key_capture(KeyEditContext *state)
{
    state->editor->key_capture_target = KEY_CAPTURE_NONE;
    state->editor->key_capture_step = 0;
    state->editor->key_capture_wait_release = false;
    state->editor->key_capture_release_binding = SDLK_UNKNOWN;
    save_key_config(state);
}

static bool key_spec_contains_binding(const char *spec, SDL_Keycode binding)
{
    char names[INTEGRAL_N64_RUNTIME_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    key_spec_to_names_count(spec, names, INTEGRAL_N64_RUNTIME_KEY_BUTTONS);
    for (unsigned i = 0; i < INTEGRAL_N64_RUNTIME_KEY_BUTTONS; i++) {
        if (names[i][0] && integral_gb_runtime_key_config_key_from_name(names[i]) == binding)
            return true;
    }
    return false;
}

static void apply_captured_binding(KeyEditContext *state,
                                   SDL_Keycode key,
                                   SDL_Scancode scancode,
                                   const char *controller_name)
{
    char name[INTEGRAL_CONFIG_KEY_NAME_MAX];
    if (controller_name && controller_name[0]) {
        copy_text(name, sizeof(name), controller_name);
    }
    else if (state->editor->key_capture_target == KEY_CAPTURE_N64) {
        scancode_name_from_sdl(scancode, name, sizeof(name));
    }
    else {
        key_name_from_sdl(key, name, sizeof(name));
    }
    if (strcmp(name, "UNKNOWN") == 0) {
        copy_text(state->status, state->status_size, "UNKNOWN KEY");
        return;
    }

    if (state->editor->key_capture_target == KEY_CAPTURE_CLIENT_ALIAS) {
        SDL_Keycode captured = integral_gb_runtime_key_config_key_from_name(name);
        if (captured == SDLK_UNKNOWN) {
            copy_text(state->status, state->status_size, "UNKNOWN KEY");
            return;
        }
        if (integral_client_alias_key_reserved(captured)) {
            copy_text(state->status, state->status_size, "RESERVED CLIENT KEY");
            return;
        }
        for (unsigned i = 0; i < INTEGRAL_CLIENT_ALIAS_KEYS; i++) {
            const char *configured = client_alias_name(state->keys, i);
            if (i != state->editor->key_capture_step && configured[0] &&
                captured == integral_gb_runtime_key_config_key_from_name(configured)) {
                copy_text(state->status, state->status_size, "KEY ALREADY ASSIGNED");
                return;
            }
        }
        copy_text(client_alias_name_mut(state->keys, state->editor->key_capture_step),
                  INTEGRAL_CONFIG_KEY_NAME_MAX, name);
        state->editor->key_capture_step++;
        if (state->editor->key_capture_step >= INTEGRAL_CLIENT_ALIAS_KEYS) {
            finish_key_capture(state);
        }
        else {
            snprintf(state->status,
                     state->status_size,
                     "NEXT %s",
                     key_config_step_label(state->editor->key_capture_target,
                                           state->editor->key_capture_step));
        }
        return;
    }

    if (state->editor->key_capture_target == KEY_CAPTURE_UTILS) {
        const char *configured[] = {state->keys->fast, state->keys->screenshot,
            state->keys->escape, state->keys->turbo_hold, state->keys->reset};
        SDL_Keycode captured = integral_gb_runtime_key_config_key_from_name(name);
        /* FAST/TURBO are enabled only with GB Slot 1. Other utilities also
         * share active input with SERVER2 and every LOCAL N64 controller. */
        bool all_modes = state->editor->key_capture_step != 0 && state->editor->key_capture_step != 3;
        if (captured != SDLK_UNKNOWN &&
            (key_spec_contains_binding(state->keys->slot1, captured) ||
             (all_modes && (key_spec_contains_binding(state->keys->slot2, captured) ||
                           key_spec_contains_binding(state->keys->n64_p1, captured) ||
                           key_spec_contains_binding(state->keys->n64_p2, captured) ||
                           key_spec_contains_binding(state->keys->n64_p3, captured) ||
                           key_spec_contains_binding(state->keys->n64_p4, captured))))) {
            copy_text(state->status, state->status_size, "KEY ALREADY ASSIGNED");
            return;
        }
        for (unsigned i = 0; i < INTEGRAL_UTIL_KEYS; ++i) {
            if (i != state->editor->key_capture_step && captured != SDLK_UNKNOWN &&
                captured == integral_gb_runtime_key_config_key_from_name(configured[i])) {
                copy_text(state->status, state->status_size, "KEY ALREADY ASSIGNED");
                return;
            }
        }
        if (state->editor->key_capture_step == 0) {
            copy_text(state->keys->fast, sizeof(state->keys->fast), name);
        }
        else if (state->editor->key_capture_step == 1) {
            copy_text(state->keys->screenshot, sizeof(state->keys->screenshot), name);
        }
        else if (state->editor->key_capture_step == 2) {
            copy_text(state->keys->escape, sizeof(state->keys->escape), name);
        }
        else if (state->editor->key_capture_step == 3) {
            copy_text(state->keys->turbo_hold, sizeof(state->keys->turbo_hold), name);
        }
        else {
            copy_text(state->keys->reset, sizeof(state->keys->reset), name);
        }
        state->editor->key_capture_step++;
        if (state->editor->key_capture_step >= INTEGRAL_UTIL_KEYS) {
            finish_key_capture(state);
        }
        else {
            snprintf(state->status,
                     state->status_size,
                     "NEXT %s",
                     key_config_step_label(state->editor->key_capture_target, state->editor->key_capture_step));
        }
        return;
    }
    if (state->editor->key_capture_target == KEY_CAPTURE_N64) {
        unsigned index = n64_key_spec_index_for_capture_step(state->editor->key_capture_step);
        copy_text(state->editor->n64_capture_names[index],
                  sizeof(state->editor->n64_capture_names[index]), name);
        state->editor->key_capture_step++;
        if (state->editor->key_capture_step >= INTEGRAL_N64_RUNTIME_KEY_BUTTONS) {
            char *target_spec = n64_key_spec_for_controller(
                state->keys, state->editor->n64_controller_index);
            key_names_to_spec_count(state->editor->n64_capture_names,
                                    INTEGRAL_N64_RUNTIME_KEY_BUTTONS,
                                    target_spec,
                                    INTEGRAL_CONFIG_KEY_SPEC_MAX);
            finish_key_capture(state);
        }
        else {
            snprintf(state->status,
                     state->status_size,
                     "NEXT %s",
                     key_config_step_label(state->editor->key_capture_target, state->editor->key_capture_step));
        }
        return;
    }

    char names[INTEGRAL_KEY_BUTTONS][INTEGRAL_CONFIG_KEY_NAME_MAX];
    char *target_spec = state->editor->key_capture_target == KEY_CAPTURE_SLOT1 ? state->keys->slot1 : state->keys->slot2;
    key_spec_to_names(target_spec, names);
    unsigned index = key_spec_index_for_capture_step(state->editor->key_capture_step);
    copy_text(names[index], sizeof(names[index]), name);
    key_names_to_spec(names, target_spec, INTEGRAL_CONFIG_KEY_SPEC_MAX);
    state->editor->key_capture_step++;
    if (state->editor->key_capture_step >= INTEGRAL_KEY_BUTTONS) {
        finish_key_capture(state);
    }
    else {
        snprintf(state->status,
                 state->status_size,
                 "NEXT %s",
                 key_config_step_label(state->editor->key_capture_target, state->editor->key_capture_step));
    }
}

static void reset_key_defaults(KeyEditContext *state)
{
    IntegralConfigKeys defaults;
    integral_keys_defaults(&defaults);
    if (state->editor->page == KEY_CONFIG_PAGE_GB) {
        copy_text(state->keys->slot1, sizeof(state->keys->slot1), defaults.slot1);
        copy_text(state->keys->slot2, sizeof(state->keys->slot2), defaults.slot2);
    }
    else if (state->editor->page == KEY_CONFIG_PAGE_N64) {
        char *target_spec = n64_key_spec_for_controller(
            state->keys, state->editor->n64_controller_index);
        char *default_spec = n64_key_spec_for_controller(
            &defaults, state->editor->n64_controller_index);
        copy_text(target_spec, INTEGRAL_CONFIG_KEY_SPEC_MAX, default_spec);
    }
    else {
        copy_text(state->keys->fast, sizeof(state->keys->fast), defaults.fast);
        copy_text(state->keys->screenshot, sizeof(state->keys->screenshot), defaults.screenshot);
        copy_text(state->keys->escape, sizeof(state->keys->escape), defaults.escape);
        copy_text(state->keys->turbo_hold, sizeof(state->keys->turbo_hold), defaults.turbo_hold);
        copy_text(state->keys->reset, sizeof(state->keys->reset), defaults.reset);
        state->keys->client_alias_right[0] = '\0';
        state->keys->client_alias_left[0] = '\0';
        state->keys->client_alias_up[0] = '\0';
        state->keys->client_alias_down[0] = '\0';
        state->keys->client_alias_enter[0] = '\0';
        state->keys->client_alias_escape[0] = '\0';
    }
    save_key_config(state);
}

static bool handle_key_config_key_context(KeyEditContext *state, const SDL_KeyboardEvent *key)
{
    if (key->repeat) {
        return false;
    }
    if (state->editor->key_capture_target != KEY_CAPTURE_NONE) {
        bool capturing_util_escape =
            state->editor->key_capture_target == KEY_CAPTURE_UTILS &&
            state->editor->key_capture_step == 2u;
        if (key->keysym.sym == SDLK_ESCAPE && !capturing_util_escape) {
            state->editor->key_capture_target = KEY_CAPTURE_NONE;
            state->editor->key_capture_step = 0;
            state->editor->key_capture_wait_release = false;
            state->editor->key_capture_release_binding = SDLK_UNKNOWN;
            copy_text(state->status, state->status_size, "KEY CONFIG CANCELED");
            return false;
        }
        apply_captured_binding(state, key->keysym.sym, key->keysym.scancode, NULL);
        return false;
    }

    switch (key->keysym.sym) {
        case SDLK_ESCAPE:
            /* Caller owns screen transition. */
            copy_text(state->status, state->status_size, "SETTINGS");
            return true;
        case SDLK_TAB:
        case SDLK_DOWN:
            move_key_selection(state, 1);
            break;
        case SDLK_UP:
            move_key_selection(state, -1);
            break;
        case SDLK_LEFT:
        case SDLK_RIGHT:
            if (state->editor->page == KEY_CONFIG_PAGE_N64 &&
                state->editor->key_selected == 0u) {
                if (key->keysym.sym == SDLK_LEFT) {
                    state->editor->n64_controller_index =
                        state->editor->n64_controller_index == 0u
                            ? 3u : state->editor->n64_controller_index - 1u;
                }
                else {
                    state->editor->n64_controller_index =
                        state->editor->n64_controller_index == 3u
                            ? 0u : state->editor->n64_controller_index + 1u;
                }
            }
            break;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (state->editor->page == KEY_CONFIG_PAGE_GB && state->editor->key_selected == 0) {
                begin_key_capture(state, KEY_CAPTURE_SLOT1);
            }
            else if (state->editor->page == KEY_CONFIG_PAGE_GB && state->editor->key_selected == 1) {
                begin_key_capture(state, KEY_CAPTURE_SLOT2);
            }
            else if (state->editor->page == KEY_CONFIG_PAGE_N64 && state->editor->key_selected == 1u) {
                begin_key_capture(state, KEY_CAPTURE_N64);
            }
            else if (state->editor->page == KEY_CONFIG_PAGE_UTIL && state->editor->key_selected == 0) {
                begin_key_capture(state, KEY_CAPTURE_UTILS);
            }
            else if (state->editor->page == KEY_CONFIG_PAGE_UTIL && state->editor->key_selected == 1u) {
                begin_key_capture(state, KEY_CAPTURE_CLIENT_ALIAS);
            }
            else if (state->editor->page == KEY_CONFIG_PAGE_N64 &&
                     state->editor->key_selected == 0u) {
                /* Controller selection is changed with Left/Right only. */
            }
            else if ((state->editor->page == KEY_CONFIG_PAGE_GB &&
                      state->editor->key_selected == 2u) ||
                     (state->editor->page == KEY_CONFIG_PAGE_N64 &&
                      state->editor->key_selected == 2u) ||
                     (state->editor->page == KEY_CONFIG_PAGE_UTIL &&
                      state->editor->key_selected == 2u)) {
                reset_key_defaults(state);
            }
            else {
                copy_text(state->status, state->status_size, "SETTINGS");
                return true;
            }
            break;
        default:
            break;
    }
    return false;
}

static void handle_key_config_controller_context(KeyEditContext *state, const SDL_Event *event)
{
    if (state->editor->key_capture_target == KEY_CAPTURE_NONE) return;
    if (state->editor->key_capture_wait_release) {
        bool pressed = true;
        if (integral_gb_runtime_key_config_binding_matches_event(
                state->editor->key_capture_release_binding, event, &pressed) && !pressed) {
            state->editor->key_capture_wait_release = false;
            state->editor->key_capture_release_binding = SDLK_UNKNOWN;
        }
        return;
    }
    SDL_Keycode binding = integral_gb_runtime_key_config_code_from_event(event);
    if (binding == SDLK_UNKNOWN) return;
    const char *name = integral_gb_runtime_key_config_key_name(binding);
    if (!name || strcmp(name, "UNKNOWN") == 0) return;
    apply_captured_binding(state, binding, SDL_SCANCODE_UNKNOWN, name);
    if (state->editor->key_capture_target != KEY_CAPTURE_NONE) {
        state->editor->key_capture_wait_release = true;
        state->editor->key_capture_release_binding = binding;
    }
}


bool integral_client_key_editor_keyboard(IntegralClientKeyEditor *editor,
    IntegralConfigKeys *keys, const char *config_path, char *status, size_t status_size,
    const SDL_KeyboardEvent *event)
{
    KeyEditContext context = {editor, keys, config_path, status, status_size};
    return handle_key_config_key_context(&context, event);
}

void integral_client_key_editor_controller(IntegralClientKeyEditor *editor,
    IntegralConfigKeys *keys, const char *config_path, char *status, size_t status_size,
    const SDL_Event *event)
{
    KeyEditContext context = {editor, keys, config_path, status, status_size};
    handle_key_config_controller_context(&context, event);
}

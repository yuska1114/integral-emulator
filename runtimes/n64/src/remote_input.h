/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_REMOTE_INPUT_H
#define INTEGRAL_N64_RUNTIME_REMOTE_INPUT_H

#include <stddef.h>
#include <stdint.h>

#define INTEGRAL_N64_RUNTIME_REMOTE_INPUT_RECORD_SIZE 28u
#define INTEGRAL_N64_RUNTIME_REMOTE_INPUT_MAX_AGE_MS 750u
#define INTEGRAL_N64_RUNTIME_REMOTE_INPUT_BUTTON_MASK 0x3ffffu

void integral_n64_runtime_remote_input_configure(const char *path);
int integral_n64_runtime_remote_input_parse(const unsigned char *record,
                                 size_t record_size,
                                 uint64_t now_ms,
                                 uint64_t *buttons_out);
int integral_n64_runtime_remote_controller_state(uint64_t *buttons_out);

#endif

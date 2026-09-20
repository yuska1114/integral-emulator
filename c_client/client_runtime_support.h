/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_RUNTIME_SUPPORT_H
#define INTEGRAL_CLIENT_RUNTIME_SUPPORT_H
#include <SDL.h>
#include "client_save_sync.h"
#include "client_config.h"
bool make_n64_runtime_util_hotkeys(const IntegralConfigKeys *, bool allow_reset, char *, size_t);

#ifdef _WIN32
IntegralChildProcess waitpid(IntegralChildProcess pid, int *status, int options);
#endif
#ifdef _WIN32
int kill(IntegralChildProcess pid, int signal_number);
#endif
int child_process_exit_code(int status);
const char *integral_gb_runtime_server_path(void);
const char *integral_gb_runtime_mobile_runtime_path(void);
const char *integral_n64_runtime_home_path(void);
int integral_runtime_frontend_access(const char *path);
void copy_text(char *dest, size_t dest_size, const char *src);
bool parse_iso8601_unix(const char *value, long long *unix_time_out);
void append_text(char *dest, size_t dest_size, const char *src);
bool append_ascii_text(char *dest, size_t dest_size, const char *src);
bool append_alnum_text(char *dest, size_t dest_size, const char *src);
void remove_last_char(char *text);
void remove_last_utf8_char(char *text);
const char *path_file_name(const char *path);
bool game_controller_input_event(Uint32 type);
bool integral_n64_runtime_paths(char *frontend,
                                    size_t frontend_size,
                                    char *core,
                                    size_t core_size,
                                    char *video,
                                    size_t video_size,
                                    char *audio,
                                    size_t audio_size,
                                    char *input,
                                    size_t input_size,
                                    char *rsp,
                                    size_t rsp_size,
                                    char *data,
                                    size_t data_size);
int n64_key_name_to_scancode(const char *name);
bool configured_controller_binding(const char *name);
bool make_n64_runtime_keymap_spec(const char *spec, char *out, size_t out_size);
#endif

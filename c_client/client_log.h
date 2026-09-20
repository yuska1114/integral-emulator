/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_LOG_H
#define INTEGRAL_CLIENT_LOG_H
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>
typedef struct AppState AppState;

#ifdef _WIN32
struct tm *localtime_r(const time_t *timep, struct tm *result);
#endif
void client_log_open(const char *path);
void client_log_close(void);
void client_log_v(const AppState *state, const char *event, const char *fmt, va_list args);
void client_log(const AppState *state, const char *event, const char *fmt, ...);
void client_save_log(const char *event, const char *fmt, ...);
void redirect_child_output_to_client_log(void);
#ifndef _WIN32
int run_client_log_permission_smoke(const char *work_dir);
#endif
#endif

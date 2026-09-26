/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_PROCESS_H
#define INTEGRAL_CLIENT_PROCESS_H

#include "client_save_sync.h"

#ifdef _WIN32
IntegralChildProcess waitpid(IntegralChildProcess pid, int *status, int options);
int kill(IntegralChildProcess pid, int signal_number);
#endif

int child_process_exit_code(int status);

#endif

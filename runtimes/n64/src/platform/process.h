/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INTEGRAL_N64_RUNTIME_PLATFORM_PROCESS_H
#define INTEGRAL_N64_RUNTIME_PLATFORM_PROCESS_H

#include <stdbool.h>

typedef struct IntegralN64RuntimeProcess {
#ifdef _WIN32
    void *handle;
    unsigned long id;
#else
    int pid;
#endif
    bool running;
} IntegralN64RuntimeProcess;

int integral_n64_runtime_process_start(IntegralN64RuntimeProcess *process, char *const argv[]);
int integral_n64_runtime_process_poll(IntegralN64RuntimeProcess *process, int *exit_code);
int integral_n64_runtime_process_stop(IntegralN64RuntimeProcess *process);

#endif

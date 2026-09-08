/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdio.h>

#include "platform/process.h"
#include "platform/thread.h"

#ifndef _WIN32
static int wait_for_exit(IntegralN64RuntimeProcess *process, int *exit_code)
{
    unsigned attempt;
    for (attempt = 0u; attempt < 200u; ++attempt) {
        int result = integral_n64_runtime_process_poll(process, exit_code);
        if (result <= 0) return result;
        integral_n64_runtime_thread_sleep(5u);
    }
    return -1;
}
#endif

int main(void)
{
#ifdef _WIN32
    puts("N64 Runtime process test skipped on Windows");
    return 0;
#else
    IntegralN64RuntimeProcess process;
    int exit_code = 0;
    char *exit_arguments[] = {"/bin/sh", "-c", "exit 7", NULL};
    char *sleep_arguments[] = {"/bin/sh", "-c", "sleep 10", NULL};
    if (integral_n64_runtime_process_start(&process, exit_arguments) != 0 ||
        wait_for_exit(&process, &exit_code) != 0 || exit_code != 7) {
        fprintf(stderr, "process exit-code test failed\n");
        return 1;
    }
    if (integral_n64_runtime_process_start(&process, sleep_arguments) != 0 ||
        integral_n64_runtime_process_stop(&process) != 0 ||
        wait_for_exit(&process, &exit_code) != 0 || exit_code != 143) {
        fprintf(stderr, "process stop test failed: %d\n", exit_code);
        return 1;
    }
    puts("N64 Runtime child-process lifecycle test passed");
    return 0;
#endif
}

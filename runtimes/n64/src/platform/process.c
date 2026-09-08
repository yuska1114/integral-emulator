/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "process.h"

#include <string.h>

#ifdef _WIN32

int integral_n64_runtime_process_start(IntegralN64RuntimeProcess *process, char *const argv[])
{
    (void)process;
    (void)argv;
    return -1;
}

int integral_n64_runtime_process_poll(IntegralN64RuntimeProcess *process, int *exit_code)
{
    (void)process;
    (void)exit_code;
    return -1;
}

int integral_n64_runtime_process_stop(IntegralN64RuntimeProcess *process)
{
    (void)process;
    return -1;
}

#else

#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

int integral_n64_runtime_process_start(IntegralN64RuntimeProcess *process, char *const argv[])
{
    pid_t pid;
    if (!process || !argv || !argv[0]) {
        return -1;
    }
    memset(process, 0, sizeof(*process));
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        execvp(argv[0], argv);
        _exit(127);
    }
    process->pid = (int)pid;
    process->running = true;
    return 0;
}

int integral_n64_runtime_process_poll(IntegralN64RuntimeProcess *process, int *exit_code)
{
    int status = 0;
    pid_t result;
    if (!process || !process->running) {
        return 0;
    }
    result = waitpid((pid_t)process->pid, &status, WNOHANG);
    if (result == 0) {
        return 1;
    }
    if (result < 0) {
        if (errno == EINTR) {
            return 1;
        }
        process->running = false;
        return -1;
    }
    process->running = false;
    if (exit_code) {
        if (WIFEXITED(status)) {
            *exit_code = WEXITSTATUS(status);
        }
        else if (WIFSIGNALED(status)) {
            *exit_code = 128 + WTERMSIG(status);
        }
        else {
            *exit_code = 1;
        }
    }
    return 0;
}

int integral_n64_runtime_process_stop(IntegralN64RuntimeProcess *process)
{
    if (!process || !process->running) {
        return 0;
    }
    return kill((pid_t)process->pid, SIGTERM) == 0 ? 0 : -1;
}

#endif

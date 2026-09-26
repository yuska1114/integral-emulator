/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_process.h"

#include <errno.h>
#include <stdio.h>

#ifdef _WIN32
int main(void)
{
    int status = 0;
    if (child_process_exit_code(23) != 23 || waitpid(0, &status, 0) != -1 || errno != ECHILD) {
        return 1;
    }
    if (kill(0, 0) != -1 || errno != ESRCH) return 1;
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
    pid_t child = fork();
    if (child < 0) return 1;
    if (child == 0) _exit(23);
    int status = 0;
    if (waitpid(child, &status, 0) != child || child_process_exit_code(status) != 23) return 1;

    child = fork();
    if (child < 0) return 1;
    if (child == 0) {
        raise(SIGTERM);
        _exit(1);
    }
    if (waitpid(child, &status, 0) != child || child_process_exit_code(status) != -1) return 1;
#endif
    puts("Client process helpers: PASS");
    return 0;
}

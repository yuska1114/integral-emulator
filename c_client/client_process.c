/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_process.h"

#include <errno.h>
#include <stdint.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifndef WNOHANG
#define WNOHANG 1
#endif
#else
#include <sys/wait.h>
#endif

#ifdef _WIN32
IntegralChildProcess waitpid(IntegralChildProcess pid, int *status, int options)
{
    HANDLE process = (HANDLE)(intptr_t)pid;
    if (!process || process == INVALID_HANDLE_VALUE) {
        errno = ECHILD;
        return -1;
    }
    DWORD wait_ms = options == WNOHANG ? 0 : INFINITE;
    DWORD wait_result = WaitForSingleObject(process, wait_ms);
    if (wait_result == WAIT_TIMEOUT) {
        return 0;
    }
    if (wait_result == WAIT_OBJECT_0) {
        DWORD exit_code = 0;
        if (status && GetExitCodeProcess(process, &exit_code)) {
            *status = (int)exit_code;
        }
        CloseHandle(process);
        return pid;
    }
    CloseHandle(process);
    errno = ECHILD;
    return -1;
}

int kill(IntegralChildProcess pid, int signal_number)
{
    (void)signal_number;
    HANDLE process = (HANDLE)(intptr_t)pid;
    if (!process || process == INVALID_HANDLE_VALUE) {
        errno = ESRCH;
        return -1;
    }
    DWORD wait_result = WaitForSingleObject(process, 0);
    if (wait_result == WAIT_TIMEOUT) {
        return 0;
    }
    errno = ESRCH;
    return -1;
}
#endif

int child_process_exit_code(int status)
{
#ifdef _WIN32
    return status;
#else
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_product_runtime.h"

#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int directory_is_empty(const char *path)
{
    DIR *directory = opendir(path);
    struct dirent *entry;
    if (directory == NULL) return 0;
    while ((entry = readdir(directory)) != NULL) {
        if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
            closedir(directory);
            return 0;
        }
    }
    closedir(directory);
    return 1;
}

int main(void)
{
    char directory[] = "/tmp/integral-gb-runtime-fixed-host-abort-XXXXXX";
    unsigned cycle;
    if (mkdtemp(directory) == NULL || !directory_is_empty(directory)) return 2;
    for (cycle = 0u; cycle < 100u; cycle++) {
        int ready[2];
        pid_t child;
        int status = 0;
        if (pipe(ready) != 0) return 1;
        child = fork();
        if (child < 0) return 1;
        if (child == 0) {
            IntegralGBRuntimeFixedHostProductRuntime runtime;
            uint8_t marker = 1u;
            close(ready[0]);
            if (chdir(directory) != 0 ||
                !integral_gb_runtime_fixed_host_product_runtime_init(
                    &runtime, INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_HOST) ||
                write(ready[1], &marker, 1u) != 1) _exit(3);
            for (;;) pause();
        }
        close(ready[1]);
        {
            uint8_t marker = 0u;
            if (read(ready[0], &marker, 1u) != 1 || marker != 1u) return 1;
        }
        close(ready[0]);
        if (kill(child, SIGKILL) != 0) return 1;
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        if (!WIFSIGNALED(status) || !directory_is_empty(directory)) return 1;
    }
    if (rmdir(directory) != 0) return 1;
    printf("PASS fixed Host abnormal-exit cycles=100 residual_files=0\n");
    return 0;
}

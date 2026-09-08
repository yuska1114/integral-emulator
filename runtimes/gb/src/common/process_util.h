/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_COMMON_PROCESS_UTIL_H
#define INTEGRAL_GB_RUNTIME_COMMON_PROCESS_UTIL_H

int integral_gb_runtime_execv_with_exe_fallback(const char *path, char *const argv[]);
int integral_gb_runtime_chdir_to_package_root(void);

#endif

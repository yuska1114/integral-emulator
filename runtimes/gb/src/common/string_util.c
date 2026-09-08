/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "string_util.h"

#include <string.h>

bool integral_gb_runtime_copy_text(char *dest, size_t dest_size, const char *src)
{
    if (!dest || dest_size == 0 || !src) {
        return false;
    }

    size_t src_len = strlen(src);
    if (src_len >= dest_size) {
        memcpy(dest, src, dest_size - 1);
        dest[dest_size - 1] = '\0';
        return false;
    }

    memcpy(dest, src, src_len + 1);
    return true;
}

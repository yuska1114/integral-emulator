/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_runtime_fixed_host_result_ipc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct Buffer { uint8_t bytes[512]; size_t used, read; } Buffer;
static ptrdiff_t put(void *ctx, const uint8_t *src, size_t size) {
    Buffer *b = ctx; if (b->used + size > sizeof(b->bytes)) return -1;
    memcpy(b->bytes + b->used, src, size); b->used += size; return (ptrdiff_t)size;
}
static ptrdiff_t get(void *ctx, uint8_t *dst, size_t size) {
    Buffer *b = ctx; if (b->read + size > b->used) return 0;
    memcpy(dst, b->bytes + b->read, size); b->read += size; return (ptrdiff_t)size;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int main(void) {
    Buffer b = {0}; IntegralGBRuntimeFixedHostResult in = {0}, out = {0};
    in.host = true; in.final_frame = 77u; memset(in.terminal_digest, 0x42, 32u);
    in.candidates.host_size = 3u; in.candidates.remote_size = 4u;
    in.candidates.host_data = malloc(3u); in.candidates.remote_data = malloc(4u);
    CHECK(in.candidates.host_data && in.candidates.remote_data);
    memcpy(in.candidates.host_data, "abc", 3u); memcpy(in.candidates.remote_data, "defg", 4u);
    CHECK(integral_gb_runtime_fixed_host_result_ipc_send(put, &b, &in));
    CHECK(integral_gb_runtime_fixed_host_result_ipc_receive(get, &b, &out));
    CHECK(out.host && out.final_frame == 77u && out.candidates.host_size == 3u &&
          out.candidates.remote_size == 4u && !memcmp(out.terminal_digest, "BBBB", 4u));
    integral_gb_runtime_fixed_host_result_release(&out);
    puts("gb_runtime_fixed_host_result_ipc_test: PASS"); return 0;
}

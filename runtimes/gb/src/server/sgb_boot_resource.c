/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sgb_boot_resource.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#define INTEGRAL_GB_RUNTIME_RESOURCE_PATH_MAX 4096u

typedef struct Sha256State {
    uint32_t h[8];
    uint64_t bit_count;
    uint8_t block[64];
    size_t used;
} Sha256State;

static uint32_t rotr32(uint32_t value, unsigned shift)
{
    return (value >> shift) | (value << (32u - shift));
}

static void sha256_transform(Sha256State *state, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5, 0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
        0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3, 0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
        0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC, 0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
        0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7, 0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
        0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13, 0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
        0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3, 0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
        0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5, 0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
        0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208, 0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2,
    };
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; i++) {
        uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state->h[0], b = state->h[1], c = state->h[2], d = state->h[3];
    uint32_t e = state->h[4], f = state->h[5], g = state->h[6], h = state->h[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ (~e & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state->h[0] += a; state->h[1] += b; state->h[2] += c; state->h[3] += d;
    state->h[4] += e; state->h[5] += f; state->h[6] += g; state->h[7] += h;
}

static void sha256_init(Sha256State *state)
{
    static const uint32_t initial[8] = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19,
    };
    memset(state, 0, sizeof(*state));
    memcpy(state->h, initial, sizeof(initial));
}

static void sha256_update(Sha256State *state, const uint8_t *data, size_t size)
{
    state->bit_count += (uint64_t)size * 8u;
    while (size > 0) {
        size_t available = sizeof(state->block) - state->used;
        size_t take = size < available ? size : available;
        memcpy(state->block + state->used, data, take);
        state->used += take;
        data += take;
        size -= take;
        if (state->used == sizeof(state->block)) {
            sha256_transform(state, state->block);
            state->used = 0;
        }
    }
}

static void sha256_finish(Sha256State *state, uint8_t digest[32])
{
    state->block[state->used++] = 0x80;
    if (state->used > 56) {
        memset(state->block + state->used, 0, 64 - state->used);
        sha256_transform(state, state->block);
        state->used = 0;
    }
    memset(state->block + state->used, 0, 56 - state->used);
    for (unsigned i = 0; i < 8; i++) {
        state->block[63 - i] = (uint8_t)(state->bit_count >> (i * 8));
    }
    sha256_transform(state, state->block);
    for (unsigned i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(state->h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(state->h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(state->h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)state->h[i];
    }
}

static int executable_directory(char *output, size_t output_size)
{
    char path[INTEGRAL_GB_RUNTIME_RESOURCE_PATH_MAX];
#ifdef _WIN32
    DWORD length = GetModuleFileNameA(NULL, path, (DWORD)sizeof(path));
    if (length == 0 || length >= sizeof(path)) return -1;
#elif defined(__APPLE__)
    uint32_t size = (uint32_t)sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return -1;
#else
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length <= 0 || (size_t)length >= sizeof(path)) return -1;
    path[length] = '\0';
#endif
    char *slash = strrchr(path, '/');
#ifdef _WIN32
    char *backslash = strrchr(path, '\\');
    if (!slash || (backslash && backslash > slash)) slash = backslash;
#endif
    if (!slash) return -1;
    *slash = '\0';
    if (snprintf(output, output_size, "%s", path) >= (int)output_size) return -1;
    return 0;
}

static int resolve_resource_path(char *output, size_t output_size)
{
    const char *configured = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_SGB2_BOOT_ROM");
    if (configured && configured[0]) {
        return snprintf(output, output_size, "%s", configured) < (int)output_size ? 0 : -1;
    }
    char directory[INTEGRAL_GB_RUNTIME_RESOURCE_PATH_MAX];
    if (executable_directory(directory, sizeof(directory)) != 0) return -1;
    return snprintf(output, output_size, "%s/bootroms/sgb2_boot.bin", directory) < (int)output_size ? 0 : -1;
}

int integral_gb_runtime_sgb2_boot_resource_load(uint8_t output[INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE],
                                        char *resolved_path,
                                        size_t resolved_path_size,
                                        char *error,
                                        size_t error_size)
{
    char path[INTEGRAL_GB_RUNTIME_RESOURCE_PATH_MAX];
    if (resolve_resource_path(path, sizeof(path)) != 0) {
        snprintf(error, error_size, "cannot resolve SGB2 SameBoot resource path");
        return -1;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(error, error_size, "cannot open SGB2 SameBoot resource: %s", strerror(errno));
        return -1;
    }
    size_t size = fread(output, 1, INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE, file);
    int extra = fgetc(file);
    int failed = ferror(file);
    fclose(file);
    if (failed || size != INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE || extra != EOF) {
        snprintf(error, error_size, "SGB2 SameBoot resource must be exactly %u bytes",
                 INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE);
        return -1;
    }
    Sha256State state;
    uint8_t digest[32];
    char hex[65];
    sha256_init(&state);
    sha256_update(&state, output, INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SIZE);
    sha256_finish(&state, digest);
    for (unsigned i = 0; i < sizeof(digest); i++) {
        snprintf(hex + i * 2, 3, "%02x", digest[i]);
    }
    if (strcmp(hex, INTEGRAL_GB_RUNTIME_SGB2_BOOT_ROM_SHA256) != 0) {
        snprintf(error, error_size, "SGB2 SameBoot SHA-256 mismatch");
        return -1;
    }
    if (resolved_path && resolved_path_size > 0) {
        snprintf(resolved_path, resolved_path_size, "%s", path);
    }
    return 0;
}

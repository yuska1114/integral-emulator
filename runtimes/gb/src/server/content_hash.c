/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "content_hash.h"

#include <string.h>

static uint32_t rotr(uint32_t value, unsigned bits)
{
    return (value >> bits) | (value << (32u - bits));
}

static void transform(IntegralGBRuntimeContentSha256 *state, const uint8_t block[64])
{
    static const uint32_t k[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2,
    };
    uint32_t w[64];
    for (unsigned i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               block[i * 4 + 3];
    }
    for (unsigned i = 16; i < 64; i++) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state->h[0], b = state->h[1], c = state->h[2], d = state->h[3];
    uint32_t e = state->h[4], f = state->h[5], g = state->h[6], h = state->h[7];
    for (unsigned i = 0; i < 64; i++) {
        uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + k[i] + w[i];
        uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    state->h[0] += a; state->h[1] += b; state->h[2] += c; state->h[3] += d;
    state->h[4] += e; state->h[5] += f; state->h[6] += g; state->h[7] += h;
}

void integral_gb_runtime_content_sha256_init(IntegralGBRuntimeContentSha256 *state)
{
    static const uint32_t initial[8] = {
        0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,
        0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19,
    };
    memset(state, 0, sizeof(*state));
    memcpy(state->h, initial, sizeof(initial));
}

void integral_gb_runtime_content_sha256_update(IntegralGBRuntimeContentSha256 *state,
                                    const void *data_value,
                                    size_t size)
{
    const uint8_t *data = data_value;
    state->bytes += size;
    while (size > 0) {
        size_t room = sizeof(state->block) - state->block_size;
        size_t take = size < room ? size : room;
        memcpy(state->block + state->block_size, data, take);
        state->block_size += take;
        data += take;
        size -= take;
        if (state->block_size == sizeof(state->block)) {
            transform(state, state->block);
            state->block_size = 0;
        }
    }
}

void integral_gb_runtime_content_sha256_finish(IntegralGBRuntimeContentSha256 *state, uint8_t digest[32])
{
    uint64_t bits = state->bytes * 8u;
    state->block[state->block_size++] = 0x80;
    if (state->block_size > 56) {
        memset(state->block + state->block_size, 0, 64 - state->block_size);
        transform(state, state->block);
        state->block_size = 0;
    }
    memset(state->block + state->block_size, 0, 56 - state->block_size);
    for (unsigned i = 0; i < 8; i++) {
        state->block[63 - i] = (uint8_t)(bits >> (i * 8));
    }
    transform(state, state->block);
    for (unsigned i = 0; i < 8; i++) {
        digest[i * 4] = (uint8_t)(state->h[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(state->h[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(state->h[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)state->h[i];
    }
    memset(state, 0, sizeof(*state));
}

void integral_gb_runtime_content_sha256(const void *data, size_t size, uint8_t digest[32])
{
    IntegralGBRuntimeContentSha256 state;
    integral_gb_runtime_content_sha256_init(&state);
    integral_gb_runtime_content_sha256_update(&state, data, size);
    integral_gb_runtime_content_sha256_finish(&state, digest);
}

void integral_gb_runtime_content_sha256_hex(const uint8_t digest[32], char hex[65])
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned i = 0; i < 32; i++) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & 15];
    }
    hex[64] = '\0';
}

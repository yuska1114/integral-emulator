/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "platform/ime.h"

#include <SDL.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
#ifdef _WIN32
    if (SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) != 0 ||
        SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "N64 Runtime IME test: SDL initialization failed: %s\n",
                SDL_GetError());
        return 1;
    }
    if (!integral_n64_runtime_ime_force_direct_input()) {
        fprintf(stderr,
                "N64 Runtime IME test: physical input setup was rejected\n");
        SDL_Quit();
        return 1;
    }
    SDL_Quit();
    puts("N64 Runtime Windows physical-input setup test passed");
#else
    puts("N64 Runtime Windows physical-input setup test skipped");
#endif
    return 0;
}

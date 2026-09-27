/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "screenshot.h"
#include "../../../common/screenshot.h"
#include <assert.h>
#include <stdio.h>

const uint32_t *integral_gb_runtime_slot_presented_pixels(const IntegralGBRuntimeSlot *slot)
{
    return slot->framebuffer ? slot->framebuffer : slot->pixels;
}

unsigned integral_gb_runtime_slot_screen_width(const IntegralGBRuntimeSlot *slot)
{
    return slot->screen_width ? slot->screen_width : INTEGRAL_GB_RUNTIME_GB_WIDTH;
}

unsigned integral_gb_runtime_slot_screen_height(const IntegralGBRuntimeSlot *slot)
{
    return slot->screen_height ? slot->screen_height : INTEGRAL_GB_RUNTIME_GB_HEIGHT;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    IntegralGBRuntimeSlot a = {
        .initialized = true,
        .screen_width = INTEGRAL_GB_RUNTIME_GB_WIDTH,
        .screen_height = INTEGRAL_GB_RUNTIME_GB_HEIGHT,
    };
    IntegralGBRuntimeSlot b = {
        .initialized = true,
        .screen_width = INTEGRAL_GB_RUNTIME_GB_WIDTH,
        .screen_height = INTEGRAL_GB_RUNTIME_GB_HEIGHT,
    };
    for (unsigned i = 0; i < 160u * 144u; ++i) {
        a.pixels[i] = 0xffff0000; b.pixels[i] = 0xff00ff00;
    }
    const char *modes[] = {"local_gb", "local_gb_server2", "gb_mobile",
                           "link_cable", "local_n64", "n64_room"};
    char path[1024], previous[1024] = {0};
    for (unsigned mode = 0; mode < 6; ++mode) {
        for (unsigned remote = 0; remote < 2; ++remote) {
            assert(integral_gb_runtime_screenshot_save_pair(&a, mode == 1 ? &b : NULL,
                modes[mode], remote ? "remote" : "host", path, sizeof(path)) == 0);
            assert(strstr(path, modes[mode]) && strstr(path, remote ? "_remote_" : "_host_"));
            assert(strcmp(path, previous));
            strcpy(previous, path);
            SDL_Surface *image = SDL_LoadBMP(path);
            assert(image && image->w == (mode == 1 ? 320 : 160) && image->h == 144);
            SDL_Surface *rgba = SDL_ConvertSurfaceFormat(image, SDL_PIXELFORMAT_ARGB8888, 0);
            assert(rgba && ((uint32_t *)rgba->pixels)[0] == a.pixels[0]);
            if (mode == 1) assert(((uint32_t *)rgba->pixels)[160] == b.pixels[0]);
            SDL_FreeSurface(rgba); SDL_FreeSurface(image);
        }
    }
    static uint32_t sgb_pixels[
        INTEGRAL_GB_RUNTIME_SGB_WIDTH * INTEGRAL_GB_RUNTIME_SGB_HEIGHT];
    a.screen_width = INTEGRAL_GB_RUNTIME_SGB_WIDTH;
    a.screen_height = INTEGRAL_GB_RUNTIME_SGB_HEIGHT;
    a.framebuffer = sgb_pixels;
    for (unsigned i = 0; i < INTEGRAL_GB_RUNTIME_SGB_WIDTH * INTEGRAL_GB_RUNTIME_SGB_HEIGHT; ++i) {
        sgb_pixels[i] = 0xff112233;
    }
    assert(integral_gb_runtime_screenshot_save_pair(
        &a, NULL, "local_gb", "local", path, sizeof(path)) == 0);
    SDL_Surface *sgb = SDL_LoadBMP(path);
    assert(sgb && sgb->w == (int)INTEGRAL_GB_RUNTIME_SGB_WIDTH &&
           sgb->h == (int)INTEGRAL_GB_RUNTIME_SGB_HEIGHT);
    SDL_FreeSurface(sgb);

    assert(integral_gb_runtime_screenshot_save_pair(&a, NULL, "local_gb", "local", path, 2) != 0);
    puts("Screenshot filenames, persistence, SERVER2 composition and SGB geometry PASS");
    return 0;
}

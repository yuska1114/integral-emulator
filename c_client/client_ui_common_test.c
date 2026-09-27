/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_ui_common.h"

#include <assert.h>
#include <stdio.h>

static SDL_Color read_pixel(SDL_Renderer *renderer, int x, int y)
{
    Uint32 pixel = 0;
    SDL_Rect area = {.x = x, .y = y, .w = 1, .h = 1};
    assert(SDL_RenderReadPixels(renderer, &area, SDL_PIXELFORMAT_RGBA32,
                               &pixel, sizeof(pixel)) == 0);
    SDL_Color color;
    SDL_PixelFormat *format = SDL_AllocFormat(SDL_PIXELFORMAT_RGBA32);
    assert(format);
    SDL_GetRGBA(pixel, format,
                &color.r, &color.g, &color.b, &color.a);
    SDL_FreeFormat(format);
    return color;
}

static void assert_color(SDL_Color actual, SDL_Color expected)
{
    assert(actual.r == expected.r);
    assert(actual.g == expected.g);
    assert(actual.b == expected.b);
    assert(actual.a == expected.a);
}

int main(void)
{
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(
        0, 64, 64, 32, SDL_PIXELFORMAT_RGBA32);
    assert(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    assert(renderer);

    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    integral_client_ui_clear_screen(renderer);
    assert_color(read_pixel(renderer, 1, 1), theme->background);

    SDL_Rect bounds = {.x = 8, .y = 8, .w = 20, .h = 20};
    integral_client_ui_fill_selection(renderer, bounds, false);
    assert_color(read_pixel(renderer, 10, 10), theme->selection_background);
    integral_client_ui_fill_selection(renderer, bounds, true);
    assert_color(read_pixel(renderer, 10, 10), theme->active_background);

    integral_client_ui_draw_panel(renderer, 30, 30, 20, 20, theme->panel_border);
    assert_color(read_pixel(renderer, 34, 34), theme->panel_fill);
    assert_color(read_pixel(renderer, 30, 30), theme->panel_border);

    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    SDL_Quit();
    puts("client ui common: PASS");
    return 0;
}

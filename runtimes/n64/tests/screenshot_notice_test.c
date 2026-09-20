/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include "gui/screenshot_notice.h"
#include <assert.h>
#include <string.h>
int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *window = SDL_CreateWindow("N64 game", 0, 0, 160, 144, SDL_WINDOW_HIDDEN);
    assert(window);
    IntegralN64ScreenshotNotice notice = {0};
    integral_n64_screenshot_notice_show(&notice, window, true, 100);
    assert(!strcmp(SDL_GetWindowTitle(window), "N64 game - SCREENSHOT SAVED"));
    integral_n64_screenshot_notice_tick(&notice, 2099);
    assert(notice.window_id);
    integral_n64_screenshot_notice_show(&notice, window, false, 2000);
    assert(!strcmp(SDL_GetWindowTitle(window), "N64 game - SCREENSHOT FAILED"));
    integral_n64_screenshot_notice_tick(&notice, 4000);
    assert(!strcmp(SDL_GetWindowTitle(window), "N64 game") && !notice.window_id);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("N64 capture title success/failure, extension and restoration PASS");
    return 0;
}

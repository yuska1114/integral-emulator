/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_screenshots.h"
#include <assert.h>
#include <png.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir_test(p) _mkdir(p)
#else
#include <unistd.h>
#define mkdir_test(p) mkdir(p,0700)
#endif

static bool key(IntegralScreenshots *v, SDL_Keycode code)
{
    SDL_KeyboardEvent event = {0}; event.keysym.sym = code;
    return integral_screenshots_key(v, &event);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(SDL_Init(SDL_INIT_VIDEO) == 0);
    SDL_Window *w = SDL_CreateWindow("Screenshot test",0,0,480,480,SDL_WINDOW_HIDDEN);
    assert(w);
    SDL_Renderer *r = SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE); assert(r);
    IntegralScreenshots *v = integral_screenshots_open(argv[1]);
    assert(v && v->count == 0);
    integral_screenshots_draw(r,v);
    assert(key(v,SDLK_RETURN));
    integral_screenshots_close(v);
    char path[4096], screenshot[4096];
#define PATH(relative) assert(snprintf(path,sizeof(path),"%s/%s",argv[1],relative) < (int)sizeof(path))
    PATH("screenshot"); assert(mkdir_test(path) == 0);
    PATH("runtime"); assert(mkdir_test(path) == 0);
    PATH("runtime/n64_runtime"); assert(mkdir_test(path) == 0);
    PATH("runtime/n64_runtime/a"); assert(mkdir_test(path) == 0);
    PATH("runtime/n64_runtime/a/screenshots"); assert(mkdir_test(path) == 0);
    PATH("runtime/n64_runtime_media"); assert(mkdir_test(path) == 0);
    PATH("runtime/n64_runtime_media/b"); assert(mkdir_test(path) == 0);
    PATH("runtime/n64_runtime_media/b/screenshots"); assert(mkdir_test(path) == 0);
    SDL_Surface *image = SDL_CreateRGBSurfaceWithFormat(0,160,144,32,SDL_PIXELFORMAT_RGBA32);
    assert(image); SDL_FillRect(image,NULL,SDL_MapRGB(image->format,10,120,220));
    PATH("screenshot/a.bmp"); assert(SDL_SaveBMP(image,path) == 0);
    png_image png = {0}; png.version = PNG_IMAGE_VERSION;
    png.width = 160; png.height = 144; png.format = PNG_FORMAT_RGBA;
    PATH("runtime/n64_runtime/a/screenshots/b.png");
    assert(png_image_write_to_file(&png,path,0,image->pixels,image->pitch,NULL));
    PATH("runtime/n64_runtime_media/b/screenshots/c.PNG");
    assert(png_image_write_to_file(&png,path,0,image->pixels,image->pitch,NULL));
    PATH("screenshot/broken.bmp"); FILE *f = fopen(path,"wb"); assert(f);
    fputs("broken",f); fclose(f);
    PATH("screenshot/save.sav"); f = fopen(path,"wb"); assert(f); fputs("not an image",f); fclose(f);
#ifndef _WIN32
    PATH("screenshot/alias.bmp"); assert(symlink("a.bmp",path) == 0);
#endif
    v = integral_screenshots_open(argv[1]); assert(v && v->count == 4);
    assert(v->selected == 3);
    unsigned loaded = 0, failed = 0;
    for (size_t i=0;i<v->count;i++) {
        if (v->image) loaded++; else failed++;
        integral_screenshots_draw(r,v);
        key(v,SDLK_RIGHT);
    }
    assert(loaded == 3 && failed == 1 && v->selected == 3);
    SDL_Rect rect = integral_screenshots_fit(160,144);
    assert(rect.w == 320 && rect.h == 288);
    rect = integral_screenshots_fit(1920,1080);
    assert(rect.w == 432 && rect.h == 243 && rect.x >= 24 && rect.y >= 70);
    rect = integral_screenshots_fit(144,1000); assert(rect.h == 300 && rect.w > 0);
    assert(integral_screenshots_fit(0,10).w == 0);
    /* Delete is NO by default; escape cancels; repeat must not confirm. */
    key(v,SDLK_DELETE); assert(v->confirm_delete && !v->confirm_yes);
    key(v,SDLK_RETURN); assert(v->count == 4);
    key(v,SDLK_DELETE); key(v,SDLK_LEFT);
    SDL_KeyboardEvent repeat = {0}; repeat.repeat=1; repeat.keysym.sym=SDLK_RETURN;
    assert(!integral_screenshots_key(v,&repeat) && v->count == 4);
    key(v,SDLK_ESCAPE); assert(!v->confirm_delete && v->count == 4);
    integral_screenshots_draw(r,v);
    /* Optional readback retained by the caller outside the temporary fixture. */
    const char *capture = getenv("INTEGRAL_SCREENSHOT_TEST_CAPTURE");
    if (capture) {
        while (!v->image) key(v,SDLK_RIGHT);
        integral_screenshots_draw(r,v);
        SDL_Surface *frame = SDL_CreateRGBSurfaceWithFormat(0,480,480,32,SDL_PIXELFORMAT_RGBA32);
        assert(frame && SDL_RenderReadPixels(r,NULL,frame->format->format,frame->pixels,frame->pitch) == 0);
        assert(SDL_SaveBMP(frame,capture) == 0); SDL_FreeSurface(frame);
    }
    while (v->count) {
        snprintf(screenshot,sizeof(screenshot),"%s",v->entries[v->selected].path);
        key(v,SDLK_DELETE); key(v,SDLK_LEFT); key(v,SDLK_RETURN);
        struct stat info; assert(stat(screenshot,&info) != 0);
    }
    integral_screenshots_draw(r,v); assert(key(v,SDLK_ESCAPE));
    PATH("screenshot/save.sav"); struct stat info; assert(stat(path,&info) == 0);
    integral_screenshots_close(v);
    SDL_FreeSurface(image); SDL_DestroyRenderer(r); SDL_DestroyWindow(w); SDL_Quit();
    puts("Screenshots: BMP/PNG, roots, navigation, fit, broken/empty, delete confirmation PASS");
    return 0;
}

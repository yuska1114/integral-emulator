/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sdl_unicode_text.h"

int integral_text_scroll_offset(int text_width, int view_width, Uint32 ticks, bool tail)
{
    if (view_width <= 0 || text_width <= view_width) return 0;
    unsigned excess = (unsigned)(text_width - view_width);
    if (tail) return (int)excess;
    Uint64 travel = (Uint64)excess * 20u;
    Uint64 phase = ticks % (2000u + travel);
    if (phase < 1000u) return 0;
    if (phase >= 1000u + travel) return (int)excess;
    return (int)((phase - 1000u) / 20u);
}

void integral_sdl_draw_utf8_text(SDL_Renderer *renderer, int x, int y,
    const char *text, int font_size, SDL_Color color, int max_width)
{
    integral_sdl_draw_utf8_scrolled(renderer, x, y, text, font_size, color, max_width, 0, false);
}

#ifdef INTEGRAL_USE_SDL_TTF

#include <SDL_ttf.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

static const char *chat_font_candidates[] = {
    NULL,
    "C:/Windows/Fonts/meiryo.ttc",
    "C:/Windows/Fonts/YuGothM.ttc",
    "C:/Windows/Fonts/msgothic.ttc",
    "/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
    "/Library/Fonts/Arial Unicode.ttf",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
};

static TTF_Font *open_chat_font(int font_size)
{
    static bool initialized = false;
    if (!initialized) {
        if (TTF_Init() != 0) {
            return NULL;
        }
        initialized = true;
    }

    chat_font_candidates[0] = getenv("INTEGRAL_EMULATOR_CHAT_FONT");
    for (size_t i = 0; i < sizeof(chat_font_candidates) / sizeof(chat_font_candidates[0]); i++) {
        const char *path = chat_font_candidates[i];
        if (!path || path[0] == '\0') {
            continue;
        }
        TTF_Font *font = TTF_OpenFont(path, font_size);
        if (font) {
            return font;
        }
    }
    return NULL;
}

void integral_sdl_draw_utf8_scrolled(SDL_Renderer *renderer,
                            int x,
                            int y,
                            const char *text,
                            int font_size,
                            SDL_Color color,
                            int max_width, Uint32 ticks, bool tail)
{
    if (!renderer || !text || text[0] == '\0' || font_size <= 0 || max_width <= 0) {
        return;
    }

    TTF_Font *font = open_chat_font(font_size);
    if (!font) {
        return;
    }

    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, text, color);
    if (surface) {
        SDL_Rect src = {.x = 0, .y = 0, .w = surface->w, .h = surface->h};
        src.x = integral_text_scroll_offset(surface->w, max_width, ticks, tail);
        if (src.w > max_width) {
            src.w = max_width;
        }
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        if (texture) {
            SDL_Rect dst = {.x = x, .y = y, .w = src.w, .h = src.h};
            SDL_RenderCopy(renderer, texture, &src, &dst);
            SDL_DestroyTexture(texture);
        }
        SDL_FreeSurface(surface);
    }
    TTF_CloseFont(font);
}

#elif defined(__APPLE__)

#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static CTFontRef create_chat_font(CGFloat size)
{
    CTFontRef font = CTFontCreateWithName(CFSTR("Hiragino Sans"), size, NULL);
    if (!font) {
        font = CTFontCreateWithName(CFSTR("Arial Unicode MS"), size, NULL);
    }
    if (!font) {
        font = CTFontCreateWithName(CFSTR("Helvetica"), size, NULL);
    }
    return font;
}

void integral_sdl_draw_utf8_scrolled(SDL_Renderer *renderer,
                            int x,
                            int y,
                            const char *text,
                            int font_size,
                            SDL_Color color,
                            int max_width, Uint32 ticks, bool tail)
{
    if (!renderer || !text || text[0] == '\0' || font_size <= 0 || max_width <= 0) {
        return;
    }

    CFStringRef string = CFStringCreateWithCString(kCFAllocatorDefault, text, kCFStringEncodingUTF8);
    if (!string) {
        return;
    }

    CTFontRef font = create_chat_font((CGFloat)font_size);
    if (!font) {
        CFRelease(string);
        return;
    }

    CGFloat components[] = {
        color.r / 255.0,
        color.g / 255.0,
        color.b / 255.0,
        color.a / 255.0,
    };
    CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
    CGColorRef cg_color = CGColorCreate(rgb, components);
    const void *keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
    const void *values[] = {font, cg_color};
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault,
                                               keys,
                                               values,
                                               2,
                                               &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef attributed = CFAttributedStringCreate(kCFAllocatorDefault, string, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(attributed);

    CGFloat ascent = 0;
    CGFloat descent = 0;
    CGFloat leading = 0;
    double typographic_width = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    int texture_width = (int)ceil(typographic_width) + 4;
    int offset = integral_text_scroll_offset(texture_width, max_width, ticks, tail);
    int texture_height = (int)ceil(ascent + descent + leading) + 4;
    if (texture_width < 1) {
        texture_width = 1;
    }
    if (texture_height < 1) {
        texture_height = font_size + 6;
    }
    if (texture_width > max_width) {
        texture_width = max_width;
    }

    size_t stride = (size_t)texture_width * 4;
    size_t byte_count = stride * (size_t)texture_height;
    unsigned char *pixels = calloc(1, byte_count);
    if (!pixels) {
        CFRelease(line);
        CFRelease(attributed);
        CFRelease(attrs);
        CGColorRelease(cg_color);
        CGColorSpaceRelease(rgb);
        CFRelease(font);
        CFRelease(string);
        return;
    }

    CGContextRef context = CGBitmapContextCreate(pixels,
                                                 (size_t)texture_width,
                                                 (size_t)texture_height,
                                                 8,
                                                 stride,
                                                 rgb,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    if (context) {
        CGContextSetTextPosition(context, 2 - offset, descent + 2);
        CTLineDraw(line, context);
        SDL_Texture *texture = SDL_CreateTexture(renderer,
                                                 SDL_PIXELFORMAT_ARGB8888,
                                                 SDL_TEXTUREACCESS_STATIC,
                                                 texture_width,
                                                 texture_height);
        if (texture) {
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(texture, NULL, pixels, (int)stride);
            SDL_Rect dst = {.x = x, .y = y, .w = texture_width, .h = texture_height};
            SDL_RenderCopy(renderer, texture, NULL, &dst);
            SDL_DestroyTexture(texture);
        }
        CGContextRelease(context);
    }

    free(pixels);
    CFRelease(line);
    CFRelease(attributed);
    CFRelease(attrs);
    CGColorRelease(cg_color);
    CGColorSpaceRelease(rgb);
    CFRelease(font);
    CFRelease(string);
}

#else

#include "sdl_text.h"

void integral_sdl_draw_utf8_scrolled(SDL_Renderer *renderer,
                            int x,
                            int y,
                            const char *text,
                            int font_size,
                            SDL_Color color,
                            int max_width, Uint32 ticks, bool tail)
{
    (void)ticks;
    (void)tail;
    int scale = font_size >= 14 ? 2 : 1;
    integral_sdl_draw_text_fit(renderer, x, y, text, scale, color, max_width);
}

#endif

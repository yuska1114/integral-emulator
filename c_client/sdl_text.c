/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "sdl_text.h"

#include <stdint.h>
#include <string.h>

static const uint8_t *glyph_for_char(char c)
{
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t unknown[7] = {31, 17, 5, 2, 4, 0, 4};
    static const uint8_t glyphs[][7] = {
        ['0'] = {14, 17, 19, 21, 25, 17, 14},
        ['1'] = {4, 12, 4, 4, 4, 4, 14},
        ['2'] = {14, 17, 1, 2, 4, 8, 31},
        ['3'] = {30, 1, 1, 14, 1, 1, 30},
        ['4'] = {2, 6, 10, 18, 31, 2, 2},
        ['5'] = {31, 16, 30, 1, 1, 17, 14},
        ['6'] = {6, 8, 16, 30, 17, 17, 14},
        ['7'] = {31, 1, 2, 4, 8, 8, 8},
        ['8'] = {14, 17, 17, 14, 17, 17, 14},
        ['9'] = {14, 17, 17, 15, 1, 2, 12},
        ['A'] = {14, 17, 17, 31, 17, 17, 17},
        ['B'] = {30, 17, 17, 30, 17, 17, 30},
        ['C'] = {14, 17, 16, 16, 16, 17, 14},
        ['D'] = {30, 17, 17, 17, 17, 17, 30},
        ['E'] = {31, 16, 16, 30, 16, 16, 31},
        ['F'] = {31, 16, 16, 30, 16, 16, 16},
        ['G'] = {14, 17, 16, 23, 17, 17, 15},
        ['H'] = {17, 17, 17, 31, 17, 17, 17},
        ['I'] = {14, 4, 4, 4, 4, 4, 14},
        ['J'] = {7, 2, 2, 2, 18, 18, 12},
        ['K'] = {17, 18, 20, 24, 20, 18, 17},
        ['L'] = {16, 16, 16, 16, 16, 16, 31},
        ['M'] = {17, 27, 21, 21, 17, 17, 17},
        ['N'] = {17, 25, 21, 19, 17, 17, 17},
        ['O'] = {14, 17, 17, 17, 17, 17, 14},
        ['P'] = {30, 17, 17, 30, 16, 16, 16},
        ['Q'] = {14, 17, 17, 17, 21, 18, 13},
        ['R'] = {30, 17, 17, 30, 20, 18, 17},
        ['S'] = {15, 16, 16, 14, 1, 1, 30},
        ['T'] = {31, 4, 4, 4, 4, 4, 4},
        ['U'] = {17, 17, 17, 17, 17, 17, 14},
        ['V'] = {17, 17, 17, 17, 17, 10, 4},
        ['W'] = {17, 17, 17, 21, 21, 21, 10},
        ['X'] = {17, 17, 10, 4, 10, 17, 17},
        ['Y'] = {17, 17, 10, 4, 4, 4, 4},
        ['Z'] = {31, 1, 2, 4, 8, 16, 31},
        ['a'] = {0, 0, 14, 1, 15, 17, 15},
        ['b'] = {16, 16, 30, 17, 17, 17, 30},
        ['c'] = {0, 0, 14, 16, 16, 17, 14},
        ['d'] = {1, 1, 15, 17, 17, 17, 15},
        ['e'] = {0, 0, 14, 17, 31, 16, 14},
        ['f'] = {6, 8, 30, 8, 8, 8, 8},
        ['g'] = {0, 0, 15, 17, 15, 1, 14},
        ['h'] = {16, 16, 30, 17, 17, 17, 17},
        ['i'] = {4, 0, 12, 4, 4, 4, 14},
        ['j'] = {2, 0, 6, 2, 2, 18, 12},
        ['k'] = {16, 16, 18, 20, 24, 20, 18},
        ['l'] = {12, 4, 4, 4, 4, 4, 14},
        ['m'] = {0, 0, 26, 21, 21, 21, 21},
        ['n'] = {0, 0, 30, 17, 17, 17, 17},
        ['o'] = {0, 0, 14, 17, 17, 17, 14},
        ['p'] = {0, 0, 30, 17, 30, 16, 16},
        ['q'] = {0, 0, 15, 17, 15, 1, 1},
        ['r'] = {0, 0, 22, 25, 16, 16, 16},
        ['s'] = {0, 0, 15, 16, 14, 1, 30},
        ['t'] = {8, 8, 30, 8, 8, 9, 6},
        ['u'] = {0, 0, 17, 17, 17, 19, 13},
        ['v'] = {0, 0, 17, 17, 17, 10, 4},
        ['w'] = {0, 0, 17, 17, 21, 21, 10},
        ['x'] = {0, 0, 17, 10, 4, 10, 17},
        ['y'] = {0, 0, 17, 17, 15, 1, 14},
        ['z'] = {0, 0, 31, 2, 4, 8, 31},
        [' '] = {0, 0, 0, 0, 0, 0, 0},
        ['?'] = {14, 17, 1, 2, 4, 0, 4},
        ['.'] = {0, 0, 0, 0, 0, 12, 12},
        ['/'] = {1, 1, 2, 4, 8, 16, 16},
        ['\\'] = {16, 16, 8, 4, 2, 1, 1},
        ['+'] = {0, 4, 4, 31, 4, 4, 0},
        ['-'] = {0, 0, 0, 31, 0, 0, 0},
        ['_'] = {0, 0, 0, 0, 0, 0, 31},
        [':'] = {0, 12, 12, 0, 12, 12, 0},
        ['&'] = {12, 18, 20, 8, 21, 18, 13},
        ['('] = {2, 4, 8, 8, 8, 4, 2},
        [')'] = {8, 4, 2, 2, 2, 4, 8},
        ['['] = {14, 8, 8, 8, 8, 8, 14},
        [']'] = {14, 2, 2, 2, 2, 2, 14},
        ['<'] = {2, 4, 8, 16, 8, 4, 2},
        ['>'] = {8, 4, 2, 1, 2, 4, 8},
        ['='] = {0, 0, 31, 0, 31, 0, 0},
        [','] = {0, 0, 0, 0, 12, 4, 8},
        ['@'] = {14, 17, 23, 21, 23, 16, 14},
        ['*'] = {0, 21, 14, 31, 14, 21, 0},
    };
    unsigned char uc = (unsigned char)c;
    if (uc >= sizeof(glyphs) / sizeof(glyphs[0])) {
        return unknown;
    }
    if (uc == ' ') {
        return blank;
    }
    if (glyphs[uc][0] == 0 && glyphs[uc][1] == 0 && glyphs[uc][2] == 0 &&
        glyphs[uc][3] == 0 && glyphs[uc][4] == 0 && glyphs[uc][5] == 0 &&
        glyphs[uc][6] == 0) {
        return unknown;
    }
    return glyphs[uc];
}

void integral_sdl_draw_text(SDL_Renderer *renderer,
                       int x,
                       int y,
                       const char *text,
                       int scale,
                       SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (const char *p = text; *p; p++) {
        const uint8_t *glyph = glyph_for_char(*p);
        for (int row = 0; row < 7; row++) {
            for (int col = 0; col < 5; col++) {
                if (glyph[row] & (1 << (4 - col))) {
                    SDL_Rect rect = {
                        .x = x + col * scale,
                        .y = y + row * scale,
                        .w = scale,
                        .h = scale,
                    };
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }
        x += 6 * scale;
    }
}

void integral_sdl_draw_text_fit(SDL_Renderer *renderer,
                           int x,
                           int y,
                           const char *text,
                           int scale,
                           SDL_Color color,
                           int max_width)
{
    if (max_width <= 0 || scale <= 0) {
        return;
    }

    size_t max_chars = (size_t)(max_width / (6 * scale));
    if (max_chars == 0) {
        return;
    }
    size_t len = strlen(text);
    if (len < max_chars) {
        integral_sdl_draw_text(renderer, x, y, text, scale, color);
        return;
    }

    char clipped[256];
    if (max_chars >= sizeof(clipped)) {
        max_chars = sizeof(clipped) - 1;
    }
    if (max_chars > 1) {
        memcpy(clipped, text, max_chars - 1);
        clipped[max_chars - 1] = '>';
        clipped[max_chars] = '\0';
    }
    else {
        clipped[0] = '\0';
    }
    integral_sdl_draw_text(renderer, x, y, clipped, scale, color);
}

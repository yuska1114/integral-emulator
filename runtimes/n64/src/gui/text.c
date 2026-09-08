/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "text.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

static const uint8_t *glyph_for_char(char c)
{
    static const uint8_t unknown[7] = {14, 17, 1, 2, 4, 0, 4};
    static const uint8_t glyphs[][7] = {
        ['0'] = {14,17,19,21,25,17,14}, ['1'] = {4,12,4,4,4,4,14},
        ['2'] = {14,17,1,2,4,8,31}, ['3'] = {30,1,1,14,1,1,30},
        ['4'] = {2,6,10,18,31,2,2}, ['5'] = {31,16,30,1,1,17,14},
        ['6'] = {6,8,16,30,17,17,14}, ['7'] = {31,1,2,4,8,8,8},
        ['8'] = {14,17,17,14,17,17,14}, ['9'] = {14,17,17,15,1,2,12},
        ['A'] = {14,17,17,31,17,17,17}, ['B'] = {30,17,17,30,17,17,30},
        ['C'] = {14,17,16,16,16,17,14}, ['D'] = {30,17,17,17,17,17,30},
        ['E'] = {31,16,16,30,16,16,31}, ['F'] = {31,16,16,30,16,16,16},
        ['G'] = {14,17,16,23,17,17,15}, ['H'] = {17,17,17,31,17,17,17},
        ['I'] = {14,4,4,4,4,4,14}, ['J'] = {7,2,2,2,18,18,12},
        ['K'] = {17,18,20,24,20,18,17}, ['L'] = {16,16,16,16,16,16,31},
        ['M'] = {17,27,21,21,17,17,17}, ['N'] = {17,25,21,19,17,17,17},
        ['O'] = {14,17,17,17,17,17,14}, ['P'] = {30,17,17,30,16,16,16},
        ['Q'] = {14,17,17,17,21,18,13}, ['R'] = {30,17,17,30,20,18,17},
        ['S'] = {15,16,16,14,1,1,30}, ['T'] = {31,4,4,4,4,4,4},
        ['U'] = {17,17,17,17,17,17,14}, ['V'] = {17,17,17,17,17,10,4},
        ['W'] = {17,17,17,21,21,21,10}, ['X'] = {17,17,10,4,10,17,17},
        ['Y'] = {17,17,10,4,4,4,4}, ['Z'] = {31,1,2,4,8,16,31},
        [' '] = {0,0,0,0,0,0,0}, ['?'] = {14,17,1,2,4,0,4},
        ['.'] = {0,0,0,0,0,12,12}, ['/'] = {1,1,2,4,8,16,16},
        ['-'] = {0,0,0,31,0,0,0}, ['_'] = {0,0,0,0,0,0,31},
        [':'] = {0,12,12,0,12,12,0}, ['('] = {2,4,8,8,8,4,2},
        [')'] = {8,4,2,2,2,4,8}, ['<'] = {2,4,8,16,8,4,2},
        ['>'] = {8,4,2,1,2,4,8}, ['='] = {0,0,31,0,31,0,0},
    };
    unsigned char uc = (unsigned char)c;
    if (uc >= sizeof(glyphs) / sizeof(glyphs[0])) return unknown;
    if (uc != ' ' && memcmp(glyphs[uc], (uint8_t[7]){0}, 7) == 0) return unknown;
    return glyphs[uc];
}

void integral_n64_runtime_gui_draw_text(SDL_Renderer *renderer, int x, int y,
                             const char *text, int scale, SDL_Color color)
{
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    for (; *text; ++text) {
        const uint8_t *glyph = glyph_for_char((char)toupper((unsigned char)*text));
        int row;
        for (row = 0; row < 7; ++row) {
            int col;
            for (col = 0; col < 5; ++col) {
                if ((glyph[row] & (1u << (4 - col))) != 0) {
                    SDL_Rect pixel = {x + col * scale, y + row * scale, scale, scale};
                    SDL_RenderFillRect(renderer, &pixel);
                }
            }
        }
        x += 6 * scale;
    }
}

void integral_n64_runtime_gui_draw_text_fit(SDL_Renderer *renderer, int x, int y,
                                 const char *text, int scale, SDL_Color color,
                                 int max_width)
{
    char clipped[256];
    size_t max_chars;
    size_t length = strlen(text);
    if (scale <= 0 || max_width <= 0) return;
    max_chars = (size_t)(max_width / (6 * scale));
    if (max_chars == 0) return;
    if (length <= max_chars) {
        integral_n64_runtime_gui_draw_text(renderer, x, y, text, scale, color);
        return;
    }
    if (max_chars >= sizeof(clipped)) max_chars = sizeof(clipped) - 1u;
    memcpy(clipped, text, max_chars);
    clipped[max_chars - 1u] = '>';
    clipped[max_chars] = '\0';
    integral_n64_runtime_gui_draw_text(renderer, x, y, clipped, scale, color);
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_screenshots.h"
#include "sdl_text.h"
#include <dirent.h>
#include <png.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/* Only enumerate existing screenshot directories, never ROM/SAV trees. */
static bool file_info(const char *path, struct stat *info)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    return stat(path, info) == 0;
#else
    return lstat(path, info) == 0 && !S_ISLNK(info->st_mode);
#endif
}

static bool join(char *out, size_t capacity, const char *root, const char *name)
{
    int n = snprintf(out, capacity, "%s/%s", root, name);
    return n > 0 && (size_t)n < capacity;
}

static bool image_suffix(const char *path)
{
    const char *dot = strrchr(path, '.');
    return dot && (!strcasecmp(dot, ".bmp") || !strcasecmp(dot, ".png"));
}

static void scan(IntegralScreenshots *v, const char *directory, bool sessions)
{
    struct stat info;
    if (!file_info(directory, &info) || !S_ISDIR(info.st_mode)) return;
    DIR *dir = opendir(directory);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir)) && v->count < 4096) {
        if (entry->d_name[0] == '.') continue;
        char path[4096];
        if (!join(path, sizeof(path), directory, entry->d_name) || !file_info(path, &info)) continue;
        if (sessions && S_ISDIR(info.st_mode)) {
            char child[4096];
            if (join(child, sizeof(child), path, "screenshots")) scan(v, child, false);
        } else if (!sessions && S_ISREG(info.st_mode) && image_suffix(path)) {
            char *name = strdup(path);
            if (!name) break;
            IntegralScreenshotEntry *items = realloc(v->entries, (v->count + 1) * sizeof(*items));
            if (!items) { free(name); break; }
            v->entries = items;
            v->entries[v->count++] = (IntegralScreenshotEntry){name, info.st_mtime};
        }
    }
    closedir(dir);
}

static int compare(const void *a, const void *b)
{
    const IntegralScreenshotEntry *x = a, *y = b;
    if (x->modified != y->modified) return x->modified < y->modified ? -1 : 1;
    return strcmp(x->path, y->path);
}

static void clear_image(IntegralScreenshots *v)
{
    SDL_DestroyTexture(v->texture);
    SDL_FreeSurface(v->image);
    v->texture = NULL;
    v->image = NULL;
}

static SDL_Surface *load_image(const char *path)
{
    struct stat info;
    if (!file_info(path, &info) || !S_ISREG(info.st_mode) ||
        info.st_size <= 0 || info.st_size > 64 * 1024 * 1024) return NULL;
    const char *suffix = strrchr(path, '.');
    if (suffix && !strcasecmp(suffix, ".png")) {
        png_image png = {0};
        png.version = PNG_IMAGE_VERSION;
        SDL_Surface *surface = NULL;
        if (png_image_begin_read_from_file(&png, path) && png.width && png.height &&
            png.width <= 4096 && png.height <= 4096) {
            png.format = PNG_FORMAT_RGBA;
            surface = SDL_CreateRGBSurfaceWithFormat(0, (int)png.width, (int)png.height,
                                                     32, SDL_PIXELFORMAT_RGBA32);
            if (surface && !png_image_finish_read(&png, NULL, surface->pixels, surface->pitch, NULL)) {
                SDL_FreeSurface(surface);
                surface = NULL;
            }
        }
        png_image_free(&png);
        return surface;
    }
    SDL_RWops *file = SDL_RWFromFile(path, "rb");
    if (!file) return NULL;
    unsigned char header[26];
    bool valid = SDL_RWread(file, header, 1, sizeof(header)) == sizeof(header) &&
                 header[0] == 'B' && header[1] == 'M';
    Uint32 dib = 0, width = 0, height = 0;
    if (valid) {
        memcpy(&dib, header + 14, 4); memcpy(&width, header + 18, 4); memcpy(&height, header + 22, 4);
        dib = SDL_SwapLE32(dib); width = SDL_SwapLE32(width); height = SDL_SwapLE32(height);
        int64_t signed_height = (int32_t)height;
        if (signed_height < 0) signed_height = -signed_height;
        valid = dib >= 40 && width > 0 && width <= 4096 && signed_height > 0 && signed_height <= 4096;
    }
    if (!valid || SDL_RWseek(file, 0, RW_SEEK_SET) < 0) { SDL_RWclose(file); return NULL; }
    return SDL_LoadBMP_RW(file, 1);
}

static void select_image(IntegralScreenshots *v)
{
    clear_image(v);
    v->status[0] = '\0';
    if (v->count) {
        v->image = load_image(v->entries[v->selected].path);
        if (!v->image) snprintf(v->status, sizeof(v->status), "LOAD FAILED");
    }
}

IntegralScreenshots *integral_screenshots_open(const char *root)
{
    IntegralScreenshots *v = calloc(1, sizeof(*v));
    if (!v) return NULL;
    const char *directories[] = {"screenshot", "runtime/n64_runtime", "runtime/n64_runtime_media"};
    for (unsigned i = 0; i < 3; i++) {
        char path[4096];
        if (join(path, sizeof(path), root, directories[i])) scan(v, path, i != 0);
    }
    if (v->count) {
        qsort(v->entries, v->count, sizeof(*v->entries), compare);
        v->selected = v->count - 1;
        select_image(v);
    }
    return v;
}

void integral_screenshots_close(IntegralScreenshots *v)
{
    if (!v) return;
    clear_image(v);
    for (size_t i = 0; i < v->count; i++) free(v->entries[i].path);
    free(v->entries);
    free(v);
}

bool integral_screenshots_key(IntegralScreenshots *v, const SDL_KeyboardEvent *event)
{
    if (!v || event->repeat) return false;
    SDL_Keycode key = event->keysym.sym;
    bool enter = key == SDLK_RETURN || key == SDLK_KP_ENTER;
    bool previous = key == SDLK_LEFT || key == SDLK_UP;
    bool next = key == SDLK_RIGHT || key == SDLK_DOWN;
    if (v->confirm_delete) {
        if (key == SDLK_ESCAPE) v->confirm_delete = false;
        else if (previous || next) v->confirm_yes = !v->confirm_yes;
        else if (enter) {
            if (v->confirm_yes && v->count) {
                struct stat info;
                const char *path = v->entries[v->selected].path;
                if (file_info(path, &info) && S_ISREG(info.st_mode) && remove(path) == 0) {
                    free(v->entries[v->selected].path);
                    memmove(v->entries + v->selected, v->entries + v->selected + 1,
                            (v->count - v->selected - 1) * sizeof(*v->entries));
                    v->count--;
                    if (v->selected >= v->count) v->selected = v->count ? v->count - 1 : 0;
                    select_image(v);
                    snprintf(v->status, sizeof(v->status), "DELETED");
                } else snprintf(v->status, sizeof(v->status), "DELETE FAILED");
            }
            v->confirm_delete = false;
        }
        return false;
    }
    if (key == SDLK_ESCAPE || enter) return true;
    if (!v->count) return false;
    if (previous || next) {
        v->selected = previous ? (v->selected + v->count - 1) % v->count : (v->selected + 1) % v->count;
        select_image(v);
    } else if (key == SDLK_DELETE || key == SDLK_BACKSPACE) {
        v->confirm_delete = true;
        v->confirm_yes = false;
    }
    return false;
}

SDL_Rect integral_screenshots_fit(int width, int height)
{
    SDL_Rect area = {24, 70, 432, 300};
    if (width <= 0 || height <= 0) return (SDL_Rect){0, 0, 0, 0};
    double scale = SDL_min((double)area.w / width, (double)area.h / height);
    if (scale >= 1) scale = (int)scale;
    int w = (int)(width * scale), h = (int)(height * scale);
    return (SDL_Rect){area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h};
}

void integral_screenshots_draw(SDL_Renderer *r, IntegralScreenshots *v)
{
    SDL_Color text = {238,238,220,255}, muted = {160,180,196,255}, green = {86,162,126,255};
    SDL_SetRenderDrawColor(r,20,24,28,255); SDL_RenderClear(r);
    integral_sdl_draw_text(r,22,20,"SCREENSHOTS",3,text);
    if (v && v->image) {
        if (!v->texture) v->texture = SDL_CreateTextureFromSurface(r, v->image);
        if (v->texture) {
            SDL_SetTextureScaleMode(v->texture, SDL_ScaleModeNearest);
            SDL_Rect rect = integral_screenshots_fit(v->image->w, v->image->h);
            SDL_RenderCopy(r, v->texture, NULL, &rect);
        }
    } else integral_sdl_draw_text(r,80,210,v && v->count ? "LOAD FAILED" : "NO SCREENSHOTS",2,muted);
    if (v && v->count) {
        char number[48]; snprintf(number,sizeof(number),"%zu / %zu",v->selected + 1,v->count);
        integral_sdl_draw_text(r,22,384,number,2,muted);
        const char *name = strrchr(v->entries[v->selected].path, '/');
        integral_sdl_draw_text_fit(r,22,408,name ? name + 1 : v->entries[v->selected].path,1,text,436);
    }
    if (v) integral_sdl_draw_text_fit(r,22,430,v->status,1,muted,436);
    integral_sdl_draw_text(r,22,452,"ARROWS CHANGE  DEL DELETE  ENTER/ESC BACK",1,muted);
    if (v && v->confirm_delete) {
        SDL_Rect panel = {48,160,384,150};
        SDL_SetRenderDrawColor(r,8,12,16,255); SDL_RenderFillRect(r,&panel);
        SDL_SetRenderDrawColor(r,86,162,126,255); SDL_RenderDrawRect(r,&panel);
        integral_sdl_draw_text(r,82,190,"DELETE IMAGE?",3,text);
        integral_sdl_draw_text(r,108,254,v->confirm_yes ? "> YES" : "  YES",2,green);
        integral_sdl_draw_text(r,270,254,v->confirm_yes ? "  NO" : "> NO",2,green);
    }
    SDL_RenderPresent(r);
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "gui/cart.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL.h>
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#endif

#include "transfer_pak/save_stage.h"

static bool regular_file(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode);
}

static bool has_gb_suffix(const char *name)
{
    size_t length = strlen(name);
    if (length >= 3u && SDL_strcasecmp(name + length - 3u, ".gb") == 0) return true;
    return length >= 4u && SDL_strcasecmp(name + length - 4u, ".gbc") == 0;
}

static void find_save(const char *root, const char *name, char *save,
                      size_t capacity)
{
    const char *dot = strrchr(name, '.');
    size_t stem_length = dot ? (size_t)(dot - name) : strlen(name);
    int length;
    save[0] = '\0';
    length = snprintf(save, capacity, "%s/%.*s.sav", root,
                      (int)stem_length, name);
    if (length > 0 && (size_t)length < capacity && regular_file(save)) return;
    length = snprintf(save, capacity, "%s/%.*s/%.*s.sav", root,
                      (int)stem_length, name, (int)stem_length, name);
    if (length > 0 && (size_t)length < capacity && regular_file(save)) return;
    save[0] = '\0';
}

static void scan_root(IntegralN64RuntimeCartCatalog *catalog, const char *root)
{
    DIR *directory;
    struct dirent *entry;
    if (!root || root[0] == '\0' || catalog->count >= INTEGRAL_N64_RUNTIME_CART_MAX) return;
    directory = opendir(root);
    if (!directory) return;
    while (catalog->count < INTEGRAL_N64_RUNTIME_CART_MAX &&
           (entry = readdir(directory)) != NULL) {
        char rom[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
        int length;
        if (!has_gb_suffix(entry->d_name)) continue;
        length = snprintf(rom, sizeof(rom), "%s/%s", root, entry->d_name);
        if (length <= 0 || (size_t)length >= sizeof(rom) || !regular_file(rom) ||
            integral_n64_runtime_cart_catalog_find(catalog, rom) >= 0) continue;
        memcpy(catalog->roms[catalog->count], rom, (size_t)length + 1u);
        find_save(root, entry->d_name, catalog->saves[catalog->count],
                  sizeof(catalog->saves[catalog->count]));
        ++catalog->count;
    }
    closedir(directory);
}

void integral_n64_runtime_cart_catalog_scan(IntegralN64RuntimeCartCatalog *catalog)
{
    const char *integral_gb_runtime_root;
    char integral_gb_runtime_roms[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    if (!catalog) return;
    memset(catalog, 0, sizeof(*catalog));
    scan_root(catalog, "roms");
    integral_gb_runtime_root = getenv("GB_RUNTIME_ROOT");
    if (integral_gb_runtime_root && integral_gb_runtime_root[0] != '\0') {
        int length = snprintf(integral_gb_runtime_roms, sizeof(integral_gb_runtime_roms),
                              "%s/roms", integral_gb_runtime_root);
        if (length > 0 && (size_t)length < sizeof(integral_gb_runtime_roms)) {
            scan_root(catalog, integral_gb_runtime_roms);
        }
    }
    else {
        scan_root(catalog, "../gb/roms");
    }
}

int integral_n64_runtime_cart_catalog_find(const IntegralN64RuntimeCartCatalog *catalog,
                                const char *rom_path)
{
    unsigned index;
    if (!catalog || !rom_path) return -1;
    for (index = 0u; index < catalog->count; ++index) {
        if (strcmp(catalog->roms[index], rom_path) == 0) return (int)index;
    }
    return -1;
}

const char *integral_n64_runtime_cart_name(const IntegralN64RuntimeCartCatalog *catalog,
                                unsigned choice)
{
    const char *slash;
    if (!catalog || choice == 0u || choice > catalog->count) return "OFF";
    slash = strrchr(catalog->roms[choice - 1u], '/');
    return slash ? slash + 1 : catalog->roms[choice - 1u];
}

static int copy_file(const char *source, const char *target)
{
    FILE *input = fopen(source, "rb");
    FILE *output = fopen(target, "wb");
    unsigned char buffer[16384];
    int result = -1;
    if (!input || !output) goto cleanup;
    for (;;) {
        size_t count = fread(buffer, 1u, sizeof(buffer), input);
        if (count > 0u && fwrite(buffer, 1u, count, output) != count) goto cleanup;
        if (count < sizeof(buffer)) {
            if (ferror(input)) goto cleanup;
            result = 0;
            break;
        }
    }
cleanup:
    if (input && fclose(input) != 0) result = -1;
    if (output && fclose(output) != 0) result = -1;
    return result;
}

static int create_save(const char *path, uint32_t size)
{
    FILE *file = fopen(path, "wb");
    unsigned char zeros[4096] = {0};
    if (!file) return -1;
    while (size > 0u) {
        size_t chunk = size > sizeof(zeros) ? sizeof(zeros) : (size_t)size;
        if (fwrite(zeros, 1u, chunk, file) != chunk) {
            fclose(file);
            return -1;
        }
        size -= (uint32_t)chunk;
    }
    return fclose(file) == 0 ? 0 : -1;
}

static bool marker_matches(const char *path, const char *rom)
{
    FILE *file = fopen(path, "r");
    char stored[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    size_t length;
    if (!file) return false;
    if (!fgets(stored, sizeof(stored), file)) {
        fclose(file);
        return false;
    }
    (void)fclose(file);
    length = strcspn(stored, "\r\n");
    stored[length] = '\0';
    return strcmp(stored, rom) == 0;
}

static int write_marker(const char *path, const char *rom)
{
    FILE *file = fopen(path, "w");
    int result;
    if (!file) return -1;
    result = fprintf(file, "%s\n", rom) > 0 ? 0 : -1;
    if (fclose(file) != 0) result = -1;
    return result;
}

static int replace_file(const char *source, const char *target)
{
#ifdef _WIN32
    return MoveFileExA(source, target,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? 0
               : -1;
#else
    return rename(source, target);
#endif
}

int integral_n64_runtime_cart_prepare_slot(const IntegralN64RuntimeCartCatalog *catalog,
                                unsigned choice, unsigned slot,
                                const char *storage)
{
    char rom_target[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char save_target[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char rom_temp[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char save_temp[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char marker_target[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char marker_temp[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    unsigned index;
    bool preserve_save;
    if (!catalog || !storage || slot < 1u || slot > 4u || choice > catalog->count) return -1;
    if (snprintf(rom_target, sizeof(rom_target), "%s/slot%u.gbc", storage, slot) <= 0 ||
        snprintf(save_target, sizeof(save_target), "%s/slot%u.sav", storage, slot) <= 0 ||
        snprintf(rom_temp, sizeof(rom_temp), "%s.prepare", rom_target) <= 0 ||
        snprintf(save_temp, sizeof(save_temp), "%s.prepare", save_target) <= 0 ||
        snprintf(marker_target, sizeof(marker_target), "%s/slot%u.source", storage, slot) <= 0 ||
        snprintf(marker_temp, sizeof(marker_temp), "%s.prepare", marker_target) <= 0) return -1;
    if (choice == 0u) {
        (void)remove(rom_target);
        (void)remove(rom_temp);
        (void)remove(save_temp);
        return 0;
    }
    index = choice - 1u;
    preserve_save = regular_file(save_target) &&
                    marker_matches(marker_target, catalog->roms[index]);
    if (copy_file(catalog->roms[index], rom_temp) != 0 ||
        (!preserve_save &&
         (catalog->saves[index][0] != '\0'
              ? copy_file(catalog->saves[index], save_temp)
              : create_save(save_temp,
                            transfer_pak_gb_ram_size(catalog->roms[index]))) != 0) ||
        write_marker(marker_temp, catalog->roms[index]) != 0 ||
        replace_file(rom_temp, rom_target) != 0 ||
        (!preserve_save && replace_file(save_temp, save_target) != 0) ||
        replace_file(marker_temp, marker_target) != 0) {
        (void)remove(rom_temp);
        (void)remove(save_temp);
        (void)remove(marker_temp);
        return -1;
    }
    return 0;
}

/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "gui/cart.h"

static int write_rom(const char *path)
{
    unsigned char rom[0x150] = {0};
    FILE *file;
    rom[0x149] = 3u;
    file = fopen(path, "wb");
    if (!file) return -1;
    if (fwrite(rom, 1u, sizeof(rom), file) != sizeof(rom)) {
        fclose(file);
        return -1;
    }
    return fclose(file) == 0 ? 0 : -1;
}

static long file_size(const char *path)
{
    struct stat info;
    return stat(path, &info) == 0 ? (long)info.st_size : -1L;
}

int main(int argc, char **argv)
{
    IntegralN64RuntimeCartCatalog catalog;
    char rom[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char save[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    char target[INTEGRAL_N64_RUNTIME_CART_PATH_MAX];
    FILE *file;
    if (argc != 2) {
        fputs("gui cart test: expected one storage argument\n", stderr);
        return 2;
    }
    memset(&catalog, 0, sizeof(catalog));
    snprintf(rom, sizeof(rom), "%s/source.gbc", argv[1]);
    snprintf(save, sizeof(save), "%s/source.sav", argv[1]);
    if (write_rom(rom) != 0) {
        fprintf(stderr, "gui cart test: failed to write %s\n", rom);
        return 1;
    }
    snprintf(catalog.roms[0], sizeof(catalog.roms[0]), "%s", rom);
    catalog.count = 1u;
    if (integral_n64_runtime_cart_prepare_slot(&catalog, 1u, 1u, argv[1]) != 0) {
        fputs("gui cart test: initial slot1 preparation failed\n", stderr);
        return 1;
    }
    snprintf(target, sizeof(target), "%s/slot1.sav", argv[1]);
    if (file_size(target) != 32768L) {
        fprintf(stderr, "gui cart test: slot1 save size is %ld\n", file_size(target));
        return 1;
    }

    file = fopen(save, "wb");
    if (!file || fwrite("saved", 1u, 5u, file) != 5u || fclose(file) != 0) {
        fputs("gui cart test: source save write failed\n", stderr);
        return 1;
    }
    snprintf(catalog.saves[0], sizeof(catalog.saves[0]), "%s", save);
    if (integral_n64_runtime_cart_prepare_slot(&catalog, 1u, 2u, argv[1]) != 0) {
        fputs("gui cart test: initial slot2 preparation failed\n", stderr);
        return 1;
    }
    snprintf(target, sizeof(target), "%s/slot2.sav", argv[1]);
    if (file_size(target) != 5L) {
        fprintf(stderr, "gui cart test: slot2 save size is %ld\n", file_size(target));
        return 1;
    }
    file = fopen(target, "ab");
    if (!file || fwrite("+", 1u, 1u, file) != 1u || fclose(file) != 0) {
        fputs("gui cart test: slot2 save append failed\n", stderr);
        return 1;
    }
    if (integral_n64_runtime_cart_prepare_slot(&catalog, 1u, 2u, argv[1]) != 0 ||
        file_size(target) != 6L) {
        fprintf(stderr, "gui cart test: repeated slot2 preparation failed, size=%ld\n",
                file_size(target));
        return 1;
    }
    if (integral_n64_runtime_cart_prepare_slot(&catalog, 0u, 2u, argv[1]) != 0 ||
        file_size(target) != 6L) {
        fprintf(stderr, "gui cart test: slot2 disable failed, size=%ld\n",
                file_size(target));
        return 1;
    }
    puts("N64 Runtime GUI cart preparation test passed");
    return 0;
}

/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_file_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    assert(argc == 2);
    char first[1024], second[1024], names[3][128], path[1200];
    assert(create_export_directory(argv[1], "20260916201305", first, sizeof(first)) == 0);
    assert(create_export_directory(argv[1], "20260916201305", second, sizeof(second)) == 0);
    assert(strcmp(first, second));
    assert(create_export_directory(argv[1], "20260916201305", path, 2) != 0);
    export_save_filename("poke_gin.gbc", 0, names[0], sizeof(names[0]));
    export_save_filename("poke_yellow.gbc", 1, names[1], sizeof(names[1]));
    export_save_filename("poke_gin.gbc", 7, names[2], sizeof(names[2]));
    assert(strcmp(names[0], names[2]));
    for (unsigned i = 0; i < 3; i++) {
        unsigned char data[4] = { (unsigned char)i, 5, 6, 7 }, restored[4];
        size_t size = 0;
        snprintf(path, sizeof(path), "%s/%s", first, names[i]);
        assert(write_binary_file(path, data, sizeof(data)) == 0);
        assert(read_binary_file(path, restored, sizeof(restored), &size) == 0);
        assert(size == sizeof(data) && !memcmp(data, restored, size));
    }
    puts("EXPORT: same-name slots, distinct directories and byte-for-byte readback PASS");
    return 0;
}

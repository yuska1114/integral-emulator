/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "transfer_pak/storage.h"

#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#define transfer_pak_mkdir(path) _mkdir(path)
#else
#define transfer_pak_mkdir(path) mkdir(path, 0755)
#endif

int transfer_pak_storage_replace(const char *temporary,
                                 const char *destination)
{
#ifdef _WIN32
    return MoveFileExA(temporary, destination,
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)
               ? 0
               : -1;
#else
    return rename(temporary, destination);
#endif
}

int transfer_pak_storage_ensure_directory(const char *path)
{
    return transfer_pak_mkdir(path) == 0 || errno == EEXIST ? 0 : -1;
}

int transfer_pak_storage_slot_path(const char *directory, uint32_t slot,
                                   const char *extension, char *output,
                                   size_t capacity)
{
    int length;
    if (directory == NULL || extension == NULL || output == NULL ||
        slot < 1u || slot > 4u) {
        return -1;
    }
    length = snprintf(output, capacity, "%s/slot%u.%s", directory,
                      (unsigned int)slot, extension);
    return length >= 0 && (size_t)length < capacity ? 0 : -1;
}

int transfer_pak_storage_file_size(const char *path, uint32_t *size)
{
    struct stat status;
    if (path == NULL || size == NULL || stat(path, &status) != 0 ||
        !S_ISREG(status.st_mode) || status.st_size < 0 ||
        (uint64_t)status.st_size > TRANSFER_PAK_STORAGE_MAX_FILE_SIZE) {
        return -1;
    }
    *size = (uint32_t)status.st_size;
    return 0;
}

int transfer_pak_storage_copy(FILE *input, FILE *output, uint32_t size)
{
    unsigned char buffer[16384];
    while (size > 0u) {
        size_t chunk = size > (uint32_t)sizeof(buffer)
                           ? sizeof(buffer)
                           : (size_t)size;
        if (fread(buffer, 1, chunk, input) != chunk ||
            fwrite(buffer, 1, chunk, output) != chunk) {
            return -1;
        }
        size -= (uint32_t)chunk;
    }
    return 0;
}

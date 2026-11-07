/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_rom_catalog.h"
#include "rom_metadata.h"

#ifdef _WIN32
#include "../runtimes/gb/src/common/utf8_file.h"
#else
#include <dirent.h>
#include <sys/stat.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static void copy_text(char *dest, size_t dest_size, const char *src)
{
    if (dest_size == 0) {
        return;
    }
    size_t len = strlen(src);
    if (len >= dest_size) {
        len = dest_size - 1;
    }
    memcpy(dest, src, len);
    dest[len] = '\0';
}

bool slot_is_supported_n64(const IntegralConfigRomSlot *slot)
{
    IntegralRomMetadata header;
    return slot && slot->rom_path[0] != '\0' &&
           integral_rom_metadata_read(slot->rom_path, &header) == 0 &&
           strcmp(header.platform, "n64") == 0;
}

bool slot_is_supported_gb(const IntegralConfigRomSlot *slot)
{
    IntegralRomMetadata header;
    return slot && slot->rom_path[0] != '\0' &&
           integral_rom_metadata_read(slot->rom_path, &header) == 0 &&
           strcmp(header.platform, "gb") == 0;
}

bool slot_has_server_registration(const IntegralConfigRomSlot *slot)
{
    return slot->rom_id[0] != '\0' && slot->save_id[0] != '\0';
}

bool slot_has_pending_local_rom(const IntegralConfigRomSlot *slot)
{
    return slot->rom_path[0] != '\0' && !slot_has_server_registration(slot) && !slot_is_supported_n64(slot);
}

bool discard_pending_rom_slots(IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS])
{
    bool changed = false;
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; i++) {
        if (slot_has_pending_local_rom(&slots[i])) {
            memset(&slots[i], 0, sizeof(slots[i]));
            changed = true;
        }
    }
    return changed;
}

void merge_server_rom_slots(IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS],
                            const IntegralApiRomSlot *server_slots, const char *rom_folder)
{
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; i++) {
        const IntegralApiRomSlot *server_slot = &server_slots[i];
        IntegralConfigRomSlot *slot = &slots[i];
        if (server_slot->rom_id[0] == '\0' || server_slot->save_id[0] == '\0') {
            if (!slot_is_supported_n64(slot)) {
                memset(slot, 0, sizeof(*slot));
            }
            continue;
        }
        copy_text(slot->rom_id, sizeof(slot->rom_id), server_slot->rom_id);
        copy_text(slot->save_id, sizeof(slot->save_id), server_slot->save_id);
        if (server_slot->filename[0] != '\0') {
            snprintf(slot->rom_path, sizeof(slot->rom_path), "%s/%s", rom_folder, server_slot->filename);
        }
    }
}

void validate_local_indices(const IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS],
                             int local_indices[2], int *room_index)
{
    for (unsigned i = 0; i < 2; i++) {
        int index = local_indices[i];
        if (index < 0 || index >= INTEGRAL_CONFIG_ROM_SLOTS || !slot_is_supported_gb(&slots[index])) {
            local_indices[i] = -1;
        }
    }
    if (local_indices[0] >= 0 &&
        local_indices[0] == local_indices[1]) {
        local_indices[1] = -1;
    }
    if ((*room_index) < 0 ||
        (*room_index) >= INTEGRAL_CONFIG_ROM_SLOTS ||
        !slot_is_supported_gb(&slots[(*room_index)])) {
        (*room_index) = -1;
    }
}

static bool rom_extension_matches(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    return strcmp(dot, ".gb") == 0 || strcmp(dot, ".gbc") == 0 || strcmp(dot, ".GB") == 0 ||
           strcmp(dot, ".GBC") == 0 || strcmp(dot, ".z64") == 0 || strcmp(dot, ".Z64") == 0 ||
           strcmp(dot, ".n64") == 0 || strcmp(dot, ".N64") == 0 || strcmp(dot, ".v64") == 0 ||
           strcmp(dot, ".V64") == 0;
}

int scan_rom_paths(const char *folder, char entries[][INTEGRAL_CONFIG_PATH_MAX], unsigned capacity)
{
#ifdef _WIN32
    char pattern[INTEGRAL_CONFIG_PATH_MAX];
    int length = snprintf(pattern, sizeof(pattern), "%s/*", folder);
    if (length < 0 || (size_t)length >= sizeof(pattern)) return -1;
    wchar_t *wide_pattern = integral_utf8_wide(pattern);
    if (!wide_pattern) return -1;
    WIN32_FIND_DATAW entry;
    HANDLE search = FindFirstFileW(wide_pattern, &entry);
    DWORD error = GetLastError();
    free(wide_pattern);
    if (search == INVALID_HANDLE_VALUE) return error == ERROR_FILE_NOT_FOUND ? 0 : -1;
    unsigned count = 0;
    while (count < capacity) {
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            char name[INTEGRAL_CONFIG_PATH_MAX];
            if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.cFileName,
                                     -1, name, sizeof(name), NULL, NULL)) {
                FindClose(search);
                return -1;
            }
            if (rom_extension_matches(name)) {
                length = snprintf(entries[count], sizeof(entries[count]), "%s/%s", folder, name);
                if (length < 0 || (size_t)length >= sizeof(entries[count])) {
                    FindClose(search);
                    return -1;
                }
                count++;
            }
        }
        if (!FindNextFileW(search, &entry)) {
            error = GetLastError();
            FindClose(search);
            return error == ERROR_NO_MORE_FILES ? (int)count : -1;
        }
    }
    FindClose(search);
    return (int)count;
#else
    DIR *dir = opendir(folder);
    if (!dir) return -1;
    unsigned count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < capacity) {
        if (!rom_extension_matches(entry->d_name)) continue;
        snprintf(entries[count], sizeof(entries[count]), "%s/%s", folder, entry->d_name);
        count++;
    }
    closedir(dir);
    return (int)count;
#endif
}

static bool local_gb_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".gb") == 0 || strcasecmp(dot, ".gbc") == 0);
}

static bool local_n64_extension(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && (strcasecmp(dot, ".z64") == 0 || strcasecmp(dot, ".n64") == 0 ||
                   strcasecmp(dot, ".v64") == 0);
}

static int local_gb_path_compare(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

static int scan_local_rom_directory(const char *folder, const char *relative, bool n64,
                                   char entries[][INTEGRAL_CONFIG_PATH_MAX], unsigned capacity,
                                   unsigned *count, unsigned depth)
{
    if (depth > 16) return 0;
    char directory[INTEGRAL_CONFIG_PATH_MAX];
    int length = relative[0] ? snprintf(directory, sizeof(directory), "%s/%s", folder, relative)
                             : snprintf(directory, sizeof(directory), "%s", folder);
    if (length < 0 || (size_t)length >= sizeof(directory)) return -1;
#ifdef _WIN32
    char pattern[INTEGRAL_CONFIG_PATH_MAX];
    length = snprintf(pattern, sizeof(pattern), "%s/*", directory);
    if (length < 0 || (size_t)length >= sizeof(pattern)) return -1;
    wchar_t *wide_pattern = integral_utf8_wide(pattern);
    if (!wide_pattern) return -1;
    WIN32_FIND_DATAW item;
    HANDLE search = FindFirstFileW(wide_pattern, &item);
    DWORD error = GetLastError();
    free(wide_pattern);
    if (search == INVALID_HANDLE_VALUE) return error == ERROR_FILE_NOT_FOUND ? 0 : -1;
    int result = 0;
    do {
        char name[INTEGRAL_CONFIG_PATH_MAX];
        if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, item.cFileName,
                                 -1, name, sizeof(name), NULL, NULL)) { result = -1; break; }
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        char child[INTEGRAL_CONFIG_PATH_MAX];
        length = relative[0] ? snprintf(child, sizeof(child), "%s/%s", relative, name)
                             : snprintf(child, sizeof(child), "%s", name);
        if (length < 0 || (size_t)length >= sizeof(child)) continue;
        if (item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;
        if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (scan_local_rom_directory(folder, child, n64, entries, capacity, count, depth + 1) != 0) { result = -1; break; }
        }
        else if ((n64 ? local_n64_extension(name) : local_gb_extension(name)) && *count < capacity) {
            char full[INTEGRAL_CONFIG_PATH_MAX];
            length = snprintf(full, sizeof(full), "%s/%s", folder, child);
            IntegralRomMetadata header;
            if (length > 0 && (size_t)length < sizeof(full) &&
                integral_rom_metadata_read(full, &header) == 0 && strcmp(header.platform, n64 ? "n64" : "gb") == 0) {
                copy_text(entries[(*count)++], INTEGRAL_CONFIG_PATH_MAX, child);
            }
        }
    } while (*count < capacity && FindNextFileW(search, &item));
    if (*count < capacity && result == 0 && GetLastError() != ERROR_NO_MORE_FILES) result = -1;
    FindClose(search);
    return result;
#else
    DIR *dir = opendir(directory);
    if (!dir) return -1;
    int result = 0;
    struct dirent *item;
    while (*count < capacity && (item = readdir(dir)) != NULL) {
        if (strcmp(item->d_name, ".") == 0 || strcmp(item->d_name, "..") == 0) continue;
        char child[INTEGRAL_CONFIG_PATH_MAX], full[INTEGRAL_CONFIG_PATH_MAX];
        length = relative[0] ? snprintf(child, sizeof(child), "%s/%s", relative, item->d_name)
                             : snprintf(child, sizeof(child), "%s", item->d_name);
        if (length < 0 || (size_t)length >= sizeof(child)) continue;
        length = snprintf(full, sizeof(full), "%s/%s", folder, child);
        if (length < 0 || (size_t)length >= sizeof(full)) continue;
        struct stat info;
        if (lstat(full, &info) != 0) continue;
        if (S_ISDIR(info.st_mode)) {
            if (scan_local_rom_directory(folder, child, n64, entries, capacity, count, depth + 1) != 0) { result = -1; break; }
        }
        else if (S_ISREG(info.st_mode) && (n64 ? local_n64_extension(item->d_name) : local_gb_extension(item->d_name))) {
            IntegralRomMetadata header;
            if (integral_rom_metadata_read(full, &header) == 0 && strcmp(header.platform, n64 ? "n64" : "gb") == 0) {
                copy_text(entries[(*count)++], INTEGRAL_CONFIG_PATH_MAX, child);
            }
        }
    }
    closedir(dir);
    return result;
#endif
}

int scan_local_gb_rom_paths(const char *folder, char entries[][INTEGRAL_CONFIG_PATH_MAX], unsigned capacity)
{
    unsigned count = 0;
    if (capacity == 0) return 0;
    if (scan_local_rom_directory(folder, "", false, entries, capacity, &count, 0) != 0) return -1;
    qsort(entries, count, sizeof(entries[0]), local_gb_path_compare);
    return (int)count;
}

int scan_local_n64_rom_paths(const char *folder, char entries[][INTEGRAL_CONFIG_PATH_MAX], unsigned capacity)
{
    unsigned count = 0;
    if (capacity == 0) return 0;
    if (scan_local_rom_directory(folder, "", true, entries, capacity, &count, 0) != 0) return -1;
    qsort(entries, count, sizeof(entries[0]), local_gb_path_compare);
    return (int)count;
}

void integral_rom_cycle_local(const IntegralConfigRomSlot *slots, int local_indices[2],
    const char *config_path, char *status, size_t status_size, unsigned slot_index, int delta)
{
    if (slot_index >= 2) {
        return;
    }
    int candidates[INTEGRAL_CONFIG_ROM_SLOTS];
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; i++) {
        if (slot_is_supported_gb(&slots[i]) &&
            local_indices[slot_index == 0 ? 1 : 0] != (int)i) {
            candidates[count++] = (int)i;
        }
    }
    if (count == 0) {
        copy_text(status, status_size, "NO ROM1-8 SET");
        return;
    }

    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if (local_indices[slot_index] == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)count - 1 : 0;
    }
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    local_indices[slot_index] = candidates[next];
    IntegralConfigLocal local = {
        .slot1_index = local_indices[0],
        .slot2_index = local_indices[1],
        .ir_off_delay_ticks = integral_config_ir_off_delay(config_path),
    };
    if (integral_config_save_local(config_path, &local) != 0) {
        copy_text(status, status_size, "GB SLOT CONFIG SAVE FAILED");
        return;
    }
    snprintf(status,
             status_size,
             "SLOT%u SELECTED ROM%d",
             slot_index + 1,
             local_indices[slot_index] + 1);
}

void integral_rom_cycle_n64(const IntegralConfigRomSlot *slots, int *n64_index,
    char *status, size_t status_size, int delta)
{
    int candidates[INTEGRAL_CONFIG_ROM_SLOTS];
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; i++) {
        if (slot_is_supported_n64(&slots[i])) {
            candidates[count++] = (int)i;
        }
    }
    if (count == 0) {
        copy_text(status, status_size, "NO N64 ROM SET");
        return;
    }
    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if ((*n64_index) == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)count - 1 : 0;
    }
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    (*n64_index) = candidates[next];
    snprintf(status,
             status_size,
             "N64 SELECTED ROM%d",
             (*n64_index) + 1);
}

void integral_rom_cycle_transfer(const IntegralConfigRomSlot *slots, int transfer_indices[4],
    char *status, size_t status_size, unsigned transfer_slot, int delta)
{
    if (transfer_slot >= 4) {
        return;
    }
    int candidates[INTEGRAL_CONFIG_ROM_SLOTS + 1];
    unsigned count = 0;
    candidates[count++] = -1;
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; i++) {
        bool already_selected = false;
        for (unsigned j = 0; j < 4; j++) {
            if (j != transfer_slot && transfer_indices[j] == (int)i) {
                already_selected = true;
                break;
            }
        }
        if (!already_selected && slot_has_server_registration(&slots[i]) && slot_is_supported_gb(&slots[i])) {
            candidates[count++] = (int)i;
        }
    }
    if (count <= 1) {
        copy_text(status, status_size, "NO GB/GBC ROM SET");
        return;
    }
    int current = 0;
    for (unsigned i = 0; i < count; i++) {
        if (transfer_indices[transfer_slot] == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    transfer_indices[transfer_slot] = candidates[next];
    if (candidates[next] < 0) {
        snprintf(status, status_size, "SLOT%u CLEARED", transfer_slot + 1);
    }
    else {
        snprintf(status,
                 status_size,
                 "SLOT%u SELECTED ROM%d",
                 transfer_slot + 1,
                 candidates[next] + 1);
    }
}

void integral_rom_cycle_room(const IntegralConfigRomSlot *slots, int *room_index,
    char *status, size_t status_size, int delta)
{
    int candidates[INTEGRAL_CONFIG_ROM_SLOTS];
    unsigned count = 0;
    for (unsigned i = 0; i < INTEGRAL_CONFIG_ROM_SLOTS; i++) {
        if (slot_is_supported_gb(&slots[i])) {
            candidates[count++] = (int)i;
        }
    }
    if (count == 0) {
        copy_text(status, status_size, "NO ROM1-8 SET");
        return;
    }

    int current = -1;
    for (unsigned i = 0; i < count; i++) {
        if ((*room_index) == candidates[i]) {
            current = (int)i;
            break;
        }
    }
    int next = current + delta;
    if (current < 0) {
        next = delta < 0 ? (int)count - 1 : 0;
    }
    if (next < 0) {
        next = (int)count - 1;
    }
    if (next >= (int)count) {
        next = 0;
    }
    (*room_index) = candidates[next];
    snprintf(status,
             status_size,
             "ROOM SLOT ROM%d",
             (*room_index) + 1);
}

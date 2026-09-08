/* SPDX-License-Identifier: GPL-2.0-or-later */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "transfer_pak/save_stage.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define TRANSFER_PAK_MBC3_RTC_TRAILER_SIZE 48u
#define TRANSFER_PAK_RTC_REASONABLE_EPOCH 852076800ull

uint32_t transfer_pak_gb_ram_size(const char *rom_path)
{
    FILE *file = fopen(rom_path, "rb");
    int code;
    if (file == NULL || fseek(file, 0x149L, SEEK_SET) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return 0u;
    }
    code = fgetc(file);
    (void)fclose(file);
    switch (code) {
    case 1: return 2u * 1024u;
    case 2: return 8u * 1024u;
    case 3: return 32u * 1024u;
    case 4: return 128u * 1024u;
    case 5: return 64u * 1024u;
    default: return 0u;
    }
}

static int gb_has_mbc3_rtc(const char *rom_path)
{
    FILE *file = fopen(rom_path, "rb");
    int code;
    if (file == NULL || fseek(file, 0x147L, SEEK_SET) != 0) {
        if (file != NULL) {
            (void)fclose(file);
        }
        return 0;
    }
    code = fgetc(file);
    (void)fclose(file);
    return code == 0x0f || code == 0x10;
}

static int copy_path(char *output, size_t capacity, const char *path)
{
    size_t length = strlen(path);
    if (length >= capacity) {
        return -1;
    }
    memcpy(output, path, length + 1u);
    return 0;
}

static int copy_range_atomic(const char *source_path, uint32_t offset,
                             const char *target_path, uint32_t size)
{
    char temporary[TRANSFER_PAK_STORAGE_PATH_CAPACITY + 16u];
    FILE *input = NULL;
    FILE *output = NULL;
    int length = snprintf(temporary, sizeof(temporary), "%s.part", target_path);
    int result = -1;
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        return -1;
    }
    input = fopen(source_path, "rb");
    output = fopen(temporary, "wb");
    if (input != NULL && output != NULL &&
        fseek(input, (long)offset, SEEK_SET) == 0 &&
        transfer_pak_storage_copy(input, output, size) == 0) {
        int close_failed = fclose(input) != 0;
        input = NULL;
        close_failed |= fclose(output) != 0;
        output = NULL;
        if (!close_failed &&
            transfer_pak_storage_replace(temporary, target_path) == 0) {
            result = 0;
        }
    }
    if (input != NULL) (void)fclose(input);
    if (output != NULL) (void)fclose(output);
    if (result != 0) (void)remove(temporary);
    return result;
}

static void put_le64(unsigned char *output, uint64_t value)
{
    int index;
    for (index = 0; index < 8; ++index) {
        output[index] = (unsigned char)((value >> (unsigned int)(index * 8)) &
                                        0xffu);
    }
}

static uint64_t get_le64(const unsigned char *input)
{
    uint64_t value = 0u;
    int index;
    for (index = 7; index >= 0; --index) {
        value = (value << 8u) | input[index];
    }
    return value;
}

static int rtc_time_fields_valid(const unsigned char *data)
{
    return data[0] < 60u && data[4] < 60u && data[8] < 24u &&
           (data[16] & 0x3eu) == 0u;
}

static int rtc_trailer_valid(const unsigned char *data)
{
    uint64_t now = (uint64_t)time(NULL);
    uint64_t last = get_le64(data + 40u);
    return rtc_time_fields_valid(data) && rtc_time_fields_valid(data + 20u) &&
           last >= TRANSFER_PAK_RTC_REASONABLE_EPOCH &&
           (now == (uint64_t)-1 || last <= now);
}

static int write_synthetic_mbc3_rtc(const char *target_path)
{
    unsigned char trailer[TRANSFER_PAK_MBC3_RTC_TRAILER_SIZE];
    char temporary[TRANSFER_PAK_STORAGE_PATH_CAPACITY + 16u];
    FILE *output;
    time_t now;
    struct tm tm_now;
    int length;

    memset(trailer, 0, sizeof(trailer));
    now = time(NULL);
    if (now == (time_t)-1) {
        return -1;
    }
#ifdef _WIN32
    if (localtime_s(&tm_now, &now) != 0) {
        return -1;
    }
#else
    if (localtime_r(&now, &tm_now) == NULL) {
        return -1;
    }
#endif
    trailer[0] = (unsigned char)tm_now.tm_sec;
    trailer[4] = (unsigned char)tm_now.tm_min;
    trailer[8] = (unsigned char)tm_now.tm_hour;
    trailer[12] = (unsigned char)(tm_now.tm_yday & 0xff);
    trailer[16] = (unsigned char)((tm_now.tm_yday >> 8) & 0x01);
    memcpy(trailer + 20u, trailer, 20u);
    put_le64(trailer + 40u, (uint64_t)now);

    length = snprintf(temporary, sizeof(temporary), "%s.part", target_path);
    if (length < 0 || (size_t)length >= sizeof(temporary)) {
        return -1;
    }
    output = fopen(temporary, "wb");
    if (output == NULL ||
        fwrite(trailer, 1, sizeof(trailer), output) != sizeof(trailer) ||
        fclose(output) != 0) {
        if (output != NULL) (void)fclose(output);
        (void)remove(temporary);
        return -1;
    }
    if (transfer_pak_storage_replace(temporary, target_path) != 0) {
        (void)remove(temporary);
        return -1;
    }
    return 0;
}

static int repair_mbc3_rtc_if_needed(const char *rtc_path)
{
    FILE *file = fopen(rtc_path, "rb");
    unsigned char trailer[TRANSFER_PAK_MBC3_RTC_TRAILER_SIZE];
    int valid = 0;
    if (file != NULL) {
        valid = fread(trailer, 1, sizeof(trailer), file) == sizeof(trailer) &&
                fclose(file) == 0 && rtc_trailer_valid(trailer);
        file = NULL;
    }
    if (file != NULL) (void)fclose(file);
    return valid ? 0 : write_synthetic_mbc3_rtc(rtc_path);
}

int transfer_pak_save_stage_prepare(TransferPakSaveStage *stage, const char *rom_path,
                             const char *save_path, char *mupen_path,
                             size_t mupen_path_capacity)
{
    uint32_t save_size;
    uint32_t ram_size;
    int length;
    if (stage == NULL || rom_path == NULL || save_path == NULL ||
        mupen_path == NULL) {
        return -1;
    }
    memset(stage, 0, sizeof(*stage));
    ram_size = transfer_pak_gb_ram_size(rom_path);
    if (transfer_pak_storage_file_size(save_path, &save_size) != 0) {
        return -1;
    }
    if (ram_size == 0u || save_size <= ram_size) {
        if (save_size == ram_size && gb_has_mbc3_rtc(rom_path)) {
            stage->ram_size = ram_size;
            stage->original_size = save_size;
            stage->merged_size = save_size + TRANSFER_PAK_MBC3_RTC_TRAILER_SIZE;
            if (copy_path(stage->original_path, sizeof(stage->original_path),
                          save_path) != 0) {
                return -1;
            }
            length = snprintf(stage->working_path, sizeof(stage->working_path),
                              "%s.mupen", save_path);
            if (length < 0 || (size_t)length >= sizeof(stage->working_path) ||
                copy_range_atomic(save_path, 0u, stage->working_path,
                                  ram_size) != 0) {
                transfer_pak_save_stage_discard(stage);
                return -1;
            }
            length = snprintf(stage->rtc_path, sizeof(stage->rtc_path),
                              "%s.rtc", stage->working_path);
            if (length < 0 || (size_t)length >= sizeof(stage->rtc_path) ||
                write_synthetic_mbc3_rtc(stage->rtc_path) != 0) {
                transfer_pak_save_stage_discard(stage);
                return -1;
            }
            stage->active = 1;
            if (copy_path(mupen_path, mupen_path_capacity,
                          stage->working_path) != 0) {
                transfer_pak_save_stage_discard(stage);
                return -1;
            }
            printf("N64 Runtime: synthesized missing MBC3 RTC trailer for %s\n",
                   save_path);
            fflush(stdout);
            return 0;
        }
        return copy_path(mupen_path, mupen_path_capacity, save_path);
    }
    stage->ram_size = ram_size;
    stage->original_size = save_size;
    stage->merged_size = save_size;
    if (copy_path(stage->original_path, sizeof(stage->original_path),
                  save_path) != 0) {
        return -1;
    }
    length = snprintf(stage->working_path, sizeof(stage->working_path),
                      "%s.mupen", save_path);
    if (length < 0 || (size_t)length >= sizeof(stage->working_path) ||
        copy_range_atomic(save_path, 0u, stage->working_path, ram_size) != 0) {
        transfer_pak_save_stage_discard(stage);
        return -1;
    }
    if (save_size - ram_size == TRANSFER_PAK_MBC3_RTC_TRAILER_SIZE) {
        length = snprintf(stage->rtc_path, sizeof(stage->rtc_path), "%s.rtc",
                          stage->working_path);
        if (length < 0 || (size_t)length >= sizeof(stage->rtc_path) ||
            copy_range_atomic(save_path, ram_size, stage->rtc_path,
                              TRANSFER_PAK_MBC3_RTC_TRAILER_SIZE) != 0 ||
            (gb_has_mbc3_rtc(rom_path) &&
             repair_mbc3_rtc_if_needed(stage->rtc_path) != 0)) {
            transfer_pak_save_stage_discard(stage);
            return -1;
        }
    }
    stage->active = 1;
    if (copy_path(mupen_path, mupen_path_capacity, stage->working_path) != 0) {
        transfer_pak_save_stage_discard(stage);
        return -1;
    }
    return 0;
}

int transfer_pak_save_stage_finalize(TransferPakSaveStage *stage)
{
    char temporary[TRANSFER_PAK_STORAGE_PATH_CAPACITY + 24u];
    uint32_t working_size;
    uint32_t original_size;
    uint32_t rtc_size = 0u;
    FILE *working = NULL;
    FILE *original = NULL;
    FILE *trailer = NULL;
    FILE *output = NULL;
    int length;
    int close_failed;
    if (stage == NULL || !stage->active) {
        return 0;
    }
    length = snprintf(temporary, sizeof(temporary), "%s.merge.part",
                      stage->original_path);
    if (length < 0 || (size_t)length >= sizeof(temporary) ||
        transfer_pak_storage_file_size(stage->working_path, &working_size) != 0 ||
        transfer_pak_storage_file_size(stage->original_path, &original_size) != 0 ||
        working_size < stage->ram_size || original_size != stage->original_size) {
        return -1;
    }
    working = fopen(stage->working_path, "rb");
    original = fopen(stage->original_path, "rb");
    output = fopen(temporary, "wb");
    if (stage->rtc_path[0] != '\0' &&
        transfer_pak_storage_file_size(stage->rtc_path, &rtc_size) == 0 &&
        rtc_size == stage->merged_size - stage->ram_size) {
        trailer = fopen(stage->rtc_path, "rb");
    }
    if (working == NULL || original == NULL || output == NULL ||
        transfer_pak_storage_copy(working, output, stage->ram_size) != 0 ||
        (trailer == NULL &&
         fseek(original, (long)stage->ram_size, SEEK_SET) != 0) ||
        transfer_pak_storage_copy(trailer != NULL ? trailer : original, output,
                           stage->merged_size - stage->ram_size) != 0) {
        if (working != NULL) (void)fclose(working);
        if (original != NULL) (void)fclose(original);
        if (trailer != NULL) (void)fclose(trailer);
        if (output != NULL) (void)fclose(output);
        (void)remove(temporary);
        return -1;
    }
    close_failed = fclose(working) != 0;
    close_failed |= fclose(original) != 0;
    if (trailer != NULL) close_failed |= fclose(trailer) != 0;
    close_failed |= fclose(output) != 0;
    if (close_failed ||
        transfer_pak_storage_replace(temporary, stage->original_path) != 0) {
        (void)remove(temporary);
        return -1;
    }
    transfer_pak_save_stage_discard(stage);
    return 0;
}

void transfer_pak_save_stage_discard(TransferPakSaveStage *stage)
{
    if (stage == NULL) {
        return;
    }
    if (stage->working_path[0] != '\0') (void)remove(stage->working_path);
    if (stage->rtc_path[0] != '\0') (void)remove(stage->rtc_path);
    memset(stage, 0, sizeof(*stage));
}

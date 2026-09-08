/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "transfer_pak/save_stage.h"
#include "transfer_pak/media_session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RAM_SIZE (64u * 1024u)
#define SMALL_RAM_SIZE (32u * 1024u)
#define RTC_SIZE 48u

static int write_zeros(FILE *file, uint32_t size)
{
    unsigned char zeros[1024] = {0};
    while (size > 0u) {
        size_t chunk = size > (uint32_t)sizeof(zeros)
                           ? sizeof(zeros)
                           : (size_t)size;
        if (fwrite(zeros, 1, chunk, file) != chunk) return -1;
        size -= (uint32_t)chunk;
    }
    return 0;
}

static int create_inputs(const char *rom_path, const char *save_path)
{
    static const char trailer[RTC_SIZE] =
        "RTC_TRAILER_12345678901234567890123456789012345";
    FILE *rom = fopen(rom_path, "wb");
    FILE *save = fopen(save_path, "wb");
    int result = -1;
    if (rom != NULL && save != NULL && write_zeros(rom, 0x150u) == 0 &&
        fseek(rom, 0x149L, SEEK_SET) == 0 && fputc(5, rom) == 5 &&
        write_zeros(save, RAM_SIZE) == 0 &&
        fwrite(trailer, 1, sizeof(trailer), save) == sizeof(trailer)) {
        int close_failed = fclose(rom) != 0;
        rom = NULL;
        close_failed |= fclose(save) != 0;
        save = NULL;
        if (!close_failed) result = 0;
    }
    if (rom != NULL) (void)fclose(rom);
    if (save != NULL) (void)fclose(save);
    return result;
}

static int create_ram_only_mbc3_rtc_inputs(const char *rom_path,
                                           const char *save_path)
{
    FILE *rom = fopen(rom_path, "wb");
    FILE *save = fopen(save_path, "wb");
    int result = -1;
    if (rom != NULL && save != NULL && write_zeros(rom, 0x150u) == 0 &&
        fseek(rom, 0x147L, SEEK_SET) == 0 && fputc(0x10, rom) == 0x10 &&
        fseek(rom, 0x149L, SEEK_SET) == 0 && fputc(3, rom) == 3 &&
        write_zeros(save, SMALL_RAM_SIZE) == 0) {
        int close_failed = fclose(rom) != 0;
        rom = NULL;
        close_failed |= fclose(save) != 0;
        save = NULL;
        if (!close_failed) result = 0;
    }
    if (rom != NULL) (void)fclose(rom);
    if (save != NULL) (void)fclose(save);
    return result;
}

static int overwrite(const char *path, long offset, const void *data, size_t size)
{
    FILE *file = fopen(path, "r+b");
    int result = file != NULL && fseek(file, offset, SEEK_SET) == 0 &&
                         fwrite(data, 1, size, file) == size &&
                         fclose(file) == 0
                     ? 0
                     : -1;
    return result;
}

int main(int argc, char **argv)
{
    static const char changed_ram[] = "save-from-emulator";
    static const unsigned char changed_rtc[4] = {0x11, 0x00, 0x00, 0x00};
    char rom_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    char save_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    char mupen_path[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    unsigned char check[sizeof(changed_ram) - 1u];
    unsigned char rtc_check[sizeof(changed_rtc)];
    uint32_t final_size;
    TransferPakSaveStage stage;
    TransferPakMediaSession media;
    char *owned_rom;
    char *owned_ram;
    FILE *save;
    if (argc != 2 ||
        snprintf(rom_path, sizeof(rom_path), "%s/slot1.gbc", argv[1]) >=
            (int)sizeof(rom_path) ||
        snprintf(save_path, sizeof(save_path), "%s/slot1.sav", argv[1]) >=
            (int)sizeof(save_path) ||
        create_inputs(rom_path, save_path) != 0 ||
        transfer_pak_gb_ram_size(rom_path) != RAM_SIZE ||
        transfer_pak_save_stage_prepare(&stage, rom_path, save_path, mupen_path,
                                 sizeof(mupen_path)) != 0 ||
        !stage.active ||
        overwrite(mupen_path, 0L, changed_ram, sizeof(changed_ram) - 1u) != 0 ||
        overwrite(stage.rtc_path, 0L, changed_rtc, sizeof(changed_rtc)) != 0 ||
        transfer_pak_save_stage_finalize(&stage) != 0 ||
        transfer_pak_storage_file_size(save_path, &final_size) != 0 ||
        final_size != RAM_SIZE + RTC_SIZE) {
        fprintf(stderr, "MBC3 save-stage test failed\n");
        return 1;
    }
    save = fopen(save_path, "rb");
    if (save == NULL ||
        fread(check, 1, sizeof(check), save) != sizeof(check) ||
        memcmp(check, changed_ram, sizeof(check)) != 0 ||
        fseek(save, (long)RAM_SIZE, SEEK_SET) != 0 ||
        fread(rtc_check, 1, sizeof(rtc_check), save) != sizeof(rtc_check) ||
        memcmp(rtc_check, changed_rtc, sizeof(rtc_check)) != 0 ||
        fclose(save) != 0) {
        fprintf(stderr, "MBC3 merged save differs\n");
        return 1;
    }
    if (transfer_pak_media_session_prepare_mask(&media, argv[1], 0u) != 0 ||
        transfer_pak_media_session_ready(&media, 0) ||
        transfer_pak_media_session_prepare_mask(&media, argv[1], 1u) != 0 ||
        !transfer_pak_media_session_ready(&media, 0)) {
        fprintf(stderr, "Transfer Pak slot-mask test failed\n");
        return 1;
    }
    owned_rom = transfer_pak_media_session_get_rom(&media, 0);
    owned_ram = transfer_pak_media_session_get_ram(&media, 0);
    if (owned_rom == NULL || owned_ram == NULL ||
        strcmp(owned_rom, rom_path) != 0 || strcmp(owned_ram, mupen_path) != 0) {
        free(owned_rom);
        free(owned_ram);
        transfer_pak_media_session_discard(&media);
        fprintf(stderr, "Transfer Pak owned-path callback test failed\n");
        return 1;
    }
    free(owned_rom);
    free(owned_ram);
    if (transfer_pak_media_session_finalize(&media) != 0) {
        fprintf(stderr, "Transfer Pak media finalization failed\n");
        return 1;
    }
    if (snprintf(rom_path, sizeof(rom_path), "%s/slot2.gbc", argv[1]) >=
            (int)sizeof(rom_path) ||
        snprintf(save_path, sizeof(save_path), "%s/slot2.sav", argv[1]) >=
            (int)sizeof(save_path) ||
        create_ram_only_mbc3_rtc_inputs(rom_path, save_path) != 0 ||
        transfer_pak_gb_ram_size(rom_path) != SMALL_RAM_SIZE ||
        transfer_pak_save_stage_prepare(&stage, rom_path, save_path, mupen_path,
                                 sizeof(mupen_path)) != 0 ||
        !stage.active || stage.rtc_path[0] == '\0' ||
        transfer_pak_save_stage_finalize(&stage) != 0 ||
        transfer_pak_storage_file_size(save_path, &final_size) != 0 ||
        final_size != SMALL_RAM_SIZE + RTC_SIZE) {
        fprintf(stderr, "MBC3 missing-RTC synthesis test failed\n");
        return 1;
    }
    puts("MBC3 RAM/RTC save-stage test passed");
    return 0;
}

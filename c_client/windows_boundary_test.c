/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "windows_process.h"
#include "rom_metadata.h"
#include "client_file_io.h"
#include <assert.h>
#include <string.h>
#ifdef _WIN32
#define WinMain launcher_test_entry
#define ensure_directory launcher_ensure_directory
#include "windows_launcher.c"
#undef ensure_directory
#undef WinMain

static void packaged_paths_test(void)
{
    const wchar_t *roots[] = {L"C:\\Integral Emulator", L"C:\\Users\\tester\\OneDrive\\デスクトップ\\Integral Emulator"};
    const char *names[] = {"INTEGRAL_EMULATOR_GB_RUNTIME_DUAL_SERVER", "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RUNTIME", "INTEGRAL_EMULATOR_GB_RUNTIME_MOBILE_RUNTIME"};
    const char *paths[] = {"runtimes\\gb\\integral_gb_runtime_dual_server.exe", "runtimes\\gb\\integral_gb_runtime_fixed_host.exe", "runtimes\\gb\\integral_gb_runtime_mobile_runtime.exe"};
    for (unsigned r=0;r<sizeof(roots)/sizeof(*roots);r++) {
        assert(set_packaged_environment(roots[r]));
        for (unsigned i=0;i<3;i++) {
            char value[256];
            assert(GetEnvironmentVariableA(names[i],value,sizeof(value))>0);
            assert(!strcmp(value,paths[i]));
        }
    }
    puts("PASS packaged GB paths remain ASCII-relative for spaces/Japanese/OneDrive roots");
}
static const char *values[] = {"", "Pokemon Gold.gbc", "Left Shift", "a\"b",
    "C:\\folder with space\\", "back\\\\\"quote", "銀 試験.gbc", NULL};

int wmain(int argc, wchar_t **argv)
{
    if (argc > 1) {
        assert(argc == 8);
        for (int i = 1; i < argc; i++) {
            wchar_t *expected = integral_utf8_wide(values[i - 1]);
            assert(expected && wcscmp(expected, argv[i]) == 0);
            free(expected);
        }
        return 0;
    }
    packaged_paths_test();
    const char *path = "銀 試験.gbc";
    unsigned char bytes[32768] = {0};
    bytes[0x100] = 0xc3; bytes[0x101] = 0x50; bytes[0x102] = 0x01;
    bytes[0x150] = 0x18; bytes[0x151] = 0xfe;
    memcpy(bytes + 0x134, "BOUNDARY TEST", 13);
    for (size_t i = 0x134; i <= 0x14c; i++) bytes[0x14d] -= bytes[i] + 1;
    FILE *file = integral_fopen(path, "wb");
    assert(file && fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0 && integral_file_regular(path));
    assert(integral_access(path, 4) == 0);
    IntegralRomMetadata metadata;
    assert(integral_rom_metadata_read(path, &metadata) == 0);
    assert(strcmp(metadata.header_title, "BOUNDARY TEST") == 0);
    unsigned char *loaded = NULL;
    size_t loaded_size = 0;
    assert(read_binary_file_alloc(path, &loaded, &loaded_size, sizeof(bytes)) == 0);
    assert(loaded_size == sizeof(bytes) && memcmp(loaded, bytes, sizeof(bytes)) == 0);
    free(loaded);
    file = integral_fopen("初期 SAV.sav", "wb");
    assert(file && fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0);
    assert(read_binary_file_alloc("初期 SAV.sav", &loaded, &loaded_size, sizeof(bytes)) == 0);
    assert(loaded_size == sizeof(bytes) && memcmp(loaded, bytes, sizeof(bytes)) == 0);
    free(loaded);
    assert(_wremove(L"初期 SAV.sav") == 0);
    const char *runtime = getenv("INTEGRAL_TEST_GB_RUNTIME");
    if (runtime) {
        const char *runtime_args[] = {runtime, "--self", "--rom1", path,
            "--save1", "boundary-runtime.sav", "--frames", "2", "--no-audio",
            "--fast-key", "Left Shift", NULL};
        assert(integral_windows_spawnv(_P_WAIT, runtime, runtime_args) == 0);
        puts("PASS GB Runtime Unicode ROM and spaced key binding");
    }
    assert(!integral_fopen("\xff", "rb"));
    assert(!integral_file_regular("missing.gbc"));
    assert(_wremove(L"銀 試験.gbc") == 0);
    wchar_t exe[32768];
    assert(GetModuleFileNameW(NULL, exe, 32768) > 0);
    assert(CopyFileW(exe, L"子 process.exe", FALSE));
    const char *args[9] = {"子 process.exe"};
    for (int i = 0; i < 7; i++) args[i + 1] = values[i];
    assert(integral_windows_spawnv(_P_WAIT, "子 process.exe", args) == 0);
    assert(DeleteFileW(L"子 process.exe"));
    puts("PASS Windows Unicode files and CRT argv round trip");
    return 0;
}
#endif

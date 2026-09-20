/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_gb_mobile.h"
#include "http_client.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

static char marker_path[2048], redirect_path[2048];
static char *active_status;
static unsigned expected_exit, cancelled, cleaned, logged;

int integral_api_cancel_mobile_session(const char *server, const char *token,
                                       const char *mobile, const char *game,
                                       long long fence, const char *reason,
                                       char *error, size_t error_size)
{
    assert(!strcmp(server, "test-server") && !strcmp(token, "test-token"));
    assert(!strcmp(mobile, "mobile-test") && !strcmp(game, "game-test") && fence == 41);
    assert(!strcmp(reason, "runtime start failed"));
    assert(!cleaned && !logged && !strcmp(active_status, "before start"));
    (void)error; (void)error_size; cancelled++;
    return 0;
}

static bool cleanup(const char *directory)
{
    assert(!strcmp(directory, "runtime/mobile-test"));
    assert(cancelled == 1 && !logged && !strcmp(active_status, "before start"));
    cleaned++;
    return true; /* No real files/SAV are removed. */
}

static void write_marker(const char *path)
{
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fputs("checked", f) >= 0 && fclose(f) == 0);
}

static void redirect_output(void)
{
    write_marker(redirect_path); /* Called in the POSIX Runtime child only. */
}

static void check_handoff(const char *server, const char *token, const char *game,
                           long long fence, const LocalSyncSlot *slots,
                           unsigned count, bool active)
{
    assert(!strcmp(server, "test-server") && !strcmp(token, "test-token"));
    assert(!strcmp(game, "game-test") && fence == 41 && count == 1 && active);
    assert(slots[0].mobile_guard && slots[0].authoritative_size == 32768);
    assert(!strcmp(slots[0].mobile_session_id, "mobile-test"));
    assert(!strcmp(slots[0].mobile_runtime_dir, "runtime/mobile-test"));
    assert(!strcmp(slots[0].mobile_result_path, "runtime/mobile-test/result.txt"));
    assert(!strcmp(slots[0].save_id, "mobile-save") && slots[0].revision == 23);
    assert(!strcmp(slots[0].last_hash, "test-hash"));
    assert(!strcmp(slots[0].save_path, "runtime/mobile-test/working.sav"));
}

#ifdef _WIN32
void start_save_sync_thread(intptr_t process, const char *server, const char *token,
                            const char *game, long long fence, const LocalSyncSlot *slots,
                            unsigned count, bool active)
{
    check_handoff(server, token, game, fence, slots, count, active);
    assert(!logged && !strcmp(active_status, "before start"));
    assert(WaitForSingleObject((HANDLE)process, 10000) == WAIT_OBJECT_0);
    DWORD code; assert(GetExitCodeProcess((HANDLE)process, &code) && code == expected_exit);
    assert(CloseHandle((HANDLE)process));
    write_marker(marker_path);
}
#else
void monitor_save_sync_process(IntegralChildProcess process, const char *server,
                               const char *token, const char *game, long long fence,
                               LocalSyncSlot *slots, unsigned count, bool active)
{
    check_handoff(server, token, game, fence, slots, count, active);
    int status; assert(waitpid(process, &status, 0) == process);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == (int)expected_exit);
    FILE *f = fopen(redirect_path, "rb"); assert(f); fclose(f);
    write_marker(marker_path);
}
#endif

static void observe(void *context, const char *event, const char *detail)
{
    assert(context == active_status && !strcmp(active_status, "MOBILE MODE STARTED"));
    assert(!strcmp(event, "gb_mobile_started"));
    assert(!strcmp(detail, "mobile_session=mobile-test game_session=game-test scenario=scenario-test"));
    assert(!cancelled && !cleaned); logged++;
}

static void test_process(IntegralGbMobileLaunch *launch, const char *self, const char *directory)
{
    LocalSyncSlot slot = {0};
    strcpy(slot.save_id, "mobile-save"); slot.revision = 23;
    strcpy(slot.last_hash, "test-hash");
    strcpy(slot.save_path, "runtime/mobile-test/working.sav");
    strcpy(slot.mobile_session_id, "mobile-test");
    strcpy(slot.mobile_runtime_dir, "runtime/mobile-test");
    strcpy(slot.mobile_result_path, "runtime/mobile-test/result.txt");
    slot.mobile_guard = true; slot.authoritative_size = 32768;
    IntegralGbMobileSession session = {
        .server = "test-server", .token = "test-token", .game_session_id = "game-test",
        .mobile_session_id = "mobile-test", .fencing_token = 41,
        .runtime_dir = "runtime/mobile-test", .scenario_id = "scenario-test", .sync_slot = &slot,
    };
    char status[160]; active_status = status;
    for (unsigned run = 0; run < 2; ++run) {
        snprintf(marker_path, sizeof(marker_path), "%s/monitor-%u", directory, run);
        snprintf(redirect_path, sizeof(redirect_path), "%s/redirect-%u", directory, run);
        strcpy(status, "before start"); cancelled = cleaned = logged = 0;
        launch->runtime = run ? "./missing-mobile-test-runtime" : self;
        expected_exit = run ? 127 : 42;
        integral_gb_mobile_start(launch, &session, status, sizeof(status), cleanup, redirect_output, observe, status);
#ifdef _WIN32
        if (run) {
            assert(cancelled == 1 && cleaned == 1 && !logged);
            assert(!strcmp(status, "MOBILE START FAILED"));
            continue;
        }
#else
        int s; assert(waitpid(-1, &s, 0) > 0); /* The sole monitor child of this test. */
        assert(WIFEXITED(s) && WEXITSTATUS(s) == 0);
#endif
        assert(logged == 1 && !cancelled && !cleaned);
        FILE *f = fopen(marker_path, "rb"); assert(f); fclose(f);
    }
    puts("GB Mobile process: guarded monitor handoff, exit, failure ordering PASS");
}

int main(int argc, char **argv_in)
{
    if (argc > 1 && !strcmp(argv_in[1], "--rom")) return 42;
    assert(argc == 2);
    IntegralConfigKeys keys = {0};
    memset(keys.slot1, 'k', sizeof(keys.slot1) - 1);
    strcpy(keys.slot2, "MUST_NOT_BE_USED");
    strcpy(keys.fast, "Tab"); strcpy(keys.screenshot, "F12");
    strcpy(keys.escape, "Escape"); strcpy(keys.turbo_hold, "Space");
    strcpy(keys.reset, "F1");
    char long_path[INTEGRAL_CONFIG_PATH_MAX];
    memset(long_path, 'r', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = 0;
    IntegralGbMobileLaunch launch = {
        .runtime = "runtime dir/mobile", .rom = "roms/日本語 name.gbc",
        .save = "session/working.sav", .adapter_config = "session/adapter.bin",
        .manifest = "session/session.manifest", .result = "session/runtime-result.txt",
        .rtc_offset = "-12345", .window_width = "360", .window_height = "721",
        .keys = &keys,
    };
    /* Frozen order from both former spawnl/execl branches, not an emulator run. */
    const char *expected[] = {
        launch.runtime,
        "--rom",
        launch.rom,
        "--save",
        launch.save,
        "--config",
        launch.adapter_config,
        "--session-manifest",
        launch.manifest,
        "--runtime-result",
        launch.result,
        "--rtc-offset-seconds",
        launch.rtc_offset,
        "--window-width",
        launch.window_width,
        "--window-height",
        launch.window_height,
        "--slot1-keys",
        keys.slot1,
        "--fast-key",
        keys.fast,
        "--screenshot-key",
        keys.screenshot,
        "--escape-key",
        keys.escape,
        "--turbo-hold-key",
        keys.turbo_hold,
        "--reset-key",
        keys.reset,
        NULL
    };
    assert(sizeof(expected) / sizeof(*expected) == INTEGRAL_GB_MOBILE_ARGV_CAPACITY);
    const char *argv[INTEGRAL_GB_MOBILE_ARGV_CAPACITY + 1];
    argv[INTEGRAL_GB_MOBILE_ARGV_CAPACITY] = "canary";
    integral_gb_mobile_arguments(&launch, argv);
    for (size_t i = 0; i < INTEGRAL_GB_MOBILE_ARGV_CAPACITY; ++i) {
        if (expected[i]) {
            assert(argv[i] && !strcmp(argv[i], expected[i]));
            assert(strcmp(argv[i], "--slot2-keys") && strcmp(argv[i], keys.slot2));
        }
        else assert(argv[i] == NULL);
    }
    assert(!strcmp(argv[INTEGRAL_GB_MOBILE_ARGV_CAPACITY], "canary"));
    assert(argv[18] == keys.slot1);
    launch.rom = long_path;
    launch.rtc_offset = "0";
    integral_gb_mobile_arguments(&launch, argv);
    assert(argv[2] == long_path && strlen(argv[2]) == sizeof(long_path) - 1);
    assert(argv[12] == launch.rtc_offset && !strcmp(argv[12], "0"));
    assert(strlen(argv[18]) == sizeof(keys.slot1) - 1);
    puts("GB Mobile arguments: exact order, paths, RTC, window, SLOT1/common keys PASS");
    launch.rom = "test.gbc"; strcpy(keys.slot1, "Z,X");
#ifdef _WIN32
    char self[32768]; DWORD length = GetModuleFileNameA(NULL, self, sizeof(self));
    assert(length > 0 && length < sizeof(self));
    test_process(&launch, self, argv_in[1]);
#else
    test_process(&launch, argv_in[0], argv_in[1]);
#endif
    return 0;
}

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_gb_local.h"
#include "http_client.h"
#include "client_file_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#define chdir _chdir
#define getcwd _getcwd
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

static char marker_path[2048];
static char *active_status;
static long started_pid;
static unsigned expected_count;
static unsigned expected_exit;
static unsigned stopped;
static unsigned started;
static int prep_failure;
static char prep_order[64];
static void record(char c)
{
    size_t n = strlen(prep_order);
    if (n >= sizeof(prep_order) - 1u) abort();
    prep_order[n] = c;
    prep_order[n + 1] = 0;
}

int integral_api_start_local_game(const char *server, const char *token,
                                 const char *save1, const char *save2,
                                 char *session, size_t size, long long *fence,
                                 char *error, size_t error_size)
{
    assert(strcmp(server, "test-server") == 0 && strcmp(token, "test-token") == 0);
    assert(strcmp(save1, "save-one") == 0);
    if (save2) assert(strcmp(save2, "save-two") == 0);
    record('L');
    if (prep_failure == 3) { snprintf(error, error_size, "LOCK TEST"); return -1; }
    snprintf(session, size, "%s", prep_failure == 4 ? "../bad" : "test-session");
    *fence = 37;
    return 0;
}

int integral_api_stop_local_game(const char *server, const char *token,
                                 const char *session, long long fence,
                                 char *error, size_t error_size)
{
    assert(strcmp(server, "test-server") == 0);
    assert(strcmp(token, "test-token") == 0);
    assert(strcmp(session, prep_failure == 4 ? "../bad" : "test-session") == 0 && fence == 37);
    (void)error; (void)error_size;
    stopped++;
    record('X');
    return 0;
}

static void check_handoff(const char *server, const char *token,
                           const char *session, long long fence,
                           const LocalSyncSlot *slots, unsigned count, bool active)
{
    assert(strcmp(server, "test-server") == 0);
    assert(strcmp(token, "test-token") == 0);
    assert(strcmp(session, "test-session") == 0 && fence == 37 && active);
    assert(count == expected_count);
    assert(strcmp(slots[0].save_id, "save-one") == 0 && slots[0].revision == 11);
    assert(strcmp(slots[0].last_hash, "hash-one") == 0 && !slots[0].mobile_guard);
    assert(strcmp(slots[0].save_path, "working/slot1.sav") == 0);
    if (count == 2) {
        assert(strcmp(slots[1].save_id, "save-two") == 0 && slots[1].revision == 29);
        assert(strcmp(slots[1].save_path, "working/slot2.sav") == 0);
    }
}

static void write_marker(void)
{
    FILE *f = fopen(marker_path, "wb");
    assert(f);
    assert(fputs("monitored", f) >= 0);
    assert(fclose(f) == 0);
}

#ifdef _WIN32
void start_save_sync_thread(intptr_t process, const char *server, const char *token,
                            const char *session, long long fence,
                            const LocalSyncSlot *slots, unsigned count, bool active)
{
    check_handoff(server, token, session, fence, slots, count, active);
    assert(started == 1); /* Status/log must precede the existing monitor handoff. */
    assert(WaitForSingleObject((HANDLE)process, 10000) == WAIT_OBJECT_0);
    DWORD code = 0;
    assert(GetExitCodeProcess((HANDLE)process, &code) && code == expected_exit);
    assert(CloseHandle((HANDLE)process));
    write_marker();
}
#else
void monitor_save_sync_process(IntegralChildProcess process, const char *server,
                               const char *token, const char *session, long long fence,
                               LocalSyncSlot *slots, unsigned count, bool active)
{
    check_handoff(server, token, session, fence, slots, count, active);
    int status;
    assert(waitpid(process, &status, 0) == process);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == (int)expected_exit);
    write_marker();
}
#endif

static void observe(void *context, const char *event, const char *detail)
{
    assert(context == active_status);
    if (strcmp(event, "gb_runtime_server_spawn_failed") == 0) {
        assert(stopped == 1 && strcmp(active_status, "GB_RUNTIME START FAILED") == 0);
        assert(strncmp(detail, "errno=", 6) == 0);
        return;
    }
#ifdef _WIN32
    assert(strcmp(event, "gb_runtime_server_started") == 0);
#else
    assert(strcmp(event, "gb_runtime_server_monitor_started") == 0);
#endif
    assert(strcmp(active_status, expected_count == 2 ? "GB_RUNTIME SERVER2 STARTED" : "GB_RUNTIME LOCAL STARTED") == 0);
    assert(sscanf(detail, "pid=%ld", &started_pid) == 1);
    started++;
}

static void test_process(IntegralGbLocalLaunch *launch, const char *self, const char *directory)
{
    LocalSyncSlot slots[2] = {0};
    strcpy(slots[0].save_id, "save-one"); slots[0].revision = 11;
    strcpy(slots[0].last_hash, "hash-one");
    strcpy(slots[0].save_path, "working/slot1.sav");
    strcpy(slots[1].save_id, "save-two"); slots[1].revision = 29;
    strcpy(slots[1].save_path, "working/slot2.sav");
    IntegralGbLocalSession session = {
        .server = "test-server", .token = "test-token", .game_session_id = "test-session",
        .fencing_token = 37, .slots = slots,
    };
    char status[160];
    active_status = status;
    for (unsigned run = 0; run < 3; ++run) {
        snprintf(marker_path, sizeof(marker_path), "%s/result-%u", directory, run);
        launch->runtime = run == 2 ? "./missing-gb-test-runtime" : self;
        launch->rom2 = run == 1 ? launch->rom1 : NULL;
        expected_count = session.count = run == 1 ? 2 : 1;
        expected_exit = run == 2 ? 127 : 42;
        stopped = started = 0;
        integral_gb_local_start(launch, &session, status, sizeof(status), observe, status);
#ifdef _WIN32
        if (run == 2) {
            assert(stopped == 1 && started == 0);
            continue;
        }
#else
        int monitor_status;
        assert(waitpid((pid_t)started_pid, &monitor_status, 0) == (pid_t)started_pid);
        assert(WIFEXITED(monitor_status) && WEXITSTATUS(monitor_status) == 0);
#endif
        assert(started == 1 && stopped == 0);
        FILE *f = fopen(marker_path, "rb");
        assert(f);
        char marker[16] = {0};
        assert(fread(marker, 1, 9, f) == 9 && strcmp(marker, "monitored") == 0);
        fclose(f);
    }
    puts("GB LOCAL process: one/two screens, monitor handoff, failed start PASS");
}

static bool prep_recover(void *context, const char *id)
{
    assert(context == active_status);
    bool first = strcmp(id, "save-one") == 0;
    record(first ? '1' : '2');
    return !(prep_failure == (first ? 1 : 2));
}

static int prep_download(void *context, const IntegralConfigRomSlot *slot,
                         const char *path, LocalSyncSlot *sync)
{
    assert(context == active_status);
    bool first = strcmp(slot->save_id, "save-one") == 0;
    record(first ? 'a' : 'b');
    assert(strcmp(path, first ? "runtime/gb-sessions/test-session/slot1.sav" :
                               "runtime/gb-sessions/test-session/slot2.sav") == 0);
    if (prep_failure == (first ? 5 : 6)) return -1;
    snprintf(sync->save_id, sizeof(sync->save_id), "%s", slot->save_id);
    /* Dummy monitor checks these sentinel paths; no real SAV is accessed. */
    snprintf(sync->save_path, sizeof(sync->save_path), "working/slot%u.sav", first ? 1 : 2);
    snprintf(sync->last_hash, sizeof(sync->last_hash), "hash-one");
    sync->revision = first ? 11 : 29;
    return 0;
}

static bool prep_rtc(void *context, char *out, size_t size)
{
    assert(context == active_status); record('R'); snprintf(out, size, "-77");
    return prep_failure != 7;
}

static void prep_window(void *context, unsigned *width, unsigned *height)
{
    assert(context == active_status); record('W'); *width = 360; *height = 480;
}

static void prep_log(void *context, const char *event, const char *detail)
{
    if (!strcmp(event, "local_start_blocked")) {
        assert(prep_failure == 1 || prep_failure == 2);
        assert(!strcmp(detail, "reason=outbox_recovery"));
    }
    else observe(context, event, detail);
}

static void test_preparation(const char *self, const char *directory, IntegralConfigKeys *keys)
{
    char cwd[4096]; assert(getcwd(cwd, sizeof(cwd))); assert(chdir(directory) == 0);
    IntegralConfigRomSlot slots[2] = {0};
    strcpy(slots[0].rom_path, "same.gbc"); strcpy(slots[1].rom_path, "same.gbc");
    strcpy(slots[0].save_id, "save-one"); strcpy(slots[1].save_id, "save-two");
    char status[160]; active_status = status;
    IntegralGbLocalRequest request = {
        .slot1 = &slots[0], .slot2 = &slots[1], .runtime = self, .port = "25100",
        .server = "test-server", .token = "test-token", .keys = keys,
        .status = status, .status_size = sizeof(status), .context = status,
        .recover = prep_recover, .download = prep_download, .rtc = prep_rtc,
        .window_size = prep_window, .log = prep_log,
    };
    const char *orders[] = {"12LabRW", "1", "12", "12L", "12LX", "12LaX", "12LabX", "12LabRX"};
    for (prep_failure = 1; prep_failure <= 7; ++prep_failure) {
        prep_order[0] = 0; stopped = started = 0;
        integral_gb_local_run(&request);
        assert(strcmp(prep_order, orders[prep_failure]) == 0);
        assert(started == 0 && stopped == (unsigned)(prep_failure >= 4));
    }
    prep_failure = 0;
    for (unsigned count = 1; count <= 2; ++count) {
        expected_count = count; expected_exit = 42; stopped = started = 0; prep_order[0] = 0;
        request.slot2 = count == 2 ? &slots[1] : NULL;
        snprintf(marker_path, sizeof(marker_path), "prepared-%u", count);
        integral_gb_local_run(&request);
#ifndef _WIN32
        int s; assert(waitpid((pid_t)started_pid, &s, 0) == (pid_t)started_pid);
        assert(WIFEXITED(s) && WEXITSTATUS(s) == 0);
#endif
        assert(started == 1 && stopped == 0);
        assert(strcmp(prep_order, count == 2 ? orders[0] : "1LaRW") == 0);
        FILE *f = fopen(marker_path, "rb"); assert(f); fclose(f);
    }
    request.slot1 = NULL; prep_order[0] = 0;
    integral_gb_local_run(&request);
    assert(!strcmp(status, "SLOT1 ROM REQUIRED") && !prep_order[0]);
    request.slot1 = &slots[0]; request.runtime = "./missing-runtime";
    integral_gb_local_run(&request);
    assert(!strcmp(status, "GB_RUNTIME SERVER NOT FOUND") && !prep_order[0]);
    request.runtime = self; request.token = "";
    integral_gb_local_run(&request);
    assert(!strcmp(status, "LOGIN TOKEN REQUIRED") && !prep_order[0]);
    request.token = "test-token"; slots[0].save_id[0] = 0;
    integral_gb_local_run(&request);
    assert(!strcmp(status, "REGISTER SLOT1 FIRST") && !prep_order[0]);
    strcpy(slots[0].save_id, "save-one"); slots[1].save_id[0] = 0;
    integral_gb_local_run(&request);
    assert(!strcmp(status, "REGISTER SLOT2 FIRST") && !prep_order[0]);
    assert(ensure_directory("blocked") == 0 && chdir("blocked") == 0);
    FILE *blocked = fopen("runtime", "wb"); assert(blocked); fclose(blocked);
    request.slot2 = NULL; prep_order[0] = 0; stopped = started = 0;
    integral_gb_local_run(&request);
    assert(!strcmp(status, "GB_RUNTIME SESSION CREATE FAILED"));
    assert(!strcmp(prep_order, "1LX") && stopped == 1 && started == 0);
    assert(chdir(cwd) == 0);
    puts("GB LOCAL preparation: order, preflight, failure cleanup, independent slots PASS");
}

int main(int argc, char **argv_in)
{
    if (argc > 1 && strcmp(argv_in[1], "--rom1") == 0) return 42;
    assert(argc == 2);
    const unsigned sizes[][6] = {
        {360, 360, 1920, 1040, 720, 360},
        {800, 600, 1920, 1040, 1600, 600},
        {1000, 700, 1600, 900, 1600, 560},
        {800, 1000, 1920, 800, 1280, 800},
        {1000, 1000, 1200, 500, 1000, 500},
        {360, 360, 720, 360, 720, 360},
        {360, 360, 0, 0, 720, 360},
        {16384, 16384, 0, 0, 16384, 8192},
    };
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        unsigned w, h;
        integral_gb_local_pair_window_size(sizes[i][0], sizes[i][1], sizes[i][2], sizes[i][3], &w, &h);
        assert(w == sizes[i][4] && h == sizes[i][5]);
    }
    IntegralConfigKeys keys = {0};
    memset(keys.slot1, 'a', sizeof(keys.slot1) - 1);
    strcpy(keys.slot2, "controller:日本語 / slot 2");
    strcpy(keys.fast, "Tab");
    strcpy(keys.screenshot, "F12");
    strcpy(keys.escape, "Escape");
    strcpy(keys.turbo_hold, "Space");
    strcpy(keys.reset, "F1");
    char long_path[INTEGRAL_CONFIG_PATH_MAX];
    memset(long_path, 'x', sizeof(long_path) - 1);
    long_path[sizeof(long_path) - 1] = '\0';
    IntegralGbLocalLaunch launch = {
        .runtime = "runtime dir/GB Runtime",
        .rom1 = "roms/日本語 name.gbc", .save1 = "session/slot1.sav",
        .rom2 = "roms/日本語 name.gbc", .save2 = "session/slot2.sav",
        .port = "8765", .rtc_offset = "-123456",
        .sgb = "disable",
        .window_width = "360", .window_height = "721", .keys = &keys,
    };
    /* LOCAL SERVER2 is an in-process link, not a LAN listener. */
    const char *expected_two[] = {
        launch.runtime,
        "--rom1",
        launch.rom1,
        "--save1",
        launch.save1,
        "--sgb",
        launch.sgb,
        "--rom2",
        launch.rom2,
        "--save2",
        launch.save2,
        "--ir-off-delay-ticks",
        "32",
        "--rtc-offset-seconds",
        launch.rtc_offset,
        "--display",
        "--display-slots",
        "2",
        "--window-width",
        launch.window_width,
        "--window-height",
        launch.window_height,
        "--slot1-keys",
        keys.slot1,
        "--slot2-keys",
        keys.slot2,
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
        "--audio",
        NULL
    };
    const char *expected_one[] = {
        launch.runtime,
        "--rom1",
        launch.rom1,
        "--save1",
        launch.save1,
        "--sgb",
        launch.sgb,
        "--self",
        "--rtc-offset-seconds",
        launch.rtc_offset,
        "--display",
        "--display-slots",
        "1",
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
        "--audio",
        NULL
    };
    const char *argv[INTEGRAL_GB_LOCAL_ARGV_CAPACITY + 1] = {0};
    argv[INTEGRAL_GB_LOCAL_ARGV_CAPACITY] = "canary";
    integral_gb_local_arguments(&launch, argv);
    for (unsigned i = 0; argv[i]; ++i) {
        assert(strcmp(argv[i], "--remote-input") != 0);
        assert(strcmp(argv[i], "--bind") != 0);
        assert(strcmp(argv[i], "--port") != 0);
    }
    for (size_t i = 0; i < sizeof(expected_two) / sizeof(*expected_two); ++i) {
        if (expected_two[i]) assert(argv[i] && strcmp(argv[i], expected_two[i]) == 0);
        else assert(argv[i] == NULL);
    }
    assert(strcmp(argv[INTEGRAL_GB_LOCAL_ARGV_CAPACITY], "canary") == 0);
    launch.ir_off_delay_ticks = "0";
    integral_gb_local_arguments(&launch, argv);
    assert(strcmp(argv[11], "--ir-off-delay-ticks") == 0);
    assert(strcmp(argv[12], "0") == 0);
    launch.rom2 = NULL;
    integral_gb_local_arguments(&launch, argv);
    for (size_t i = 0; i < sizeof(expected_one) / sizeof(*expected_one); ++i) {
        if (expected_one[i]) assert(argv[i] && strcmp(argv[i], expected_one[i]) == 0);
        else assert(argv[i] == NULL);
    }
    launch.rom1 = long_path;
    integral_gb_local_arguments(&launch, argv);
    assert(argv[2] == long_path && strlen(argv[2]) == sizeof(long_path) - 1);
    assert(argv[0] == launch.runtime);
    puts("GB LOCAL arguments: one/two screens, keys, RTC, paths PASS");
    /* The above maximum-length vector is a data test, not a real ROM path.
     * Use native executable naming for CRT spawn, not MSYS argv[0]. */
    launch.rom1 = "test.gbc";
    strcpy(keys.slot1, "Z,X");
    strcpy(keys.slot2, "A,S");
#ifdef _WIN32
    char self[32768];
    DWORD length = GetModuleFileNameA(NULL, self, sizeof(self));
    assert(length > 0 && length < sizeof(self));
    test_process(&launch, self, argv_in[1]);
    test_preparation(self, argv_in[1], &keys);
#else
    test_process(&launch, argv_in[0], argv_in[1]);
    char self[4096]; assert(realpath(argv_in[0], self));
    test_preparation(self, argv_in[1], &keys);
#endif
    return 0;
}

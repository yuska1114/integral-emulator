/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_room_common.h"
#include "client_app.h"
#include "client_runtime_support.h"
#include "client_local.h"
#include "client_save_outbox.h"
#include "../runtimes/n64/src/remote_media_ipc.h"
#include "../runtimes/gb/src/common/key_config.h"
/* Inspect the received-frame fixture and notice without a production test API. */
#include "n64_runtime_media_stream.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#endif

/* Real ROOM functions and disposable child; no server or user SAV. */
int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    LoginState login = {0};
    IntegralConfigKeys keys = {0};
    IntegralConfigRomSlot slots[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    IntegralApiRomSlot server_slots[INTEGRAL_CONFIG_ROM_SLOTS] = {0};
    int local_slots[2] = {-1, -1};
    AppScreen screen = SCREEN_ROOM;
    bool quit = false;
    IntegralRoomContext room = {
        .login = &login, .keys = &keys, .rom_slots = slots,
        .server_rom_slots = server_slots, .local_slot_indices = local_slots,
        .screen = &screen, .quit = &quit,
    };
    integral_keys_defaults(&keys);
    if (argc == 2 && !strcmp(argv[1], "--focus-return")) {
        assert(SDL_Init(SDL_INIT_VIDEO) == 0);
        AppState app={0};bind_room_context(&app);
        SDL_Window *main=SDL_CreateWindow("Main focus regression",0,0,360,360,SDL_WINDOW_SHOWN);
        assert(main);
        assert(integral_room_create_media(&app.room,main,NULL));
        IntegralN64RuntimeMediaStream *stream=app.room.n64.n64_runtime_media_stream;
        stream->video_window=SDL_CreateWindow("Remote focus regression",30,30,360,360,SDL_WINDOW_SHOWN);
        assert(stream->video_window);
        integral_focus_new_game_window(stream->video_window);
        app.ui.screen=SCREEN_N64_ROOM;app.room.common.room_number=65;
        strcpy(app.room.n64.n64_runtime_media_role,"remote");
        finish_n64_room_locally(&app.room);
        SDL_PumpEvents();
        assert(!stream->video_window && app.ui.screen==SCREEN_MAIN_MENU);
        if (strcmp(SDL_GetCurrentVideoDriver(),"dummy")) assert(SDL_GetKeyboardFocus()==main);
        SDL_KeyboardEvent key={0};key.keysym.sym=SDLK_DOWN;
        handle_main_key(&app,&key);assert(app.ui.main_selected==1);
        key.keysym.sym=SDLK_TAB;handle_main_key(&app,&key);assert(app.ui.main_selected==2);
        key.keysym.sym=SDLK_UP;handle_main_key(&app,&key);assert(app.ui.main_selected==1);
        key.keysym.sym=SDLK_RETURN;handle_main_key(&app,&key);assert(app.ui.screen==SCREEN_ROOM_MODE);
        integral_room_destroy_resources(&app.room);SDL_DestroyWindow(main);SDL_Quit();
        puts("Remote window destroyed, MAIN focus restored, arrows/Tab/Enter PASS");
        return 0;
    }
    if (argc == 3 && (!strcmp(argv[1], "--capture-unit") ||
                      !strcmp(argv[1], "--capture-runtime"))) {
        bool live = !strcmp(argv[1], "--capture-runtime");
        assert(SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) == 0);
        int device = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 2, 16, 1);
        assert(device >= 0 && integral_gb_runtime_key_config_open_game_controllers() >= 1);
        screen = SCREEN_N64_ROOM;
        room.n64.n64_runtime_media_host_pid = 1; /* Never polled/stopped by this test. */
        strcpy(room.n64.n64_runtime_media_role, "host");
        assert((live ? integral_n64_runtime_remote_media_open_reader(argv[2]) :
                       integral_n64_runtime_remote_media_open_writer(argv[2])) == 0);
        for (int pad = 0; pad < 2; ++pad) {
            SDL_Event event = {0};
            if (pad) {
                event.type = SDL_JOYBUTTONDOWN;
                event.jbutton.which = SDL_JoystickGetDeviceInstanceID(device);
                event.jbutton.button = 8;
                event.jbutton.state = SDL_PRESSED;
                SDL_Keycode code = integral_gb_runtime_key_config_code_from_event(&event);
                snprintf(keys.screenshot, sizeof(keys.screenshot), "%s",
                         integral_gb_runtime_key_config_key_name(code));
            } else {
                strcpy(keys.screenshot, "F12");
                event.type = SDL_KEYDOWN;
                event.key.keysym.sym = SDLK_F12;
            }
            assert(handle_n64_room_util_event(&room, &event));
            assert(!strcmp(login.status, "SCREENSHOT REQUESTED"));
            assert(!handle_n64_room_util_event(&room, &event)); /* Held: no duplicate. */
            if (!live) {
                assert(integral_n64_runtime_remote_media_take_screenshot_request());
                assert(!integral_n64_runtime_remote_media_take_screenshot_request());
                integral_n64_runtime_remote_media_finish_screenshot(1);
            }
            int result = 0;
            for (int i = 0; i < 400 && !result; ++i) {
                result = integral_n64_runtime_remote_media_screenshot_result();
                if (!result) SDL_Delay(25);
            }
            assert(result == 1 && !integral_n64_runtime_remote_media_screenshot_result());
            event.type = pad ? SDL_JOYBUTTONUP : SDL_KEYUP;
            if (pad) event.jbutton.state = SDL_RELEASED;
            assert(!handle_n64_room_util_event(&room, &event));
        }
        integral_n64_runtime_remote_media_close();
        SDL_Event press = {0}; press.type = SDL_KEYDOWN; press.key.keysym.sym = SDLK_F12;
        strcpy(keys.screenshot, "F12");
        assert(handle_n64_room_util_event(&room, &press));
        assert(!strcmp(login.status, "SCREENSHOT FAILED"));
        if (!live) {
            IntegralN64RuntimeMediaStream remote = {0};
            unsigned char pixels[160 * 144 * 3];
            memset(pixels, 90, sizeof(pixels));
            remote.decode_mutex = SDL_CreateMutex();
            assert(remote.decode_mutex);
            remote.rgb = pixels;
            remote.decoded_frame_sequence = 1;
            remote.decoded_width = 160;
            remote.decoded_height = 144;
            room.n64.n64_runtime_media_stream = &remote;
            room.n64.n64_runtime_media_host_pid = 0;
            room.n64.n64_runtime_media_paired = true;
            strcpy(room.n64.n64_runtime_media_role, "remote");
            room.n64.util_screenshot_held = false;
            for (int pad = 0; pad < 2; ++pad) {
                SDL_Event event = {0};
                if (pad) {
                    event.type = SDL_JOYBUTTONDOWN;
                    event.jbutton.which = SDL_JoystickGetDeviceInstanceID(device);
                    event.jbutton.button = 8;
                    event.jbutton.state = SDL_PRESSED;
                    snprintf(keys.screenshot, sizeof(keys.screenshot), "%s",
                        integral_gb_runtime_key_config_key_name(
                            integral_gb_runtime_key_config_code_from_event(&event)));
                } else {
                    event.type = SDL_KEYDOWN; event.key.keysym.sym = SDLK_F12;
                    strcpy(keys.screenshot, "F12");
                }
                assert(handle_n64_room_util_event(&room, &event));
                assert(!handle_n64_room_util_event(&room, &event));
                assert(!strcmp(remote.screenshot_notice, "SCREENSHOT SAVED"));
                assert(remote.screenshot_notice_until > SDL_GetTicks64());
                event.type = pad ? SDL_JOYBUTTONUP : SDL_KEYUP;
                if (pad) event.jbutton.state = SDL_RELEASED;
                assert(!handle_n64_room_util_event(&room, &event));
            }
            SDL_DestroyMutex(remote.decode_mutex);
            room.n64.n64_runtime_media_stream = NULL;
        }
        integral_gb_runtime_key_config_close_game_controllers();
        assert(SDL_JoystickDetachVirtual(device) == 0);
        SDL_Quit();
        if (!live) assert(remove(argv[2]) == 0);
        puts("Host keyboard/controller capture: one request per press, completion and failure PASS");
        return 0;
    }
#ifndef _WIN32
    if (argc == 3 && !strcmp(argv[1], "--preflight-http")) {
        snprintf(login.server, sizeof(login.server), "%s", argv[2]);
        strcpy(login.token,"test-token");screen=SCREEN_N64_ROOM;
        room.common.room_number=65;
        room.common.room_ready_self=room.common.room_ready_peer=true;
        strcpy(room.n64.n64_runtime_media_session_id,"invalid-save-session");
        IntegralApiHeartbeatStatus notice={0};
        strcpy(notice.lifecycle_kind,"media");
        strcpy(notice.lifecycle_session_id,"invalid-save-session");
        strcpy(notice.lifecycle_status,"CANCELLED");
        strcpy(notice.termination_reason,"preflight_failed");
        handle_room_heartbeat_result(&room,&notice,true,NULL);
        assert(screen==SCREEN_N64_ROOM && room.common.room_number==65);
        assert(room.n64.preflight_failed && !room.n64.terminal_pending);
        assert(!room.common.room_ready_self && !room.common.room_ready_peer);
        for (Uint32 now=5000;now<60000;now+=5000) assert(poll_n64_runtime_media_transport(&room,now));
        SDL_KeyboardEvent key={0};key.keysym.sym=SDLK_DOWN;handle_n64_room_key(&room,&key);
        assert(room.common.room_selected==1);
        key.keysym.sym=SDLK_TAB;handle_n64_room_key(&room,&key);assert(room.common.room_selected==2);
        key.keysym.sym=SDLK_ESCAPE;handle_n64_room_key(&room,&key);
        assert(screen==SCREEN_MAIN_MENU && !room.common.room_number);
        puts("Preflight terminal stops retries, ROOM keyboard and Esc remain operational PASS");
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "--round7-http")) {
        snprintf(login.server, sizeof(login.server), "%s", argv[2]);
        strcpy(login.token, "disposable-test-token");
        room.common.room_number = 1;
        strcpy(room.link.room_link_session_id, "link-test");
        strcpy(room.link.room_game_session_id, "run-test");
        room.link.room_fencing_token = 1;
        room.link.room_client_started = true;
        pid_t child = fork();
        assert(child >= 0);
        if (!child) { signal(SIGTERM, SIG_DFL); for (;;) pause(); }
        room.link.room_client_pid = child;
        SDL_KeyboardEvent esc = {0};
        esc.keysym.sym = SDLK_ESCAPE;
        handle_room_key(&room, &esc);
        assert(screen == SCREEN_ROOM && room.common.room_number == 1);
        assert(!room.link.room_client_pid && room.link.room_link_session_id[0]);
        handle_room_key(&room, &esc);
        assert(screen == SCREEN_MAIN_MENU && !room.common.room_number);
        assert(!room.link.room_client_pid && !room.link.room_link_session_id[0]);
        int status;
        assert(waitpid(child, &status, WNOHANG) == -1 && errno == ECHILD);
        screen = SCREEN_N64_ROOM;
        room.common.room_number = 65;
        room.n64.terminal_pending = true;
        strcpy(room.n64.lifecycle_session_id, "media-test");
        strcpy(room.n64.lifecycle_room_code, "12345");
        handle_n64_room_key(&room, &esc); /* HTTP failure must retain ownership. */
        assert(screen == SCREEN_N64_ROOM && room.n64.terminal_pending);
        assert(!strcmp(room.n64.lifecycle_session_id, "media-test"));
        handle_n64_room_key(&room, &esc); /* Confirmed terminal permits MAIN. */
        assert(screen == SCREEN_MAIN_MENU && !room.common.room_number);
        assert(!room.n64.lifecycle_session_id[0] && !room.n64.terminal_pending);
        /* Authoritative ROOM disappearance must reap before returning MAIN,
         * independently of whether a Relay terminal frame was received. */
        for (int role = 0; role < 2; role++) for (int repeat = 0; repeat < 3; repeat++) {
            screen = SCREEN_ROOM;
            room.common.room_number = 1;
            strcpy(room.link.room_link_session_id, "link-current");
            strcpy(room.link.room_gb_runtime_fixed_host_role, role ? "remote" : "host");
            room.common.room_ready_self = room.common.room_ready_peer = true;
            child = fork();
            assert(child >= 0);
            if (!child) { signal(SIGTERM, SIG_DFL); for (;;) pause(); }
            room.link.room_client_pid = child;
            room.link.room_client_started = true;
            refresh_room(&room); /* API unavailable is not authoritative exit. */
            assert(screen == SCREEN_ROOM && room.link.room_client_pid == child);
            assert(kill(child, 0) == 0);
            refresh_room(&room);
            assert(screen == SCREEN_MAIN_MENU && !room.common.room_number);
            assert(!room.link.room_client_pid && !room.link.room_link_session_id[0]);
            assert(!room.common.room_ready_self && !room.common.room_ready_peer);
            assert(waitpid(child, &status, WNOHANG) == -1 && errno == ECHILD);
        }
        puts("Round7 HTTP ordering, pending retention, confirmed cleanup and reap PASS");
        return 0;
    }
#endif
    /* Both UI entry points must refuse rapid Enter presses before any API
     * call, during launch and while a disposable monitor is still alive. */
    AppState *app = calloc(1, sizeof(*app));
    assert(app);
    SDL_KeyboardEvent enter = {0};
    enter.type = SDL_KEYDOWN;
    enter.keysym.sym = SDLK_RETURN;
    app->ui.screen = SCREEN_GB_MOBILE;
    app->local.local_starting = true;
    for (unsigned i = 0; i < 2; i++) {
        handle_local_key(app, &enter);
        assert(!strcmp(app->login.status, "GAME ALREADY RUNNING"));
        handle_n64_runtime_key(app, &enter);
        assert(!strcmp(app->login.status, "GAME ALREADY RUNNING"));
    }
    app->local.local_starting = false;
#ifdef _WIN32
    app->local.local_monitor = _spawnl(_P_NOWAIT, "C:/Windows/System32/cmd.exe", "cmd.exe", "/c", "ping -n 2 127.0.0.1 >NUL", NULL);
    assert(app->local.local_monitor != -1);
#else
    app->local.local_monitor = fork();
    assert(app->local.local_monitor >= 0);
    if (!app->local.local_monitor) { sleep(1); _exit(0); }
#endif
    for (unsigned i = 0; i < 2; i++) {
        handle_local_key(app, &enter);
        assert(!strcmp(app->login.status, "GAME ALREADY RUNNING"));
        handle_n64_runtime_key(app, &enter);
        assert(!strcmp(app->login.status, "GAME ALREADY RUNNING"));
    }
    int monitor_status = 0;
    waitpid(app->local.local_monitor, &monitor_status, 0);
    handle_local_key(app, &enter);
    assert(!app->local.local_monitor && !strcmp(app->login.status, "SLOT1 ROM REQUIRED"));
    handle_n64_runtime_key(app, &enter);
    assert(!strcmp(app->login.status, "LOGIN TOKEN REQUIRED"));
#ifndef _WIN32
    char original_cwd[4096], notice_dir[] = "/tmp/integral-save-notice-XXXXXX";
    assert(getcwd(original_cwd, sizeof(original_cwd)) && mkdtemp(notice_dir));
    assert(chdir(notice_dir) == 0);
    LocalSyncSlot notice_slot = {0};
    strcpy(notice_slot.account, "notice-owner");
    strcpy(notice_slot.save_id, "notice-save");
    const unsigned char synthetic_sav[] = {1,2,3,4};
    assert(write_save_upload_outbox("notice-server", &notice_slot, synthetic_sav, 4,
        "test-hash", "DNS resolution failed", "test-game", 1, "test-request"));
    strcpy(app->login.server, "notice-server");
    strcpy(app->login.username, "notice-owner");
    strcpy(app->login.token, "synthetic-token");
    strcpy(app->catalog.server_rom_slots[0].save_id, "notice-save");
    const AppScreen notice_screens[] = {SCREEN_LOCAL, SCREEN_LOCAL, SCREEN_GB_MOBILE, SCREEN_N64_RUNTIME};
    for (unsigned i = 0; i < 4; ++i) {
        app->ui.screen = notice_screens[i];
        app->local.outbox_checked_ticks = 0;
        poll_local_save_notice(app);
        assert(!strcmp(app->local.save_sync_notice, "SAV UNSENT - RETRY ON NEXT START"));
        assert(!strcmp(app->local.save_sync_error, "DNS resolution failed"));
        assert(strcmp(app->local.save_sync_notice, "SAV SYNC RECOVERED"));
    }
    strcpy(app->login.username, "different-owner");
    poll_local_save_notice(app);
    assert(!app->local.save_sync_notice[0] && !app->local.save_sync_error[0]);
    assert(chdir(original_cwd) == 0);
#endif
    free(app);
    char hotkeys[640];
    for (unsigned local = 0; local < 2; ++local) {
        assert(make_n64_runtime_util_hotkeys(&keys, local != 0, hotkeys, sizeof(hotkeys)));
        const char *cursor = hotkeys;
        for (unsigned i = 0; i < 27; ++i) {
            int kind, index, direction, device, consumed;
            assert(sscanf(cursor, "%d:%d:%d:%d%n", &kind, &index, &direction, &device, &consumed) == 4);
            bool active = i == 0 || i == 8 || (local && i == 5);
            assert(kind == (active ? 1 : 0));
            assert(direction == 0 && device == -1);
            assert(active ? index > 0 : index == 0);
            cursor += consumed;
            if (i != 26) assert(*cursor++ == ',');
            else assert(*cursor == 0);
        }
    }
    assert(!make_n64_runtime_util_hotkeys(&keys, true, hotkeys, 3));
    strcpy(login.username, "host");
    IntegralApiRoom match = {0};
    match.room_number = 1;
    strcpy(match.link_mode, "trade");
    activate_matched_room(&room, &match);
    assert(screen == SCREEN_ROOM && room.common.room_number == 1);
    assert(room.link.room_link_mode == INTEGRAL_ROOM_MODE_TRADE);
    assert(room.link.room_slot_index == -1 && !room.link.room_game_ended);

    set_room_link_session_id(&room, "link_first");
    room.link.room_fencing_token = 27;
    room.link.room_gb_runtime_save_preflight_blocked = true;
    set_room_link_session_id(&room, "link_first");
    assert(room.link.room_fencing_token == 27);
    assert(room.link.room_gb_runtime_save_preflight_blocked);
    set_room_link_session_id(&room, "link_second");
    assert(room.link.room_fencing_token == 0);
    assert(!room.link.room_gb_runtime_save_preflight_blocked);
    assert(room.link.room_gb_runtime_fixed_host_result_read == -1);
    assert(room.common.room_remaining_seconds == -1);
    assert(gb_runtime_fixed_host_may_start_for_control_state("host", "READY"));
    assert(!gb_runtime_fixed_host_may_start_for_control_state("host", "WAITING_PEER"));
    assert(gb_runtime_fixed_host_may_start_for_control_state("remote", "WAITING_PEER"));
    assert(!gb_runtime_fixed_host_may_start_for_control_state("host", "ENDED"));

    IntegralApiHeartbeatStatus heartbeat = {0};
    strcpy(heartbeat.lifecycle_kind, "link");
    strcpy(heartbeat.lifecycle_session_id, "link_first");
    strcpy(heartbeat.lifecycle_status, "EXPIRED");
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(!room.link.room_game_ended && !room.common.room_lifecycle_status[0]);
    strcpy(heartbeat.lifecycle_session_id, "link_second");
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(room.link.room_game_ended);
    clear_room_link_session_id(&room);
    assert(!room.link.room_link_session_id[0] && !room.link.room_game_ended);
    assert(!room.link.room_start_requested && room.link.room_client_pid == 0);
#ifndef _WIN32
    for (unsigned ignore_term = 0; ignore_term < 2; ++ignore_term) {
        int ready_pipe[2];
        assert(pipe(ready_pipe) == 0);
        pid_t owned = fork();
        assert(owned >= 0);
        if (!owned) {
            close(ready_pipe[0]);
            signal(SIGTERM, ignore_term ? SIG_IGN : SIG_DFL);
            assert(write(ready_pipe[1], "r", 1) == 1);
            for (;;) pause();
        }
        close(ready_pipe[1]);
        char ready;
        assert(read(ready_pipe[0], &ready, 1) == 1);
        close(ready_pipe[0]);
        strcpy(room.link.room_link_session_id, "owned-child");
        room.link.room_client_pid = owned;
        room.link.room_client_started = true;
        clear_room_link_session_id(&room);
        assert(!room.link.room_client_pid && !room.link.room_client_started);
        assert(waitpid(owned, NULL, WNOHANG) == -1 && errno == ECHILD);
    }
#endif

    handle_room_heartbeat_result(&room, NULL, false, "network unavailable");
    handle_room_heartbeat_result(&room, NULL, false, "network unavailable");
    assert(!room.link.room_game_ended && room.common.room_heartbeat_failures == 2);
    handle_room_heartbeat_result(&room, NULL, false, "network unavailable");
    assert(room.link.room_game_ended && room.common.room_heartbeat_failures == 3);
    activate_matched_room(&room, &match);
    assert(!room.link.room_game_ended && room.common.room_heartbeat_failures == 0);
    handle_room_heartbeat_result(&room, NULL, false, "403 fenced");
    assert(room.link.room_game_ended && room.common.room_heartbeat_failures == 3);

    activate_matched_room(&room, &match);
    strcpy(heartbeat.lifecycle_kind, "room");
    strcpy(heartbeat.lifecycle_status, "EXPIRED");
    room.common.room_ready_self = room.common.room_ready_peer = true;
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(screen == SCREEN_MAIN_MENU && room.common.room_number == 0);
    assert(!room.common.room_ready_self && !room.common.room_ready_peer);

    screen = SCREEN_N64_ROOM;
    strcpy(room.n64.n64_runtime_media_session_id, "media_current");
    strcpy(heartbeat.lifecycle_kind, "media");
    strcpy(heartbeat.lifecycle_session_id, "media_old");
    room.link.room_game_ended = false;
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(!room.link.room_game_ended);
    room.common.room_ready_self = room.common.room_ready_peer = true;
    room.n64.n64_runtime_media_retry_after_ticks = 1000;
    assert(poll_n64_runtime_media_transport(&room, 999));
    assert(!room.n64.n64_runtime_media_connection);
    room.n64.n64_runtime_media_retry_after_ticks = 10;
    assert(poll_n64_runtime_media_transport(&room, UINT32_MAX - 1));
    assert(!room.n64.n64_runtime_media_connection);
    request_runtime_exit_confirmation(&room, true);
    assert(room.n64.runtime_exit_confirming && !room.n64.runtime_exit_confirm_yes);
    SDL_Event event = {0};
    event.type = SDL_QUIT;
    assert(handle_runtime_exit_confirmation_event(&room, &event));
    assert(!quit && room.n64.runtime_exit_confirming);
    event.type = SDL_KEYDOWN;
    event.key.keysym.sym = SDLK_ESCAPE;
    assert(handle_runtime_exit_confirmation_event(&room, &event));
    assert(!quit && !room.n64.runtime_exit_confirming);
    assert(!room.n64.runtime_exit_quit_client);
    /* Authoritative end wins over transport recovery; no stale READY/retry. */
    room.common.room_number = 65;
    room.n64.n64_runtime_media_reconnect_deadline = 25000;
    room.n64.n64_room_ready = true;
    unsigned previous_epoch = room.common.room_poll_epoch;
    strcpy(heartbeat.lifecycle_session_id, "media_current");
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(screen == SCREEN_MAIN_MENU && room.common.room_number == 0);
    assert(!room.common.room_ready_self && !room.common.room_ready_peer);
    assert(!room.n64.n64_room_ready && !room.n64.n64_runtime_media_reconnect_deadline);
    assert(room.common.room_poll_epoch != previous_epoch);
    /* A creator's authoritative ROOM closure is not a recovery timeout.
     * Exercise short and >30-minute local uptime, including cleared media ID. */
    const Uint32 uptimes[] = {1000u, 2220000u};
    for (unsigned i = 0; i < 2; i++) {
        screen = SCREEN_N64_ROOM;
        room.common.room_number = 65;
        room.common.room_ready_self = room.common.room_ready_peer = true;
        room.n64.n64_room_ready = true;
        room.n64.n64_runtime_media_reconnect_deadline = uptimes[i] + 25000u;
        strcpy(room.common.current_room.user1, "HOST");
        strcpy(heartbeat.lifecycle_kind, "room");
        strcpy(heartbeat.lifecycle_status, "CLOSED");
        strcpy(heartbeat.termination_reason, i ? "participant_left" : "host_finished");
        previous_epoch = room.common.room_poll_epoch;
        handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
        assert(screen == SCREEN_MAIN_MENU && !room.common.room_number);
        assert(!room.common.room_ready_self && !room.common.room_ready_peer);
        assert(!room.common.current_room.user1[0] && !room.n64.n64_room_ready);
        assert(!room.n64.n64_runtime_media_reconnect_deadline);
        assert(room.common.room_poll_epoch != previous_epoch);
        assert(!strcmp(login.status, "N64 GAME ENDED"));
    }
    /* Explicit participant exit is normal for both ROOM and media notices. */
    screen = SCREEN_N64_ROOM;
    room.common.room_number = 65;
    strcpy(room.n64.n64_runtime_media_session_id, "exit-current");
    strcpy(heartbeat.lifecycle_kind, "media");
    strcpy(heartbeat.lifecycle_session_id, "exit-current");
    strcpy(heartbeat.lifecycle_status, "CANCELLED");
    strcpy(heartbeat.termination_reason, "participant_left");
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(screen == SCREEN_MAIN_MENU && !strcmp(login.status, "N64 GAME ENDED"));
    /* Local time alone cannot end a server-owned recovery interval. */
    screen = SCREEN_N64_ROOM;
    room.n64.n64_runtime_media_reconnect_deadline = 50;
    assert(poll_n64_runtime_media_transport(&room, UINT32_MAX - 20));
    assert(screen == SCREEN_N64_ROOM);
    assert(poll_n64_runtime_media_transport(&room, 50));
    assert(screen == SCREEN_N64_ROOM);
    reset_n64_runtime_media_connection(&room);
    assert(!room.n64.n64_runtime_media_session_id[0]);
    assert(!room.n64.n64_runtime_media_connection);
    assert(!room.n64.n64_runtime_media_paired && !room.n64.n64_runtime_media_host_pid);
#ifdef _WIN32
    room.n64.n64_runtime_media_host_pid = _spawnl(_P_NOWAIT, "C:/Windows/System32/cmd.exe", "cmd.exe", "/c", "exit 0", NULL);
    assert(room.n64.n64_runtime_media_host_pid != -1);
#else
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) _exit(0);
    room.n64.n64_runtime_media_host_pid = child;
#endif
    screen = SCREEN_N64_ROOM;
    room.common.room_ready_self = room.common.room_ready_peer = false;
    strcpy(login.status, "N64 HOST STARTED");
    strcpy(room.n64.n64_runtime_media_launched_session_id, "previous-child");
    for (unsigned tries = 0; tries < 200 && room.n64.n64_runtime_media_host_pid; tries++) {
        poll_n64_runtime_media_transport(&room, SDL_GetTicks());
        SDL_Delay(10);
    }
    /* No API in this unit: a lost finish acknowledgement must retain the
     * transport/session rather than claim success or relaunch the Runtime. */
    assert(screen == SCREEN_N64_ROOM && !room.n64.n64_runtime_media_host_pid);
    assert(room.n64.host_finish_pending);
    assert(!room.n64.n64_runtime_media_launched_session_id[0]);
    assert(strcmp(login.status, "N64 FINISH PENDING - RETRYING") == 0);
    strcpy(room.n64.n64_runtime_media_session_id, "finish-current");
    /* A 20-second outage crossing the heartbeat failure threshold must not
     * discard the finish intent or permit another media authentication. */
    for (unsigned i = 0; i < 4; ++i)
        handle_room_heartbeat_result(&room, NULL, false, "network unavailable");
    assert(room.n64.host_finish_pending && room.n64.terminal_pending);
    assert(!strcmp(room.n64.n64_runtime_media_session_id, "finish-current"));
    room.n64.n64_runtime_media_retry_after_ticks = SDL_GetTicks() + 20000u;
    assert(poll_n64_runtime_media_transport(&room, SDL_GetTicks()));
    assert(room.n64.host_finish_pending && !room.n64.n64_runtime_media_host_pid);
    strcpy(heartbeat.lifecycle_kind, "room");
    strcpy(heartbeat.lifecycle_status, "CLOSED");
    strcpy(heartbeat.termination_reason, "host_finished");
    handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
    assert(screen == SCREEN_MAIN_MENU && !room.n64.host_finish_pending);
    assert(strcmp(login.status, "N64 GAME ENDED") == 0);
    for (unsigned role = 0; role < 2; ++role) {
        screen = SCREEN_N64_ROOM;
        room.common.room_number = 65;
        room.common.room_ready_self = room.common.room_ready_peer = true;
        strcpy(room.n64.n64_runtime_media_session_id, "expired-current");
        strcpy(room.n64.n64_runtime_media_role, role ? "remote" : "host");
        for (unsigned i = 0; i < 3; ++i)
            handle_room_heartbeat_result(&room, NULL, false, "API disconnected");
        assert(room.n64.terminal_pending);
        assert(!strcmp(room.n64.n64_runtime_media_session_id, "expired-current"));
        room.n64.n64_runtime_media_session_id[0] = '\0'; /* authorization output no longer owns lifecycle */
        strcpy(heartbeat.lifecycle_kind, "media");
        strcpy(heartbeat.lifecycle_session_id, "past-room-session");
        strcpy(heartbeat.lifecycle_status, "EXPIRED");
        strcpy(heartbeat.termination_reason, "session_expired");
        handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
        assert(screen == SCREEN_N64_ROOM);
        strcpy(heartbeat.lifecycle_session_id, "expired-current");
        handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
        assert(screen == SCREEN_MAIN_MENU && !room.common.room_number);
        assert(!room.n64.terminal_pending && !room.n64.host_finish_pending);
        assert(!room.common.room_ready_self && !room.common.room_ready_peer);
        previous_epoch = room.common.room_poll_epoch;
        handle_room_heartbeat_result(&room, &heartbeat, true, NULL);
        assert(room.common.room_poll_epoch == previous_epoch);
    }
    screen = SCREEN_N64_ROOM;
    room.common.room_number = 65;
    room.n64.terminal_pending = true;
    strcpy(room.n64.lifecycle_session_id, "offline-expired");
    SDL_KeyboardEvent escape = {0};
    escape.keysym.sym = SDLK_ESCAPE;
    handle_n64_room_key(&room, &escape);
    assert(screen == SCREEN_N64_ROOM && room.common.room_number == 65);
    assert(room.n64.terminal_pending && !strcmp(room.n64.lifecycle_session_id, "offline-expired"));
    assert(!strcmp(login.status, "ROOM EXIT UNCONFIRMED - ESC RETRY"));
    puts("ROOM state: stop/reap, offline exit, entry/reentry, session reset, launch controls, stale lifecycle, heartbeat failure/expiry, retry wrap, exit cancel, media reset PASS");
    return 0;
}

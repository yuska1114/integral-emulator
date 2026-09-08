/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "protocol.h"
#include "audio_player.h"
#include "input_router.h"
#include "lan_remote.h"
#include "link_bridge.h"
#include "log_util.h"
#include "net_compat.h"
#include "process_util.h"
#include "server_cleanup.h"
#include "server_options.h"
#include "screenshot.h"
#include "slot.h"
#include "slot2_upload_wait.h"
#include "stream_server.h"
#include "string_util.h"
#include "video_window.h"
#include "joypad.h"

#define INTEGRAL_GB_RUNTIME_FRAME_INTERVAL_US 16743ULL
#define INTEGRAL_GB_RUNTIME_MAX_FRAME_DEADLINE_LAG_US (INTEGRAL_GB_RUNTIME_FRAME_INTERVAL_US * 2ULL)

static volatile sig_atomic_t g_gb_runtime_server_shutdown_requested = 0;

static void integral_gb_runtime_server_handle_shutdown_signal(int signum)
{
    (void)signum;
    g_gb_runtime_server_shutdown_requested = 1;
}

static void integral_gb_runtime_server_install_signal_handlers(void)
{
    signal(SIGINT, integral_gb_runtime_server_handle_shutdown_signal);
    signal(SIGTERM, integral_gb_runtime_server_handle_shutdown_signal);
}

static int exec_menu(void)
{
    char *argv[] = {"build/integral_gb_runtime_frontend", NULL};
    integral_gb_runtime_execv_with_exe_fallback(argv[0], argv);
    perror("exec menu failed");
    return 1;
}

static void free_slot_heap(IntegralGBRuntimeSlot **slot)
{
    if (!slot || !*slot) {
        return;
    }
    integral_gb_runtime_slot_free(*slot);
    free(*slot);
    *slot = NULL;
}

static void close_stream_server_heap(IntegralGBRuntimeStreamServer **server, bool *is_open)
{
    if (!server || !*server) {
        return;
    }
    if (is_open && *is_open) {
        integral_gb_runtime_stream_server_close(*server);
        *is_open = false;
    }
    free(*server);
    *server = NULL;
}

static void release_macro_keys(GB_gameboy_t *gb)
{
    GB_set_key_state(gb, GB_KEY_UP, false);
    GB_set_key_state(gb, GB_KEY_DOWN, false);
    GB_set_key_state(gb, GB_KEY_LEFT, false);
    GB_set_key_state(gb, GB_KEY_RIGHT, false);
    GB_set_key_state(gb, GB_KEY_A, false);
    GB_set_key_state(gb, GB_KEY_B, false);
    GB_set_key_state(gb, GB_KEY_START, false);
    GB_set_key_state(gb, GB_KEY_SELECT, false);
}

static bool press_macro_key(GB_gameboy_t *gb, char key)
{
    switch (key) {
        case 'U':
        case 'u':
            GB_set_key_state(gb, GB_KEY_UP, true);
            return true;
        case 'D':
        case 'd':
            GB_set_key_state(gb, GB_KEY_DOWN, true);
            return true;
        case 'L':
        case 'l':
            GB_set_key_state(gb, GB_KEY_LEFT, true);
            return true;
        case 'R':
        case 'r':
            GB_set_key_state(gb, GB_KEY_RIGHT, true);
            return true;
        case 'A':
        case 'a':
            GB_set_key_state(gb, GB_KEY_A, true);
            return true;
        case 'B':
        case 'b':
            GB_set_key_state(gb, GB_KEY_B, true);
            return true;
        case 'S':
        case 's':
            GB_set_key_state(gb, GB_KEY_START, true);
            return true;
        case 'T':
        case 't':
            GB_set_key_state(gb, GB_KEY_SELECT, true);
            return true;
        case '.':
        case '-':
        case '_':
            return true;
        default:
            return false;
    }
}

static unsigned apply_slot_macro(IntegralGBRuntimeSlot *slot,
                                 const char *slot_name,
                                 const char *macro,
                                 unsigned frame,
                                 unsigned step,
                                 unsigned press,
                                 bool screenshots)
{
    if (!slot || !macro || macro[0] == '\0') {
        return 0;
    }
    release_macro_keys(slot->gb);
    unsigned offset = frame;
    size_t length = strlen(macro);
    size_t index = offset / step;
    if (index >= length || (offset % step) >= press) {
        return 0;
    }
    if (screenshots && (offset % step) == 0) {
        char screenshot_path[512];
        if (integral_gb_runtime_screenshot_save_slot(slot, screenshot_path, sizeof(screenshot_path)) == 0) {
            printf("  %s macro screenshot: frame=%u index=%zu key=%c path=%s\n",
                   slot_name,
                   frame,
                   index,
                   macro[index],
                   screenshot_path);
        }
        else {
            printf("  %s macro screenshot: frame=%u index=%zu key=%c failed\n",
                   slot_name,
                   frame,
                   index,
                   macro[index]);
        }
    }
    return press_macro_key(slot->gb, macro[index]) ? 1u : 0u;
}

static void poll_remote_stream_inputs(IntegralGBRuntimeStreamServer *stream_server,
                                      bool stream_server_open,
                                      IntegralGBRuntimeSlot *slot2,
                                      bool slot2_ready,
                                      IntegralGBRuntimeStreamServer *stream_server_slot1,
                                      bool stream_server_slot1_open,
                                      IntegralGBRuntimeSlot *slot1)
{
    if (stream_server_open) {
        integral_gb_runtime_stream_server_poll(stream_server);
        if (slot2_ready) {
            integral_gb_runtime_stream_server_apply_input(stream_server, slot2);
        }
    }
    if (stream_server_slot1_open) {
        integral_gb_runtime_stream_server_poll(stream_server_slot1);
        integral_gb_runtime_stream_server_apply_input(stream_server_slot1, slot1);
    }
}

static void wait_until_frame_deadline(uint64_t deadline_us,
                                      IntegralGBRuntimeStreamServer *stream_server,
                                      bool stream_server_open,
                                      IntegralGBRuntimeSlot *slot2,
                                      bool slot2_ready,
                                      IntegralGBRuntimeStreamServer *stream_server_slot1,
                                      bool stream_server_slot1_open,
                                      IntegralGBRuntimeSlot *slot1)
{
    while (!g_gb_runtime_server_shutdown_requested) {
        poll_remote_stream_inputs(stream_server,
                                  stream_server_open,
                                  slot2,
                                  slot2_ready,
                                  stream_server_slot1,
                                  stream_server_slot1_open,
                                  slot1);
        uint64_t now_us = integral_gb_runtime_now_us();
        if (now_us >= deadline_us) {
            break;
        }
        uint64_t remaining_us = deadline_us - now_us;
        if (remaining_us < 1000u) {
            break;
        }
        integral_gb_runtime_sleep_ms(remaining_us > 2000u ? 2u : 1u);
    }
}

static bool append_text(char *dest, size_t dest_size, size_t *used, const char *text)
{
    size_t len = strlen(text);
    if (*used >= dest_size || len >= dest_size - *used) {
        return false;
    }
    memcpy(dest + *used, text, len);
    *used += len;
    dest[*used] = '\0';
    return true;
}

static bool build_lan_remote_text(char *dest,
                                  size_t dest_size,
                                  const char *prefix,
                                  const char *host,
                                  unsigned port)
{
    char port_text[16];
    int n = snprintf(port_text, sizeof(port_text), "%u", port);
    if (n < 0 || (size_t)n >= sizeof(port_text)) {
        return false;
    }

    size_t used = 0;
    if (dest_size == 0) {
        return false;
    }
    dest[0] = '\0';
    return append_text(dest, dest_size, &used, prefix) &&
           append_text(dest, dest_size, &used, host) &&
           append_text(dest, dest_size, &used, ":") &&
           append_text(dest, dest_size, &used, port_text);
}

int main(int argc, char **argv)
{
    integral_gb_runtime_server_install_signal_handlers();
    integral_gb_runtime_log_redirect_stdio("server");
    ServerOptions options = {0};
    if (integral_gb_runtime_server_parse_options(argc, argv, &options) != 0) {
        integral_gb_runtime_server_print_usage(argv[0]);
        return 2;
    }

    printf("INTEGRAL EMULATOR GB Runtime server\n");
    printf("  protocol: %u\n", INTEGRAL_GB_RUNTIME_PROTOCOL_VERSION);
    printf("  bind: %s:%u\n", options.bind, options.port);
    printf("  slot1 rom: %s\n", options.rom1);
    printf("  slot1 save: %s\n", options.save1);
    bool slot2_from_client = !options.self_mode && options.remote_input_enabled && !options.rom2;
    char client_slot2_rom[512] = {0};
    char client_slot2_save[512] = {0};
    uint32_t client_slot2_unix_time = 0;
    const char *slot2_rom_path = options.rom2;
    const char *slot2_save_path = options.save2;
    if (!options.self_mode) {
        printf("  slot2 rom: %s\n", slot2_from_client ? "(client upload)" : slot2_rom_path);
        printf("  slot2 save: %s\n", slot2_from_client ? "(client upload)" : slot2_save_path);
    }
    printf("  video: %ux%u RGB565\n", INTEGRAL_GB_RUNTIME_GB_WIDTH, INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    printf("  frame smoke count: %u\n", options.frames);
    printf("  display: %s\n", options.display ? "on" : "off");
    printf("  boot rom: %s\n", options.skip_boot_rom ? "HLE skip" : "normal callback-required path");
    printf("  mode: %s\n", options.self_mode ? "self" : "dual-slot");
    printf("  lan remote: %s\n", options.lan_remote_enabled ? "on" : "off");
    printf("  speed: x%u%s\n",
           options.self_mode ? options.speed_multiplier : 1u,
           options.self_mode ? "" : " (self only)");
    printf("  auto A frames: %u\n", options.auto_a_frames);
    printf("  auto A pulse: %u\n", options.auto_a_pulse);
    printf("  display slots: %u\n", options.display_slots);
    printf("  local link: %s\n", options.link_enabled ? "on" : "off");
    printf("  remote input: %s\n", options.remote_input_enabled ? "on" : "off");
    printf("  remote input dual: %s\n", options.remote_input_dual_enabled ? "on" : "off");
    printf("  local audio: %s\n", options.local_audio_enabled ? "on" : "off");
    printf("  audio queue cap: %u ms\n", options.audio_max_ms);
    int64_t effective_rtc_offset_seconds = options.rtc_offset_seconds + (int64_t)options.rtc_offset_minutes * 60;
    printf("  rtc offset: %+lld second(s)%s\n",
           (long long)effective_rtc_offset_seconds,
           options.self_mode ? "" : " (dual-slot experimental)");
    IntegralGBRuntimeSlot *slot1 = calloc(1, sizeof(*slot1));
    IntegralGBRuntimeSlot *slot2 = calloc(1, sizeof(*slot2));
    if (!slot1 || !slot2) {
        fprintf(stderr, "slot allocation failed\n");
        free(slot1);
        free(slot2);
        return 1;
    }
    GB_model_t slot1_model;
    IntegralGBRuntimeRomProfile slot1_profile;
    IntegralGBRuntimeRomModelReason slot1_reason;
    if (integral_gb_runtime_slot_model_for_rom(options.rom1,
                                       &slot1_model,
                                       &slot1_profile,
                                       &slot1_reason) != 0) {
        free_slot_heap(&slot1);
        free_slot_heap(&slot2);
        return 1;
    }
    printf("  slot1 model: %s reason=%s cgb=%02X sgb=%02X old_licensee=%02X\n",
           integral_gb_runtime_slot_model_name(slot1_model),
           integral_gb_runtime_rom_model_reason_name(slot1_reason),
           slot1_profile.cgb_flag,
           slot1_profile.sgb_flag,
           slot1_profile.old_licensee);

    IntegralGBRuntimeSlotConfig slot1_config = {
        .name = "slot1",
        .rom_path = options.rom1,
        .save_path = options.save1,
        .model = slot1_model,
        .skip_boot_rom = options.skip_boot_rom,
    };

    if (integral_gb_runtime_slot_init(slot1, &slot1_config) != 0) {
        free_slot_heap(&slot1);
        free_slot_heap(&slot2);
        return 1;
    }
    if (effective_rtc_offset_seconds != 0) {
        int rtc_result = integral_gb_runtime_slot_apply_rtc_offset_seconds(slot1, effective_rtc_offset_seconds);
        printf("  slot1 rtc offset: %s\n", rtc_result > 0 ? "applied" : "not supported");
    }
    bool slot2_ready = false;

    IntegralGBRuntimeStreamServer *stream_server = calloc(1, sizeof(*stream_server));
    IntegralGBRuntimeStreamServer *stream_server_slot1 = NULL;
    if (!stream_server) {
        fprintf(stderr, "stream server allocation failed\n");
        free_slot_heap(&slot1);
        free_slot_heap(&slot2);
        return 1;
    }
    if (options.remote_input_dual_enabled) {
        stream_server_slot1 = calloc(1, sizeof(*stream_server_slot1));
        if (!stream_server_slot1) {
            fprintf(stderr, "slot1 stream server allocation failed\n");
            close_stream_server_heap(&stream_server, &(bool){false});
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            return 1;
        }
    }
    bool stream_server_open = false;
    bool stream_server_slot1_open = false;
    Uint32 next_mdns_ms = 0;
    if (options.remote_input_enabled) {
        if (integral_gb_runtime_stream_server_open(stream_server, options.bind, options.port) != 0) {
            close_stream_server_heap(&stream_server, &stream_server_open);
            close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            return 1;
        }
        integral_gb_runtime_stream_server_set_auth_token(stream_server, options.slot2_auth_token);
        stream_server_open = true;
        printf("  remote input: listening on %s:%u\n", options.bind, options.port);
        if (options.remote_input_dual_enabled) {
            if (options.port >= 65535 ||
                integral_gb_runtime_stream_server_open(stream_server_slot1, options.bind, options.port + 1) != 0) {
                close_stream_server_heap(&stream_server, &stream_server_open);
                close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
                free_slot_heap(&slot1);
                free_slot_heap(&slot2);
                return 1;
            }
            integral_gb_runtime_stream_server_set_auth_token(stream_server_slot1, options.slot1_auth_token);
            stream_server_slot1_open = true;
            printf("  remote input slot1: listening on %s:%u\n", options.bind, options.port + 1);
        }
        integral_gb_runtime_slot2_upload_advertise_mdns_if_due(options.port, &next_mdns_ms);
    }

    if (slot2_from_client) {
        IntegralGBRuntimeSlot2UploadWaitResult wait_result = {0};
        if (integral_gb_runtime_slot2_upload_wait(stream_server,
                                        &options,
                                        client_slot2_rom,
                                        sizeof(client_slot2_rom),
                                        client_slot2_save,
                                        sizeof(client_slot2_save),
                                        &client_slot2_unix_time,
                                        &wait_result) != 0) {
            close_stream_server_heap(&stream_server, &stream_server_open);
            close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            if (wait_result.return_to_menu_requested) {
                return exec_menu();
            }
            return 1;
        }
        slot2_rom_path = client_slot2_rom;
        slot2_save_path = client_slot2_save;
        printf("  slot2 rom: %s\n", slot2_rom_path);
        printf("  slot2 save: %s\n", slot2_save_path);
        printf("  slot2 client rtc: %s\n", client_slot2_unix_time ? "available" : "unavailable");
    }

    if (!options.self_mode) {
        GB_model_t slot2_model;
        IntegralGBRuntimeRomProfile slot2_profile;
        IntegralGBRuntimeRomModelReason slot2_reason;
        if (integral_gb_runtime_slot_model_for_rom(slot2_rom_path,
                                           &slot2_model,
                                           &slot2_profile,
                                           &slot2_reason) != 0) {
            close_stream_server_heap(&stream_server, &stream_server_open);
            close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            return 1;
        }
        printf("  slot2 model: %s reason=%s cgb=%02X sgb=%02X old_licensee=%02X\n",
               integral_gb_runtime_slot_model_name(slot2_model),
               integral_gb_runtime_rom_model_reason_name(slot2_reason),
               slot2_profile.cgb_flag,
               slot2_profile.sgb_flag,
               slot2_profile.old_licensee);
        IntegralGBRuntimeSlotConfig slot2_config = {
            .name = "slot2",
            .rom_path = slot2_rom_path,
            .save_path = slot2_save_path,
            .model = slot2_model,
            .skip_boot_rom = options.skip_boot_rom,
        };
        if (integral_gb_runtime_slot_init(slot2, &slot2_config) != 0) {
            close_stream_server_heap(&stream_server, &stream_server_open);
            close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            return 1;
        }
        if (effective_rtc_offset_seconds != 0) {
            int rtc_result = integral_gb_runtime_slot_apply_rtc_offset_seconds(slot2, effective_rtc_offset_seconds);
            printf("  slot2 rtc offset: %s\n", rtc_result > 0 ? "applied" : "not supported");
        }
        if (slot2_from_client && client_slot2_unix_time != 0) {
            int64_t client_delta_seconds = (int64_t)(uint64_t)client_slot2_unix_time - (int64_t)time(NULL);
            if (client_delta_seconds == 0) {
                printf("  slot2 client rtc sync: no adjustment needed (+0 second(s))\n");
            }
            else {
                int rtc_result = integral_gb_runtime_slot_apply_rtc_offset_seconds(slot2, client_delta_seconds);
                printf("  slot2 client rtc sync: %s (%+lld second(s))\n",
                       rtc_result > 0 ? "applied" : "not supported",
                       (long long)client_delta_seconds);
            }
        }
        slot2_ready = true;
    }

    printf("  slot instances: initialized\n");

    IntegralGBRuntimeLinkBridge link_bridge;
    bool link_connected = false;
    if (options.link_enabled && slot2_ready) {
        if (integral_gb_runtime_link_bridge_connect(&link_bridge, slot1, slot2) != 0) {
            fprintf(stderr, "Failed to attach the local Link serial peripheral\n");
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            return 1;
        }
        link_connected = true;
        printf("  local link: connected Slot 1 <-> Slot 2\n");
    }

    IntegralGBRuntimeVideoWindow *window = NULL;
    IntegralGBRuntimeInputRouter input_router;
    integral_gb_runtime_input_router_init(&input_router, slot1, slot2_ready ? slot2 : NULL);
    integral_gb_runtime_input_router_set_keymaps(&input_router,
                                       &options.slot1_keys,
                                       &options.slot2_keys,
                                       options.fast_key,
                                       options.screenshot_key,
                                       options.escape_key,
                                       options.self_mode && !options.lan_remote_enabled
                                           ? options.turbo_hold_key
                                           : SDLK_UNKNOWN,
                                       options.reset_key);
    integral_gb_runtime_input_router_set_speed_multiplier(&input_router, options.self_mode ? options.speed_multiplier : 1u);
    if (options.display) {
            integral_gb_runtime_input_router_print_keymap(&input_router);
            if (options.self_mode) {
                printf("  speed cycle: press %s for x1/x2/x3/x4\n", integral_gb_runtime_key_config_key_name(options.fast_key));
                printf("  screenshot: press %s\n", integral_gb_runtime_key_config_key_name(options.screenshot_key));
                printf("  reset: press %s\n", integral_gb_runtime_key_config_key_name(options.reset_key));
            }
            if (options.self_mode && !options.lan_remote_enabled) {
                printf("  turbo hold: hold %s and press buttons; press %s alone to clear\n",
                       integral_gb_runtime_key_config_key_name(options.turbo_hold_key),
                       integral_gb_runtime_key_config_key_name(options.turbo_hold_key));
            }
    }
    char window_title[128];
    snprintf(window_title,
             sizeof(window_title),
             "INTEGRAL EMULATOR - GB Runtime (%s mode)",
             integral_gb_runtime_server_window_mode_name(&options));
    if (options.display &&
        integral_gb_runtime_video_window_open_titled(&window, options.scale, options.display_slots, window_title) != 0) {
        close_stream_server_heap(&stream_server, &stream_server_open);
        close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
        if (slot2_ready) {
            free_slot_heap(&slot2);
        }
        free_slot_heap(&slot1);
        return 1;
    }

    IntegralGBRuntimeAudioPlayer *audio_player = NULL;
    if (options.local_audio_enabled &&
        integral_gb_runtime_audio_player_open(&audio_player, options.audio_max_ms) != 0) {
        integral_gb_runtime_video_window_close(window);
        if (stream_server_open) {
            close_stream_server_heap(&stream_server, &stream_server_open);
        }
        if (stream_server_slot1_open) {
            close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
        }
        if (link_connected) {
            integral_gb_runtime_link_bridge_disconnect(&link_bridge);
        }
        if (slot2_ready) {
            free_slot_heap(&slot2);
        }
        free_slot_heap(&slot1);
        return 1;
    }

    IntegralGBRuntimeLanRemote lan_remote;
    memset(&lan_remote, 0, sizeof(lan_remote));
    bool lan_remote_open = false;
    if (options.lan_remote_enabled) {
        if (integral_gb_runtime_lan_remote_open(&lan_remote, options.lan_remote_port) != 0) {
            fprintf(stderr, "Failed to start LAN remote on port %u\n", options.lan_remote_port);
            integral_gb_runtime_audio_player_close(audio_player);
            integral_gb_runtime_video_window_close(window);
            if (link_connected) {
                integral_gb_runtime_link_bridge_disconnect(&link_bridge);
            }
            free_slot_heap(&slot1);
            free_slot_heap(&slot2);
            return 1;
        }
        lan_remote_open = true;
        char remote_url[128];
        if (!build_lan_remote_text(remote_url,
                                   sizeof(remote_url),
                                   "LAN REMOTE http://",
                                   integral_gb_runtime_lan_remote_host(&lan_remote),
                                   integral_gb_runtime_lan_remote_port(&lan_remote))) {
            (void)integral_gb_runtime_copy_text(remote_url, sizeof(remote_url), "LAN REMOTE READY");
        }
        printf("  lan remote url: http://%s:%u/\n",
               integral_gb_runtime_lan_remote_host(&lan_remote),
               integral_gb_runtime_lan_remote_port(&lan_remote));
        char remote_status[128];
        if (!build_lan_remote_text(remote_status,
                                   sizeof(remote_status),
                                   "LAN ",
                                   integral_gb_runtime_lan_remote_host(&lan_remote),
                                   integral_gb_runtime_lan_remote_port(&lan_remote))) {
            (void)integral_gb_runtime_copy_text(remote_status, sizeof(remote_status), "LAN REMOTE");
        }
        integral_gb_runtime_video_window_set_status_message(window, remote_status);
        integral_gb_runtime_video_window_show_message(window, remote_url);
    }

    unsigned frames_to_run = options.frames;
    unsigned frame = 0;
    signed delta = 0;
    int16_t audio_scratch[INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES * INTEGRAL_GB_RUNTIME_AUDIO_CHANNELS];
    unsigned link_wait_slot1_for_slot2 = 0;
    unsigned link_wait_slot2_for_slot1 = 0;
    unsigned link_wait_ir_trace_count = 0;
    unsigned link_wait_ir_trace_nonzero = 0;
    unsigned link_wait_ir_trace_frame[128];
    unsigned link_wait_ir_trace_ir1[128];
    unsigned link_wait_ir_trace_ir2[128];
    unsigned link_wait_ir_last_slot1 = 0;
    unsigned link_wait_ir_last_slot2 = 0;
    unsigned slot1_macro_events = 0;
    unsigned slot2_macro_events = 0;
    uint64_t next_frame_deadline_us = integral_gb_runtime_now_us();
    bool return_to_menu = false;
    bool smoke_screenshot_saved = false;
    while (!g_gb_runtime_server_shutdown_requested &&
           (options.display || options.remote_input_enabled || frame < frames_to_run)) {
        if (frames_to_run > 0 && frame >= frames_to_run) {
            break;
        }
        bool auto_a_pressed = options.auto_a_frames > 0 && frame < options.auto_a_frames;
        if (auto_a_pressed && options.auto_a_pulse > 1) {
            unsigned half_period = options.auto_a_pulse / 2;
            if (half_period == 0) {
                half_period = 1;
            }
            auto_a_pressed = (frame % options.auto_a_pulse) < half_period;
        }
        GB_set_key_state(slot1->gb, GB_KEY_A, auto_a_pressed);
        if (slot2_ready) {
            GB_set_key_state(slot2->gb, GB_KEY_A, auto_a_pressed);
        }
        slot1_macro_events += apply_slot_macro(slot1,
                                               "slot1",
                                               options.slot1_macro,
                                               frame,
                                               options.macro_step_frames,
                                               options.macro_press_frames,
                                               options.macro_screenshots);
        if (slot2_ready) {
            slot2_macro_events += apply_slot_macro(slot2,
                                                   "slot2",
                                                   options.slot2_macro,
                                                   frame,
                                                   options.macro_step_frames,
                                                   options.macro_press_frames,
                                                   options.macro_screenshots);
        }
        if (window) {
            IntegralGBRuntimeVideoWindowPollResult poll_result =
                integral_gb_runtime_video_window_poll(window, &input_router, slot1, slot2_ready ? slot2 : NULL);
            if (poll_result == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CLOSE) {
                break;
            }
            if (poll_result == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_RETURN_MENU) {
                return_to_menu = true;
                break;
            }
        }
        if (window && options.self_mode &&
            integral_gb_runtime_input_router_take_speed_multiplier_changed(&input_router)) {
            char speed_message[16];
            snprintf(speed_message,
                     sizeof(speed_message),
                     "SPEED X%u",
                     integral_gb_runtime_input_router_speed_multiplier(&input_router));
            integral_gb_runtime_video_window_show_speed_message(window, speed_message);
        }
        if (lan_remote_open) {
            integral_gb_runtime_lan_remote_poll(&lan_remote, slot1);
            integral_gb_runtime_lan_remote_apply_input(&lan_remote, slot1);
        }
        integral_gb_runtime_input_router_update_turbo(&input_router);
        if (window && options.self_mode && !lan_remote_open) {
            char turbo_status[64];
            if (integral_gb_runtime_input_router_describe_turbo(&input_router,
                                                       turbo_status,
                                                       sizeof(turbo_status))) {
                integral_gb_runtime_video_window_set_status_message(window, turbo_status);
            }
            else {
                integral_gb_runtime_video_window_set_status_message(window, "");
            }
        }
        if (options.self_mode && integral_gb_runtime_input_router_take_screenshot_request(&input_router)) {
            char screenshot_path[512];
            if (integral_gb_runtime_screenshot_save_slot(slot1, screenshot_path, sizeof(screenshot_path)) == 0) {
                printf("  screenshot saved: %s\n", screenshot_path);
                integral_gb_runtime_video_window_show_message(window, "SCREENSHOT SAVED");
            }
            else {
                fprintf(stderr, "Failed to save screenshot\n");
                integral_gb_runtime_video_window_show_message(window, "SCREENSHOT FAILED");
            }
        }
        if (options.self_mode && integral_gb_runtime_input_router_take_reset_request(&input_router)) {
            integral_gb_runtime_slot_reset(slot1);
            if (slot2_ready) {
                integral_gb_runtime_slot_reset(slot2);
            }
            integral_gb_runtime_video_window_show_message(window, "RESET");
        }
        if (stream_server_open) {
            integral_gb_runtime_slot2_upload_advertise_mdns_if_due(options.port, &next_mdns_ms);
        }
        poll_remote_stream_inputs(stream_server,
                                  stream_server_open,
                                  slot2,
                                  slot2_ready,
                                  stream_server_slot1,
                                  stream_server_slot1_open,
                                  slot1);
        bool advanced_frame = false;
        if (link_connected) {
            link_bridge.current_frame = frame;
            slot1->vblank_occurred = false;
            slot2->vblank_occurred = false;
            while (!slot1->vblank_occurred || !slot2->vblank_occurred) {
                bool slot1_master_waiting = integral_gb_runtime_slot_serial_internal_clock(slot1) &&
                                            !integral_gb_runtime_slot_serial_active(slot2);
                bool slot2_master_waiting = integral_gb_runtime_slot_serial_internal_clock(slot2) &&
                                            !integral_gb_runtime_slot_serial_active(slot1);
                if (slot1_master_waiting && !slot2->vblank_occurred) {
                    delta += (signed)integral_gb_runtime_slot_run_until_sync(slot2);
                    link_bridge.slot2_syncs++;
                    link_wait_slot1_for_slot2++;
                    unsigned ir1_delta = link_bridge.slot1_ir_edges - link_wait_ir_last_slot1;
                    unsigned ir2_delta = link_bridge.slot2_ir_edges - link_wait_ir_last_slot2;
                    if (ir1_delta != 0 || ir2_delta != 0) {
                        link_wait_ir_trace_nonzero++;
                        if (link_wait_ir_trace_count < 128) {
                            link_wait_ir_trace_frame[link_wait_ir_trace_count] = frame;
                            link_wait_ir_trace_ir1[link_wait_ir_trace_count] = ir1_delta;
                            link_wait_ir_trace_ir2[link_wait_ir_trace_count] = ir2_delta;
                            link_wait_ir_trace_count++;
                        }
                    }
                    link_wait_ir_last_slot1 = link_bridge.slot1_ir_edges;
                    link_wait_ir_last_slot2 = link_bridge.slot2_ir_edges;
                }
                else if (slot2_master_waiting && !slot1->vblank_occurred) {
                    delta -= (signed)integral_gb_runtime_slot_run_until_sync(slot1);
                    link_bridge.slot1_syncs++;
                    link_wait_slot2_for_slot1++;
                }
                else if (delta >= 0) {
                    delta -= (signed)integral_gb_runtime_slot_run_until_sync(slot1);
                    link_bridge.slot1_syncs++;
                }
                else {
                    delta += (signed)integral_gb_runtime_slot_run_until_sync(slot2);
                    link_bridge.slot2_syncs++;
                }
            }
            advanced_frame = true;
        }
        else {
            unsigned frames_this_tick = 1;
            if (options.self_mode) {
                if (lan_remote_open && integral_gb_runtime_lan_remote_fast_enabled(&lan_remote)) {
                    frames_this_tick = 2u;
                }
                else {
                    frames_this_tick = integral_gb_runtime_input_router_speed_multiplier(&input_router);
                }
            }
            if (frames_to_run > 0 && frame + frames_this_tick > frames_to_run) {
                frames_this_tick = frames_to_run - frame;
            }
            if (lan_remote_open && integral_gb_runtime_lan_remote_paused(&lan_remote)) {
                SDL_Delay(16);
            }
            else {
                if (integral_gb_runtime_slot_run_frames(slot1, frames_this_tick) != 0 ||
                    (slot2_ready && integral_gb_runtime_slot_run_frames(slot2, frames_this_tick) != 0)) {
                    fprintf(stderr, "Failed to advance slot frames\n");
                    integral_gb_runtime_lan_remote_close(&lan_remote);
                    integral_gb_runtime_video_window_close(window);
                    close_stream_server_heap(&stream_server, &stream_server_open);
                    close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
                    if (slot2_ready) {
                        free_slot_heap(&slot2);
                    }
                    free_slot_heap(&slot1);
                    return 1;
                }
                if (frames_this_tick > 1) {
                    frame += frames_this_tick - 1;
                }
                advanced_frame = true;
            }
        }
        if (window && integral_gb_runtime_video_window_render(window, slot1, slot2_ready ? slot2 : NULL) != 0) {
            if (link_connected) {
                integral_gb_runtime_link_bridge_disconnect(&link_bridge);
            }
            integral_gb_runtime_audio_player_close(audio_player);
            integral_gb_runtime_lan_remote_close(&lan_remote);
            integral_gb_runtime_video_window_close(window);
            close_stream_server_heap(&stream_server, &stream_server_open);
            close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
            if (slot2_ready) {
                free_slot_heap(&slot2);
            }
            free_slot_heap(&slot1);
            return 1;
        }
        if (lan_remote_open) {
            integral_gb_runtime_lan_remote_update_frame(&lan_remote, slot1);
        }
        if (options.self_mode && options.smoke_screenshot && !smoke_screenshot_saved) {
            char screenshot_path[512];
            if (integral_gb_runtime_screenshot_save_slot(slot1, screenshot_path, sizeof(screenshot_path)) == 0) {
                printf("  screenshot saved: %s\n", screenshot_path);
                smoke_screenshot_saved = true;
                integral_gb_runtime_video_window_show_message(window, "SCREENSHOT SAVED");
            }
            else {
                fprintf(stderr, "Failed to save screenshot\n");
                integral_gb_runtime_video_window_show_message(window, "SCREENSHOT FAILED");
                integral_gb_runtime_lan_remote_close(&lan_remote);
                integral_gb_runtime_video_window_close(window);
                close_stream_server_heap(&stream_server, &stream_server_open);
                close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
                free_slot_heap(&slot1);
                return 1;
            }
        }
        if (audio_player || lan_remote_open || stream_server_slot1_open) {
            unsigned audio_frames = integral_gb_runtime_slot_drain_audio(slot1,
                                                                audio_scratch,
                                                                INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES);
            if (audio_player) {
                integral_gb_runtime_audio_player_queue_samples(audio_player, audio_scratch, audio_frames);
            }
            if (lan_remote_open) {
                integral_gb_runtime_lan_remote_queue_audio(&lan_remote, audio_scratch, audio_frames);
            }
            if (stream_server_slot1_open) {
                integral_gb_runtime_stream_server_queue_audio(stream_server_slot1, audio_scratch, audio_frames);
                integral_gb_runtime_stream_server_poll(stream_server_slot1);
            }
        }
        if (stream_server_slot1_open) {
            integral_gb_runtime_stream_server_queue_frame(stream_server_slot1, slot1);
            integral_gb_runtime_stream_server_poll(stream_server_slot1);
        }
        if (stream_server_open && slot2_ready) {
            unsigned audio_frames = integral_gb_runtime_slot_drain_audio(slot2,
                                                                audio_scratch,
                                                                INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES);
            integral_gb_runtime_stream_server_queue_audio(stream_server, audio_scratch, audio_frames);
            integral_gb_runtime_stream_server_poll(stream_server);
            integral_gb_runtime_stream_server_queue_frame(stream_server, slot2);
            integral_gb_runtime_stream_server_poll(stream_server);
            if (slot2_from_client) {
                integral_gb_runtime_server_send_slot2_save_if_dirty(slot2, stream_server, slot2_save_path);
            }
        }
        integral_gb_runtime_server_save_local_slot_if_dirty(slot1);
        if (slot2_ready && !slot2_from_client) {
            integral_gb_runtime_server_save_local_slot_if_dirty(slot2);
        }
        if (advanced_frame) {
            frame++;
        }
        if (!options.display && options.remote_input_enabled && frames_to_run == 0) {
            if (advanced_frame) {
                next_frame_deadline_us += INTEGRAL_GB_RUNTIME_FRAME_INTERVAL_US;
                uint64_t now_us = integral_gb_runtime_now_us();
                if (now_us > next_frame_deadline_us + INTEGRAL_GB_RUNTIME_MAX_FRAME_DEADLINE_LAG_US) {
                    next_frame_deadline_us = now_us;
                }
                wait_until_frame_deadline(next_frame_deadline_us,
                                          stream_server,
                                          stream_server_open,
                                          slot2,
                                          slot2_ready,
                                          stream_server_slot1,
                                          stream_server_slot1_open,
                                          slot1);
            }
            else {
                integral_gb_runtime_sleep_ms(1);
            }
        }
    }
    if (frame > 0) {
        printf("  slot frames: advanced %u frame(s) each\n", frame);
        printf("  slot1 pixels checksum: %08X\n", integral_gb_runtime_slot_pixel_checksum(slot1));
        if (slot2_ready) {
            printf("  slot2 pixels checksum: %08X\n", integral_gb_runtime_slot_pixel_checksum(slot2));
        }
    }
    if (options.slot1_macro || options.slot2_macro) {
        printf("  slot macros: slot1=%s events=%u slot2=%s events=%u step=%u press=%u\n",
               options.slot1_macro ? options.slot1_macro : "",
               slot1_macro_events,
               options.slot2_macro ? options.slot2_macro : "",
               slot2_macro_events,
               options.macro_step_frames,
               options.macro_press_frames);
    }
    if (options.dump_screenshot) {
        char screenshot_path[512];
        if (integral_gb_runtime_screenshot_save_slot(slot1, screenshot_path, sizeof(screenshot_path)) == 0) {
            printf("  slot1 screenshot: %s\n", screenshot_path);
        }
        if (slot2_ready &&
            integral_gb_runtime_screenshot_save_slot(slot2, screenshot_path, sizeof(screenshot_path)) == 0) {
            printf("  slot2 screenshot: %s\n", screenshot_path);
        }
    }
    if (link_connected) {
        integral_gb_runtime_link_bridge_print_stats(&link_bridge);
        printf("  link serial wait assists: slot1_waited_for_slot2=%u slot2_waited_for_slot1=%u\n",
               link_wait_slot1_for_slot2,
               link_wait_slot2_for_slot1);
        printf("  link serial wait ir trace: nonzero=%u shown=%u\n",
               link_wait_ir_trace_nonzero,
               link_wait_ir_trace_count);
        printf("  link serial wait ir trace first:");
        for (unsigned i = 0; i < link_wait_ir_trace_count; i++) {
            printf(" #%u:f%u:ir%u/%u",
                   i,
                   link_wait_ir_trace_frame[i],
                   link_wait_ir_trace_ir1[i],
                   link_wait_ir_trace_ir2[i]);
        }
        printf("\n");
    }
    if (slot2_from_client && slot2_ready && stream_server_open) {
        integral_gb_runtime_server_send_slot2_save_on_shutdown(slot2, stream_server, slot2_save_path);
    }
    if (stream_server_open) {
        integral_gb_runtime_stream_server_print_stats(stream_server);
    }
    if (stream_server_slot1_open) {
        integral_gb_runtime_stream_server_print_stats(stream_server_slot1);
    }
    integral_gb_runtime_audio_player_print_stats(audio_player);

    integral_gb_runtime_audio_player_close(audio_player);
    integral_gb_runtime_lan_remote_close(&lan_remote);
    close_stream_server_heap(&stream_server, &stream_server_open);
    close_stream_server_heap(&stream_server_slot1, &stream_server_slot1_open);
    if (link_connected) {
        integral_gb_runtime_link_bridge_disconnect(&link_bridge);
    }
    integral_gb_runtime_video_window_close(window);
    if (slot2_ready) {
        free_slot_heap(&slot2);
    }
    free_slot_heap(&slot1);
    integral_gb_runtime_server_remove_temp_slot2_uploads(slot2_from_client,
                                               client_slot2_rom,
                                               client_slot2_save);
    if (return_to_menu) {
        return exec_menu();
    }
    return 0;
}

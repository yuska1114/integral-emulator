/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "media_relay_client.h"
#include "n64_runtime_media_stream.h"
#include "sdl_text.h"
#include "gb_runtime_fixed_host_runtime.h"
#include "gb_runtime_fixed_host_product_runtime.h"
#include "gb_runtime_fixed_host_scheduler.h"
#include "gb_runtime_fixed_host_snapshot_ipc.h"
#include "gb_runtime_fixed_host_result_ipc.h"
#include "../runtimes/gb/src/common/key_config.h"
#include "../runtimes/gb/src/server/content_hash.h"
#include "../runtimes/gb/src/server/secure_memory.h"

#include <SDL.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

#define GB_FIXED_SCOPE "gb-runtime-fixed-host-media-v1"
#define GB_FIXED_TOKEN_ENV "INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TICKET"
#define GB_WIDTH 160u
#define GB_HEIGHT 144u
#define GB_DISPLAY_SCALE 3u
#define METRICS_US UINT64_C(5000000)
#define INPUT_HEARTBEAT_US UINT64_C(250000)
#define PING_INTERVAL_US UINT64_C(1000000)

typedef struct Options {
    const char *role;
    const char *relay_host;
    unsigned relay_port;
    const char *relay_transport;
    const char *session_id;
    const char *rom1;
    const char *rom2;
    const char *ca_file;
    const char *test_macro_host;
    const char *test_macro_remote;
    unsigned test_macro_press_frames;
    unsigned test_macro_step_frames;
    IntegralGBRuntimeKeyConfig keys;
    bool snapshot_stdin;
    IntegralGBRuntimeFixedHostSnapshotPair snapshots;
    intptr_t result_handle;
    const char *headless_receipt_file;
    bool commit_pair;
} Options;

typedef struct HostVideo {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
} HostVideo;

static uint64_t now_us(void)
{
    uint64_t frequency = SDL_GetPerformanceFrequency();
    return frequency ? SDL_GetPerformanceCounter() * UINT64_C(1000000) / frequency
                     : SDL_GetTicks64() * UINT64_C(1000);
}

static void put_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24u); data[1] = (uint8_t)(value >> 16u);
    data[2] = (uint8_t)(value >> 8u); data[3] = (uint8_t)value;
}

static void put_be64(uint8_t *data, uint64_t value)
{
    for (int index = 7; index >= 0; index--) {
        data[index] = (uint8_t)value;
        value >>= 8u;
    }
}

static uint32_t get_be32(const uint8_t *data)
{
    return (uint32_t)data[0] << 24u | (uint32_t)data[1] << 16u |
           (uint32_t)data[2] << 8u | data[3];
}

static uint64_t get_be64(const uint8_t *data)
{
    uint64_t value = 0;
    for (unsigned index = 0; index < 8; index++) value = value << 8u | data[index];
    return value;
}

static void encode_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8u);
    data[2] = (uint8_t)(value >> 16u);
    data[3] = (uint8_t)(value >> 24u);
}

static int write_slot_bmp(const char *path, const IntegralGBRuntimeSlot *slot)
{
    enum { header_size = 54, pixel_size = GB_WIDTH * GB_HEIGHT * 4u };
    if (!path || !path[0] || !slot || !slot->initialized) return -1;
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    uint8_t header[header_size] = {0};
    header[0] = 'B'; header[1] = 'M';
    encode_u32_le(header + 2u, header_size + pixel_size);
    encode_u32_le(header + 10u, header_size);
    encode_u32_le(header + 14u, 40u);
    encode_u32_le(header + 18u, GB_WIDTH);
    encode_u32_le(header + 22u, (uint32_t)(-(int32_t)GB_HEIGHT));
    header[26] = 1u; header[28] = 32u;
    encode_u32_le(header + 34u, pixel_size);
    if (fwrite(header, 1u, sizeof(header), file) != sizeof(header)) {
        fclose(file); return -1;
    }
    uint8_t row[GB_WIDTH * 4u];
    for (unsigned y = 0u; y < GB_HEIGHT; y++) {
        for (unsigned x = 0u; x < GB_WIDTH; x++) {
            uint32_t pixel = slot->pixels[y * GB_WIDTH + x];
            row[x * 4u] = (uint8_t)pixel;
            row[x * 4u + 1u] = (uint8_t)(pixel >> 8u);
            row[x * 4u + 2u] = (uint8_t)(pixel >> 16u);
            row[x * 4u + 3u] = 0xffu;
        }
        if (fwrite(row, 1u, sizeof(row), file) != sizeof(row)) {
            fclose(file); return -1;
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage: %s --role host|remote --relay-host HOST --session SESSION "
            "[--relay-port 25164] --relay-transport tls|plain "
            "[--ca FILE] [--rom1 FILE --rom2 FILE] "
            "[--snapshot-stdin]\n",
            program);
}

static int parse_options(int argc, char **argv, Options *options)
{
    memset(options, 0, sizeof(*options));
    options->relay_port = 25164u;
    options->test_macro_host = "A|A";
    options->test_macro_remote = "A|A";
    options->test_macro_press_frames = 8u;
    options->test_macro_step_frames = 60u;
    integral_gb_runtime_key_config_slot1_default(&options->keys);
    for (int index = 1; index < argc; index++) {
        const char *name = argv[index];
        if (strcmp(name, "--snapshot-stdin") == 0) {
            options->snapshot_stdin = true;
            continue;
        }
        if (index + 1 >= argc) return -1;
        const char *value = argv[++index];
        if (strcmp(name, "--role") == 0) options->role = value;
        else if (strcmp(name, "--relay-host") == 0) options->relay_host = value;
        else if (strcmp(name, "--relay-port") == 0) {
            char *end = NULL;
            unsigned long port = strtoul(value, &end, 10);
            if (!end || *end || !port || port > 65535u) return -1;
            options->relay_port = (unsigned)port;
        }
        else if (strcmp(name, "--relay-transport") == 0) options->relay_transport = value;
        else if (strcmp(name, "--session") == 0) options->session_id = value;
        else if (strcmp(name, "--rom1") == 0) options->rom1 = value;
        else if (strcmp(name, "--rom2") == 0) options->rom2 = value;
        else if (strcmp(name, "--ca") == 0) options->ca_file = value;
        else return -1;
    }
    {
        const char *value;
        if (!options->role && (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROLE")))
            options->role = value;
        if (!options->relay_host && (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_HOST")))
            options->relay_host = value;
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_PORT")))
            options->relay_port = (unsigned)strtoul(value, NULL, 10);
        if (!options->relay_transport &&
            (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RELAY_TRANSPORT")))
            options->relay_transport = value;
        if (!options->session_id && (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SESSION")))
            options->session_id = value;
        if (!options->rom1 && (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM1")))
            options->rom1 = value;
        if (!options->rom2 && (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_ROM2")))
            options->rom2 = value;
        if (!options->ca_file && (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_CA")))
            options->ca_file = value;
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TEST_MACRO_HOST")))
            options->test_macro_host = value;
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TEST_MACRO_REMOTE")))
            options->test_macro_remote = value;
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TEST_MACRO_PRESS_FRAMES")))
            options->test_macro_press_frames = (unsigned)strtoul(value, NULL, 10);
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TEST_MACRO_STEP_FRAMES")))
            options->test_macro_step_frames = (unsigned)strtoul(value, NULL, 10);
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_RESULT_HANDLE")))
            options->result_handle = (intptr_t)strtoll(value, NULL, 10);
        if (getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_HEADLESS_SMOKE") &&
            (value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_HEADLESS_RECEIPT_FILE")))
            options->headless_receipt_file = value;
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SAVE_POLICY")))
            options->commit_pair = strcmp(value, "commit_pair") == 0;
        if ((value = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_KEYS")) &&
            integral_gb_runtime_key_config_parse(&options->keys, value) != 0) return -1;
    }
    if (!options->role || !options->relay_host || !options->session_id ||
        !options->relay_transport ||
        (strcmp(options->relay_transport, "tls") != 0 &&
         strcmp(options->relay_transport, "plain") != 0)) return -1;
    bool host = strcmp(options->role, "host") == 0;
    if (!host && strcmp(options->role, "remote") != 0) return -1;
    if (host && (!options->rom1 || !options->rom2)) return -1;
    if (host && !options->snapshot_stdin) return -1;
    return 0;
}

static ptrdiff_t result_write_callback(void *context, const uint8_t *data, size_t size)
{
#ifdef _WIN32
    DWORD written = 0u;
    HANDLE handle = (HANDLE)(intptr_t)context;
    if (!WriteFile(handle, data, (DWORD)size, &written, NULL)) return -1;
    return (ptrdiff_t)written;
#else
    int descriptor = (int)(intptr_t)context;
    ssize_t written = write(descriptor, data, size);
    return written < 0 ? -1 : (ptrdiff_t)written;
#endif
}

static ptrdiff_t result_file_write_callback(void *context, const uint8_t *data, size_t size)
{
    FILE *file = context;
    size_t written = fwrite(data, 1u, size, file);
    return written ? (ptrdiff_t)written : -1;
}

static bool send_result(const Options *options, IntegralGBRuntimeFixedHostResult *result)
{
    if (options->result_handle > 0) {
        return integral_gb_runtime_fixed_host_result_ipc_send(
            result_write_callback, (void *)options->result_handle, result);
    }
    if (options->headless_receipt_file && options->headless_receipt_file[0]) {
        FILE *file = fopen(options->headless_receipt_file, "wb");
        if (!file) {
            integral_gb_runtime_fixed_host_result_release(result);
            return false;
        }
        bool success = integral_gb_runtime_fixed_host_result_ipc_send(
            result_file_write_callback, file, result);
        if (fclose(file) != 0) success = false;
        return success;
    }
    integral_gb_runtime_fixed_host_result_release(result);
    return false;
}

static void gb_runtime_fixed_host_terminal_digest(
    const IntegralGBRuntimeLinkSnapshot *snapshot, uint8_t digest[32])
{
    IntegralGBRuntimeContentSha256 hash;
    uint8_t frame[8];
    put_be64(frame, snapshot->logical_frame);
    integral_gb_runtime_content_sha256_init(&hash);
    integral_gb_runtime_content_sha256_update(&hash, "gb-runtime-fixed-host-terminal-v1", 22u);
    integral_gb_runtime_content_sha256_update(&hash, frame, sizeof(frame));
    integral_gb_runtime_content_sha256_update(&hash, snapshot->pair_sha256, 32u);
    integral_gb_runtime_content_sha256_update(&hash, snapshot->battery_a_sha256, 32u);
    integral_gb_runtime_content_sha256_update(&hash, snapshot->battery_b_sha256, 32u);
    integral_gb_runtime_content_sha256_finish(&hash, digest);
    integral_gb_runtime_secure_zero(&hash, sizeof(hash));
}

static ptrdiff_t file_read_callback(void *context, uint8_t *data, size_t size)
{
    size_t received = fread(data, 1u, size, (FILE *)context);
    if (received == 0u && ferror((FILE *)context)) return -1;
    return (ptrdiff_t)received;
}

static IntegralGBRuntimeFixedHostExitSelection poll_input(
                       IntegralGBRuntimeFixedHostProductRuntime *product,
                       IntegralN64RuntimeMediaStream *stream,
                       const Options *options)
{
    SDL_Event event;
    bool headless_smoke =
        getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_HEADLESS_SMOKE") != NULL;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_JOYDEVICEADDED ||
            event.type == SDL_CONTROLLERDEVICEREMOVED || event.type == SDL_JOYDEVICEREMOVED) {
            integral_gb_runtime_key_config_handle_device_event(&event);
            continue;
        }
        bool close_requested = event.type == SDL_QUIT && !headless_smoke;
        if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE &&
            !headless_smoke &&
            (!stream || integral_n64_runtime_media_stream_is_video_window(stream, event.window.windowID))) {
            close_requested = true;
        }
        if (close_requested) {
            integral_gb_runtime_fixed_host_product_runtime_request_exit(product);
            integral_n64_runtime_media_stream_set_exit_confirmation(
                stream, true, product->exit_confirm_yes);
            continue;
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
            integral_gb_runtime_fixed_host_product_runtime_focus_lost(product);
            continue;
        }
        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
            product->focused = true;
            continue;
        }
        uint8_t press_mask = 0u, release_mask = 0u;
        integral_gb_runtime_key_config_buttons_for_event(
            &options->keys, &event, &press_mask, &release_mask);
        bool escape_pressed = !headless_smoke && event.type == SDL_KEYDOWN && !event.key.repeat &&
                              event.key.keysym.sym == SDLK_ESCAPE;
        bool enter_pressed = event.type == SDL_KEYDOWN && !event.key.repeat &&
                             (event.key.keysym.sym == SDLK_RETURN ||
                              event.key.keysym.sym == SDLK_KP_ENTER);
        bool left_right_pressed = event.type == SDL_KEYDOWN && !event.key.repeat &&
                                  (event.key.keysym.sym == SDLK_LEFT ||
                                   event.key.keysym.sym == SDLK_RIGHT);
        bool pressed = false;
        uint8_t menu_button = integral_gb_runtime_key_config_button_for_event(
            &options->keys, &event, &pressed);
        if (product->exit_confirming) {
            if (escape_pressed) {
                integral_gb_runtime_fixed_host_product_runtime_cancel_exit(product);
            }
            else if (left_right_pressed ||
                     (pressed && (menu_button & (INTEGRAL_GB_RUNTIME_BTN_LEFT |
                                                 INTEGRAL_GB_RUNTIME_BTN_RIGHT)))) {
                integral_gb_runtime_fixed_host_product_runtime_toggle_exit_selection(product);
            }
            else if (enter_pressed || (pressed && (menu_button & INTEGRAL_GB_RUNTIME_BTN_A))) {
                IntegralGBRuntimeFixedHostExitSelection selection =
                    integral_gb_runtime_fixed_host_product_runtime_select_exit(product);
                if (selection == INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONFIRMED)
                    return selection;
            }
            integral_n64_runtime_media_stream_set_exit_confirmation(
                stream, product->exit_confirming, product->exit_confirm_yes);
            continue;
        }
        if (escape_pressed) {
            integral_gb_runtime_fixed_host_product_runtime_request_exit(product);
            integral_n64_runtime_media_stream_set_exit_confirmation(
                stream, true, product->exit_confirm_yes);
            continue;
        }
        if (press_mask) integral_gb_runtime_fixed_host_product_runtime_set_button(
            product, press_mask, true);
        if (release_mask) integral_gb_runtime_fixed_host_product_runtime_set_button(
            product, release_mask, false);
        if (press_mask || release_mask) continue;
        if (event.type != SDL_KEYDOWN && event.type != SDL_KEYUP) continue;
        if (event.key.repeat) continue;
    }
    return INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONTINUE;
}

static int host_video_open(HostVideo *video)
{
    video->window = SDL_CreateWindow("INTEGRAL EMULATOR - GB FIXED HOST SLOT1",
                                     SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                     (int)(GB_WIDTH * GB_DISPLAY_SCALE),
                                     (int)(GB_HEIGHT * GB_DISPLAY_SCALE), SDL_WINDOW_SHOWN);
    if (!video->window) return -1;
    video->renderer = SDL_CreateRenderer(video->window, -1,
                                          SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!video->renderer) video->renderer = SDL_CreateRenderer(video->window, -1, 0);
    if (!video->renderer) return -1;
    video->texture = SDL_CreateTexture(video->renderer, SDL_PIXELFORMAT_ARGB8888,
                                       SDL_TEXTUREACCESS_STREAMING, GB_WIDTH, GB_HEIGHT);
    if (!video->texture) return -1;
    return 0;
}

static void host_video_render(HostVideo *video,
                              const IntegralGBRuntimeLinkEngine *engine,
                              const IntegralGBRuntimeFixedHostProductRuntime *product)
{
    SDL_SetRenderDrawColor(video->renderer, 0, 0, 0, 255);
    SDL_RenderClear(video->renderer);
    SDL_UpdateTexture(video->texture, NULL, engine->a.pixels,
                      (int)(GB_WIDTH * 4u));
    SDL_RenderCopy(video->renderer, video->texture, NULL, NULL);
    if (product->exit_confirming) {
        SDL_Rect panel = {.x = (int)(GB_WIDTH * GB_DISPLAY_SCALE) / 2 - 210,
                          .y = (int)(GB_HEIGHT * GB_DISPLAY_SCALE) / 2 - 70,
                          .w = 420,
                          .h = 140};
        SDL_Color title = {238, 238, 220, 255};
        SDL_Color text = {185, 205, 216, 255};
        SDL_Color selected = {86, 220, 150, 255};
        SDL_SetRenderDrawColor(video->renderer, 10, 14, 18, 235);
        SDL_RenderFillRect(video->renderer, &panel);
        SDL_SetRenderDrawColor(video->renderer, 86, 162, 126, 255);
        SDL_RenderDrawRect(video->renderer, &panel);
        integral_sdl_draw_text(video->renderer, panel.x + 36, panel.y + 28,
                               "EXIT GAME?", 3, title);
        integral_sdl_draw_text(video->renderer, panel.x + 86, panel.y + 84,
                               product->exit_confirm_yes ? "> YES" : "  YES", 3,
                               product->exit_confirm_yes ? selected : text);
        integral_sdl_draw_text(video->renderer, panel.x + 244, panel.y + 84,
                               product->exit_confirm_yes ? "  NO" : "> NO", 3,
                               product->exit_confirm_yes ? text : selected);
    }
    SDL_RenderPresent(video->renderer);
}

static void host_video_close(HostVideo *video)
{
    if (video->texture) SDL_DestroyTexture(video->texture);
    if (video->renderer) SDL_DestroyRenderer(video->renderer);
    if (video->window) SDL_DestroyWindow(video->window);
    memset(video, 0, sizeof(*video));
}

static void pixels_to_rgb24(const uint32_t *pixels, uint8_t *rgb)
{
    for (size_t index = 0; index < GB_WIDTH * GB_HEIGHT; index++) {
        uint32_t pixel = pixels[index];
        rgb[index * 3u] = (uint8_t)(pixel >> 16u);
        rgb[index * 3u + 1u] = (uint8_t)(pixel >> 8u);
        rgb[index * 3u + 2u] = (uint8_t)pixel;
    }
}

static void log_media_metrics(const char *role, IntegralN64RuntimeMediaStream *stream, uint64_t current)
{
    IntegralN64RuntimeMediaMetrics metrics;
    if (!integral_n64_runtime_media_stream_take_metrics(stream, current, &metrics)) return;
    double seconds = metrics.window_ms ? metrics.window_ms / 1000.0 : 1.0;
    printf("GB_FIXED_METRICS role=%s video_fps=%.2f encoded_fps=%.2f received_fps=%.2f "
           "capture_fps=%.2f capture_overwrites=%u encode_avg_ms=%.2f "
           "audio_queue_ms=%u audio_queue_max_ms=%u audio_jitter=%u audio_conceals=%u "
           "relay_pending_ms=%u present_p95_ms=%.2f\n",
           role, metrics.presented_frames / seconds, metrics.encoded_frames / seconds,
           metrics.received_frames / seconds, metrics.capture_frames / seconds,
           metrics.capture_overwrites, metrics.encode_avg_us / 1000.0,
           metrics.audio_queue_ms, metrics.audio_queue_max_ms,
           metrics.audio_jitter_packets, metrics.audio_conceals, metrics.relay_pending_ms,
           metrics.present_p95_us / 1000.0);
    fflush(stdout);
}

static bool finish_host_trade(const Options *options,
                              IntegralMediaRelayConnection *connection,
                              IntegralGBRuntimeLinkEngine *engine,
                              uint32_t *sequence,
                              char *error, size_t error_size)
{
    IntegralGBRuntimeLinkSnapshot snapshot;
    IntegralGBRuntimeFixedHostResult result = {0};
    uint8_t terminal[INTEGRAL_MEDIA_GB_TERMINAL_BYTES];
    bool acknowledged = false;
    if (!options->commit_pair ||
        (options->result_handle <= 0 && !options->headless_receipt_file) ||
        integral_gb_runtime_link_engine_snapshot(engine, &snapshot) != 0) return false;
    put_be64(terminal, snapshot.logical_frame);
    gb_runtime_fixed_host_terminal_digest(&snapshot, terminal + 8u);
    if (integral_media_relay_send_control(
            connection, INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL, (*sequence)++, terminal,
            sizeof(terminal), error, error_size) < 0) goto done;
    for (uint64_t deadline = now_us() + UINT64_C(5000000); now_us() < deadline;) {
        uint8_t type = 0u, payload[INTEGRAL_MEDIA_GB_TERMINAL_BYTES];
        uint32_t received_sequence = 0u, size = 0u;
        int got = integral_media_relay_poll_control(
            connection, &type, &received_sequence, payload, sizeof(payload), &size,
            error, error_size);
        (void)received_sequence;
        if (got < 0) goto done;
        if (got > 0 && type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL_ACK &&
            size == sizeof(terminal) && memcmp(payload, terminal, sizeof(terminal)) == 0) {
            acknowledged = true;
            break;
        }
        SDL_Delay(5u);
    }
    if (!acknowledged) goto done;
    result.host = true;
    result.final_frame = snapshot.logical_frame;
    memcpy(result.terminal_digest, terminal + 8u, 32u);
    result.candidates.host_data = snapshot.battery_a;
    result.candidates.host_size = snapshot.battery_a_size;
    result.candidates.remote_data = snapshot.battery_b;
    result.candidates.remote_size = snapshot.battery_b_size;
    snapshot.battery_a = snapshot.battery_b = NULL;
    snapshot.battery_a_size = snapshot.battery_b_size = 0u;
    if (!send_result(options, &result)) {
        acknowledged = false;
    }
done:
    integral_gb_runtime_link_snapshot_free(&snapshot);
    integral_gb_runtime_secure_zero(terminal, sizeof(terminal));
    return acknowledged;
}

static int wait_paired(IntegralMediaRelayConnection *connection)
{
    char error[192] = {0};
    for (;;) {
        int paired = integral_media_relay_poll(connection, error, sizeof(error));
        if (paired > 0) return 0;
        if (paired < 0) {
            fprintf(stderr, "%s\n", error);
            return -1;
        }
        SDL_Delay(10u);
    }
}

static int run_host(const Options *options, IntegralMediaRelayConnection *connection)
{
    uint8_t *save1 = NULL, *save2 = NULL;
    size_t save1_size = 0, save2_size = 0;
    IntegralGBRuntimeFixedHostSnapshotPair snapshots = options->snapshots;
    if (options->snapshot_stdin) {
        save1 = snapshots.host_data;
        save1_size = snapshots.host_size;
        save2 = snapshots.remote_data;
        save2_size = snapshots.remote_size;
    }
    IntegralGBRuntimeFixedHostRuntime runtime;
    IntegralGBRuntimeFixedHostRuntimeConfig config = {
        .host_rom_path = options->rom1,
        .remote_rom_path = options->rom2,
        .host_save = save1,
        .host_save_size = save1_size,
        .remote_save = save2,
        .remote_save_size = save2_size,
        .preserve_both_audio = true,
    };
    if (integral_gb_runtime_fixed_host_runtime_init(&runtime, &config) != 0) {
        fprintf(stderr, "GB fixed host twin engine initialization failed\n");
        if (options->snapshot_stdin) integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);
        return 1;
    }
    if (options->snapshot_stdin) integral_gb_runtime_fixed_host_snapshot_pair_release(&snapshots);

    HostVideo video = {0};
    if (host_video_open(&video) != 0) {
        fprintf(stderr, "GB fixed host video failed: %s\n", SDL_GetError());
        integral_gb_runtime_fixed_host_runtime_free(&runtime); return 1;
    }
    IntegralN64RuntimeMediaStream *media = integral_n64_runtime_media_stream_create(NULL);
    if (!media) {
        host_video_close(&video);
        integral_gb_runtime_fixed_host_runtime_free(&runtime);
        return 1;
    }
    SDL_AudioSpec audio_spec = {.freq = INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE, .format = AUDIO_S16SYS,
                                .channels = 2, .samples = 1024};
    SDL_AudioDeviceID local_audio = SDL_OpenAudioDevice(NULL, 0, &audio_spec, NULL, 0);
    if (local_audio) SDL_PauseAudioDevice(local_audio, 0);
    uint8_t rgb[GB_WIDTH * GB_HEIGHT * 3u];
    int16_t pcm[INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES * 2u];
    uint8_t local_buttons = 0, remote_buttons = 0;
    IntegralGBRuntimeFixedHostProductRuntime product;
    IntegralGBRuntimeFixedHostScheduler scheduler;
    if (!integral_gb_runtime_fixed_host_product_runtime_init(
            &product, INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_HOST)) {
        if (local_audio) SDL_CloseAudioDevice(local_audio);
        integral_n64_runtime_media_stream_destroy(media);
        host_video_close(&video);
        integral_gb_runtime_fixed_host_runtime_free(&runtime);
        return 1;
    }
    if (!integral_gb_runtime_fixed_host_scheduler_init(
            &scheduler, options->test_macro_host, options->test_macro_remote,
            options->test_macro_press_frames, options->test_macro_step_frames)) {
        integral_gb_runtime_fixed_host_product_runtime_stop(&product);
        if (local_audio) SDL_CloseAudioDevice(local_audio);
        integral_n64_runtime_media_stream_destroy(media);
        host_video_close(&video);
        integral_gb_runtime_fixed_host_runtime_free(&runtime);
        return 1;
    }
    if (getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_TEST_AUTOMATION"))
        integral_gb_runtime_fixed_host_scheduler_start_test_automation(&scheduler);
    uint32_t output_sequence = 1;
    bool pending_ack = false;
    uint32_t pending_input_sequence = 0;
    uint64_t pending_input_sent_us = 0, pending_received_us = 0;
    uint64_t next_frame = now_us();
    char error[192] = {0};
    printf("GB_RUNTIME_FIXED_HOST paired session=%s role=host sav_writeback=disabled rom_transfer=disabled\n",
           options->session_id);
    bool normal_exit = false;
    uint64_t headless_finish_frame = 0u;
    if (getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_HEADLESS_FINISH_FRAME"))
        headless_finish_frame = strtoull(
            getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_HEADLESS_FINISH_FRAME"), NULL, 10);
    while (true) {
        if (poll_input(&product, NULL, options) ==
            INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONFIRMED) {
            fprintf(stderr, "GB fixed host local UI requested exit\n");
            break;
        }
        for (unsigned count = 0; count < 16u; count++) {
            uint8_t type = 0, payload[INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES];
            uint32_t sequence = 0, size = 0;
            int got = integral_media_relay_poll_control(connection, &type, &sequence,
                                                         payload, sizeof(payload), &size,
                                                         error, sizeof(error));
            if (got < 0) { fprintf(stderr, "%s\n", error); goto done; }
            if (!got) break;
            if (type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT) {
                remote_buttons = (uint8_t)get_be64(payload);
                pending_ack = true;
                pending_input_sequence = sequence;
                pending_input_sent_us = get_be64(payload + 8u);
                pending_received_us = now_us();
            }
            else if (type == INTEGRAL_MEDIA_MESSAGE_GB_PING) {
                (void)integral_media_relay_send_control(connection,
                                                         INTEGRAL_MEDIA_MESSAGE_GB_PONG,
                                                         output_sequence++, payload, size,
                                                         error, sizeof(error));
            }
            else if (type == INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE &&
                     size == INTEGRAL_MEDIA_GB_SESSION_STATE_BYTES) {
                uint32_t session_state = get_be32(payload);
                printf("GB_FIXED_SESSION_STATE role=host state=%u remaining_ms=%u\n",
                       session_state, get_be32(payload + 4u));
                integral_gb_runtime_fixed_host_scheduler_set_paused(
                    &scheduler, session_state == 1u);
                remote_buttons = 0u;
                product.buttons = 0u;
                if (session_state == 2u) {
                    if (integral_n64_runtime_media_stream_prepare_video_resume(
                            media, connection, error, sizeof(error)) < 0) {
                        fprintf(stderr, "%s\n", error);
                        goto done;
                    }
                }
            }
        }
        uint64_t current = now_us();
        if (integral_n64_runtime_media_stream_pump_host_video_output(
                media, connection, current, error, sizeof(error)) < 0) {
            fprintf(stderr, "GB fixed host async video send failed: %s\n", error);
            goto done;
        }
        if (current < next_frame) {
            SDL_Delay((Uint32)((next_frame - current) / 1000u));
            continue;
        }
        uint64_t frame_us = UINT64_C(1000000) / 60u;
        next_frame += frame_us;
        if (current > next_frame + UINT64_C(250000)) next_frame = current;
        if (!integral_gb_runtime_fixed_host_scheduler_frame(
                &scheduler, product.buttons, remote_buttons,
                &local_buttons, &remote_buttons)) break;
        if (scheduler.paused) {
            SDL_Delay(2u);
            continue;
        }
        if (integral_gb_runtime_fixed_host_runtime_run_frame(
                &runtime, local_buttons, remote_buttons) != 0) {
            fprintf(stderr, "GB fixed host twin frame failed\n"); break;
        }
        if (headless_finish_frame &&
            runtime.engine.logical_frame >= headless_finish_frame) {
            normal_exit = true;
            break;
        }
        if (pending_ack) {
            uint8_t payload[INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES] = {0};
            put_be32(payload, pending_input_sequence);
            put_be64(payload + 8u, pending_input_sent_us);
            put_be64(payload + 16u, runtime.engine.logical_frame);
            int sent = integral_media_relay_send_control(connection,
                                                          INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK,
                                                          output_sequence++, payload, sizeof(payload),
                                                          error, sizeof(error));
            if (sent > 0) {
                printf("GB_FIXED_INPUT_APPLIED sequence=%u frame=%llu receive_to_apply_us=%llu\n",
                       pending_input_sequence,
                       (unsigned long long)runtime.engine.logical_frame,
                       (unsigned long long)(now_us() - pending_received_us));
                pending_ack = false;
            }
        }
        host_video_render(&video, &runtime.engine, &product);
        if ((runtime.engine.logical_frame & 1u) == 0u) {
            pixels_to_rgb24(runtime.engine.b.pixels, rgb);
            if (integral_n64_runtime_media_stream_submit_host_rgb24(media, rgb,
                                                             GB_WIDTH, GB_HEIGHT, false,
                                                             current, error, sizeof(error)) < 0) {
                fprintf(stderr, "%s\n", error); break;
            }
        }
        unsigned remote_audio = integral_gb_runtime_slot_drain_audio(
            &runtime.engine.b, pcm, INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES);
        if (remote_audio && integral_n64_runtime_media_stream_send_host_pcm(media, connection, pcm,
                                                                    (uint16_t)remote_audio,
                                                                    INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE,
                                                                    current, error, sizeof(error)) < 0) {
            fprintf(stderr, "%s\n", error); break;
        }
        unsigned local_frames = integral_gb_runtime_slot_drain_audio(
            &runtime.engine.a, pcm, INTEGRAL_GB_RUNTIME_AUDIO_MAX_FRAMES);
        if (local_audio && local_frames) SDL_QueueAudio(local_audio, pcm, local_frames * 4u);
        if (local_audio && SDL_GetQueuedAudioSize(local_audio) > INTEGRAL_GB_RUNTIME_AUDIO_SAMPLE_RATE * 4u / 5u) {
            SDL_ClearQueuedAudio(local_audio);
        }
        log_media_metrics("host", media, current);
    }
done:
    {
        const char *screenshot_a = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SCREENSHOT_A");
        const char *screenshot_b = getenv("INTEGRAL_EMULATOR_GB_RUNTIME_FIXED_HOST_SCREENSHOT_B");
        if (screenshot_a || screenshot_b) {
            int saved_a = write_slot_bmp(screenshot_a, &runtime.engine.a);
            int saved_b = write_slot_bmp(screenshot_b, &runtime.engine.b);
            printf("GB_FIXED_FINAL_SCREEN frame=%llu a=%s b=%s saved_a=%s saved_b=%s\n",
                   (unsigned long long)runtime.engine.logical_frame,
                   screenshot_a ? screenshot_a : "", screenshot_b ? screenshot_b : "",
                   saved_a == 0 ? "yes" : "no", saved_b == 0 ? "yes" : "no");
            if (saved_a != 0 || saved_b != 0) normal_exit = false;
        }
    }
    printf("GB_FIXED_PROTOCOL_SUMMARY frame=%llu serial_events=%llu ir_events=%llu\n",
           (unsigned long long)runtime.engine.logical_frame,
           (unsigned long long)runtime.engine.serial_events,
           (unsigned long long)runtime.engine.ir_events);
    if (normal_exit && options->commit_pair &&
        !finish_host_trade(options, connection, &runtime.engine,
                           &output_sequence, error, sizeof(error))) {
        fprintf(stderr, "GB fixed host Trade terminal agreement failed: %s\n", error);
        normal_exit = false;
    }
    integral_gb_runtime_fixed_host_scheduler_stop(&scheduler);
    integral_gb_runtime_fixed_host_product_runtime_stop(&product);
    if (local_audio) SDL_CloseAudioDevice(local_audio);
    integral_n64_runtime_media_stream_destroy(media);
    host_video_close(&video);
    integral_gb_runtime_fixed_host_runtime_free(&runtime);
    return normal_exit || !options->commit_pair ? 0 : 1;
}

static int run_remote(const Options *options, IntegralMediaRelayConnection *connection)
{
    IntegralN64RuntimeMediaStream *media = integral_n64_runtime_media_stream_create(NULL);
    if (!media) return 1;
    integral_n64_runtime_media_stream_set_window_title(media,
                                                "INTEGRAL EMULATOR - GB FIXED HOST REMOTE SLOT2");
    integral_n64_runtime_media_stream_set_window_scale(media, GB_DISPLAY_SCALE);
    uint8_t buttons = 0, last_sent_buttons = 0xffu;
    IntegralGBRuntimeFixedHostProductRuntime product;
    if (!integral_gb_runtime_fixed_host_product_runtime_init(
            &product, INTEGRAL_GB_RUNTIME_FIXED_HOST_PRODUCT_REMOTE)) {
        integral_n64_runtime_media_stream_destroy(media);
        return 1;
    }
    uint32_t sequence = 1;
    uint64_t last_input_sent = 0, last_ping_sent = 0;
    uint64_t rtt_total = 0, input_total = 0;
    uint32_t rtt_samples = 0, input_samples = 0;
    char error[192] = {0};
    printf("GB_FIXED_REMOTE paired session=%s core=disabled rom_transfer=disabled\n", options->session_id);
    bool terminal_received = false;
    while (true) {
        if (poll_input(&product, media, options) ==
            INTEGRAL_GB_RUNTIME_FIXED_HOST_EXIT_CONFIRMED) break;
        buttons = product.buttons;
        if (integral_gb_runtime_fixed_host_product_runtime_take_neutral(&product))
            last_sent_buttons = 0xffu;
        uint64_t current = now_us();
        if (buttons != last_sent_buttons || current - last_input_sent >= INPUT_HEARTBEAT_US) {
            uint8_t payload[INTEGRAL_MEDIA_GB_INPUT_BYTES];
            put_be64(payload, buttons);
            put_be64(payload + 8u, current);
            int sent = integral_media_relay_send_control(connection,
                                                          INTEGRAL_MEDIA_MESSAGE_GB_INPUT,
                                                          sequence++, payload, sizeof(payload),
                                                          error, sizeof(error));
            if (sent < 0) { fprintf(stderr, "%s\n", error); break; }
            if (sent > 0) {
                last_sent_buttons = buttons;
                last_input_sent = current;
            }
        }
        if (!last_ping_sent || current - last_ping_sent >= PING_INTERVAL_US) {
            uint8_t payload[INTEGRAL_MEDIA_GB_PING_BYTES];
            put_be64(payload, current);
            int sent = integral_media_relay_send_control(connection,
                                                          INTEGRAL_MEDIA_MESSAGE_GB_PING,
                                                          sequence++, payload, sizeof(payload),
                                                          error, sizeof(error));
            if (sent < 0) { fprintf(stderr, "%s\n", error); break; }
            if (sent > 0) last_ping_sent = current;
        }
        if (integral_n64_runtime_media_stream_pump_remote(media, connection, current,
                                                   error, sizeof(error)) < 0) {
            fprintf(stderr, "%s\n", error); break;
        }
        integral_n64_runtime_media_stream_render(media, NULL, NULL);
        uint8_t type = 0, payload[INTEGRAL_MEDIA_GB_TERMINAL_BYTES];
        uint32_t control_sequence = 0, size = 0;
        if (integral_n64_runtime_media_stream_take_remote_control(media, &type, &control_sequence,
                                                           payload, sizeof(payload), &size)) {
            (void)control_sequence;
            uint64_t measured = 0;
            if (type == INTEGRAL_MEDIA_MESSAGE_GB_PONG && size == INTEGRAL_MEDIA_GB_PING_BYTES) {
                uint64_t sent_us = get_be64(payload);
                if (current >= sent_us) measured = current - sent_us;
                rtt_total += measured; rtt_samples++;
                printf("GB_FIXED_RTT rtt_ms=%.2f\n", measured / 1000.0);
            }
            else if (type == INTEGRAL_MEDIA_MESSAGE_GB_INPUT_ACK &&
                     size == INTEGRAL_MEDIA_GB_INPUT_ACK_BYTES) {
                uint32_t input_sequence = get_be32(payload);
                uint64_t sent_us = get_be64(payload + 8u);
                uint64_t applied_frame = get_be64(payload + 16u);
                if (current >= sent_us) measured = current - sent_us;
                input_total += measured; input_samples++;
                printf("GB_FIXED_INPUT_DELAY sequence=%u apply_delay_ms=%.2f host_frame=%llu\n",
                       input_sequence, measured / 1000.0, (unsigned long long)applied_frame);
            }
            else if (type == INTEGRAL_MEDIA_MESSAGE_GB_SESSION_STATE &&
                     size == INTEGRAL_MEDIA_GB_SESSION_STATE_BYTES) {
                printf("GB_FIXED_SESSION_STATE state=%u remaining_ms=%u\n",
                       get_be32(payload), get_be32(payload + 4u));
            }
            else if (type == INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL &&
                     size == INTEGRAL_MEDIA_GB_TERMINAL_BYTES &&
                     options->commit_pair &&
                     (options->result_handle > 0 || options->headless_receipt_file)) {
                IntegralGBRuntimeFixedHostResult result = {0};
                if (integral_media_relay_send_control(
                        connection, INTEGRAL_MEDIA_MESSAGE_GB_TERMINAL_ACK,
                        sequence++, payload, size, error, sizeof(error)) < 0) break;
                result.host = false;
                result.final_frame = get_be64(payload);
                memcpy(result.terminal_digest, payload + 8u, 32u);
                if (!send_result(options, &result)) {
                    break;
                }
                terminal_received = true;
                printf("GB_FIXED_TRADE_TERMINAL_RECEIVED frame=%llu\n",
                       (unsigned long long)get_be64(payload));
                break;
            }
        }
        if (terminal_received) break;
        IntegralN64RuntimeMediaMetrics metrics;
        if (integral_n64_runtime_media_stream_take_metrics(media, current, &metrics)) {
            double seconds = metrics.window_ms ? metrics.window_ms / 1000.0 : 1.0;
            printf("GB_FIXED_METRICS role=remote video_fps=%.2f received_fps=%.2f "
                   "decoded_fps=%.2f decode_avg_ms=%.2f decode_max_ms=%.2f "
                   "decode_queue_peak=%u display_overwrites=%u "
                   "rtt_avg_ms=%.2f input_apply_avg_ms=%.2f audio_queue_ms=%u "
                   "audio_queue_max_ms=%u audio_jitter=%u audio_conceals=%u present_p95_ms=%.2f\n",
                   metrics.presented_frames / seconds, metrics.received_frames / seconds,
                   metrics.decoded_frames / seconds,
                   metrics.decode_avg_us / 1000.0,
                   metrics.decode_max_us / 1000.0,
                   metrics.decode_queue_peak,
                   metrics.display_overwrites,
                   rtt_samples ? rtt_total / (double)rtt_samples / 1000.0 : 0.0,
                   input_samples ? input_total / (double)input_samples / 1000.0 : 0.0,
                   metrics.audio_queue_ms, metrics.audio_queue_max_ms,
                   metrics.audio_jitter_packets, metrics.audio_conceals,
                   metrics.present_p95_us / 1000.0);
            fflush(stdout);
            rtt_total = input_total = 0; rtt_samples = input_samples = 0;
        }
        SDL_Delay(integral_n64_runtime_media_stream_is_video_vsync_paced(media) ? 1u : 8u);
    }
    integral_gb_runtime_fixed_host_product_runtime_stop(&product);
    integral_n64_runtime_media_stream_destroy(media);
    return terminal_received || !options->commit_pair ? 0 : 1;
}

int main(int argc, char **argv)
{
    /* Product diagnostics must survive abrupt process termination so fault
     * evidence is not lost when stdout is redirected by the parent client. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
#ifndef _WIN32
    /* A WAN relay may close while OpenSSL is writing. Surface that as a
     * normal transport error instead of letting SIGPIPE terminate the host. */
    signal(SIGPIPE, SIG_IGN);
#endif
    Options options;
    if (parse_options(argc, argv, &options) != 0) {
        usage(argv[0]); return 2;
    }
#ifdef _WIN32
    if (options.snapshot_stdin && _setmode(_fileno(stdin), _O_BINARY) == -1) {
        fprintf(stderr, "GB fixed host snapshot IPC could not set binary stdin\n");
        return 1;
    }
#endif
    if (options.snapshot_stdin &&
        !integral_gb_runtime_fixed_host_snapshot_ipc_receive(
            file_read_callback, stdin, &options.snapshots)) {
        fprintf(stderr, "GB fixed host snapshot IPC rejected\n");
        return 1;
    }
    const char *token = getenv(GB_FIXED_TOKEN_ENV);
    if (!token || strlen(token) < 24u) {
        fprintf(stderr, "%s must contain at least 24 characters\n", GB_FIXED_TOKEN_ENV);
        integral_gb_runtime_fixed_host_snapshot_pair_release(&options.snapshots);
        return 2;
    }
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS |
                 SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
        integral_gb_runtime_fixed_host_snapshot_pair_release(&options.snapshots);
        return 1;
    }
    (void)integral_gb_runtime_key_config_open_game_controllers();
    IntegralMediaRelayConnection *connection = NULL;
    char error[192] = {0};
    if (integral_media_relay_connect(options.relay_host, options.relay_port,
                                      options.relay_transport,
                                      options.session_id, options.role, GB_FIXED_SCOPE,
                                      token, options.ca_file, &connection,
                                      error, sizeof(error)) != 0 || wait_paired(connection) != 0) {
        fprintf(stderr, "%s\n", error);
        integral_gb_runtime_fixed_host_snapshot_pair_release(&options.snapshots);
        integral_media_relay_close(connection); SDL_Quit(); return 1;
    }
    int result = strcmp(options.role, "host") == 0
                     ? run_host(&options, connection)
                     : run_remote(&options, connection);
    integral_media_relay_close(connection);
    SDL_Quit();
    return result;
}

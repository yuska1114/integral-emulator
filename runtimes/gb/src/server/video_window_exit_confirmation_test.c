/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "input_router.h"
#include "video_window.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
static uint32_t *readback_pixels;
static int readback_pitch;
static int readback_result = -1;
#define CHECK(value) do { checks++; if (!(value)) { \
    fprintf(stderr, "CHECK failed line %d: %s\n", __LINE__, #value); exit(1); \
} } while (0)

void integral_gb_runtime_video_window_test_readback(SDL_Renderer *renderer)
{
    if (!readback_pixels) return;
    readback_result = SDL_RenderReadPixels(renderer, NULL,
                                           SDL_PIXELFORMAT_ARGB8888,
                                           readback_pixels, readback_pitch);
}

static SDL_Window *find_test_window(void)
{
    for (Uint32 id = 1u; id < 64u; id++) {
        SDL_Window *window = SDL_GetWindowFromID(id);
        if (window) return window;
    }
    return NULL;
}

static int run_real_rom_capture(const char *rom_path,
                                const char *output_path,
                                unsigned frame_count,
                                unsigned window_width,
                                unsigned window_height,
                                unsigned slot_count)
{
    IntegralGBRuntimeRomProfile profile;
    IntegralGBRuntimeRomModelReason reason;
    GB_model_t model;
    IntegralGBRuntimeSlot slot;
    if (!rom_path || !output_path || frame_count == 0u ||
        integral_gb_runtime_slot_model_for_rom(
            rom_path, &model, &profile, &reason) != 0) {
        return 2;
    }
    IntegralGBRuntimeSlotConfig config = {
        .name = "real-rom-layout-test",
        .rom_path = rom_path,
        .model = model,
        .skip_boot_rom = true,
        .battery_mode = INTEGRAL_GB_RUNTIME_BATTERY_MEMORY_ONLY,
    };
    if (integral_gb_runtime_slot_init(&slot, &config) != 0) return 1;

    IntegralGBRuntimeVideoWindow *window = NULL;
    if (integral_gb_runtime_video_window_open_titled_unthrottled_sized(
            &window, 1u, slot_count, "GB real-ROM layout test",
            window_width, window_height) != 0) {
        integral_gb_runtime_slot_free_without_save(&slot);
        return 1;
    }
    if (integral_gb_runtime_slot_run_frames(&slot, frame_count) != 0) {
        integral_gb_runtime_video_window_close(window);
        integral_gb_runtime_slot_free_without_save(&slot);
        return 1;
    }
    bool suppressed = integral_gb_runtime_slot_presentation_suppressed(&slot);

    SDL_Window *sdl_window = find_test_window();
    SDL_Renderer *renderer = sdl_window ? SDL_GetRenderer(sdl_window) : NULL;
    int output_width = 0;
    int output_height = 0;
    if (!renderer ||
        SDL_GetRendererOutputSize(renderer, &output_width, &output_height) != 0 ||
        output_width < (int)window_width || output_height < (int)window_height) {
        integral_gb_runtime_video_window_close(window);
        integral_gb_runtime_slot_free_without_save(&slot);
        return 1;
    }
    uint32_t *capture = calloc((size_t)output_width * (size_t)output_height,
                               sizeof(*capture));
    if (!capture) {
        integral_gb_runtime_video_window_close(window);
        integral_gb_runtime_slot_free_without_save(&slot);
        return 1;
    }
    readback_pixels = capture;
    readback_pitch = output_width * (int)sizeof(*capture);
    readback_result = -1;
    int result = integral_gb_runtime_video_window_render(window, &slot,
                                                        slot_count == 2u ? &slot : NULL);
    readback_pixels = NULL;
    readback_pitch = 0;
    if (result == 0 && readback_result == 0) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
            capture, output_width, output_height, 32,
            output_width * (int)sizeof(*capture), SDL_PIXELFORMAT_ARGB8888);
        if (!surface || SDL_SaveBMP(surface, output_path) != 0) result = -1;
        if (surface) SDL_FreeSurface(surface);
    }
    else {
        result = -1;
    }
    printf("REAL_ROM_LAYOUT model=%s frames=%u window=%ux%u output=%dx%d checksum=%08X suppressed=%s file=%s\n",
           integral_gb_runtime_slot_model_name(model), frame_count,
           window_width, window_height, output_width, output_height,
           integral_gb_runtime_slot_pixel_checksum(&slot),
           suppressed ? "yes" : "no", output_path);
    free(capture);
    integral_gb_runtime_video_window_close(window);
    integral_gb_runtime_slot_free_without_save(&slot);
    return result == 0 ? 0 : 1;
}

static void push_key(Uint32 type, SDL_Keycode key)
{
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.key.type = type;
    event.key.keysym.sym = key;
    CHECK(SDL_PushEvent(&event) == 1);
}

static IntegralGBRuntimeVideoWindowPollResult poll_window(
    IntegralGBRuntimeVideoWindow *window, IntegralGBRuntimeInputRouter *input)
{
    return integral_gb_runtime_video_window_poll(window, input, NULL, NULL);
}

int main(int argc, char **argv)
{
    if (argc == 4 || argc == 6 || argc == 7) {
        char *end = NULL;
        unsigned long frames = strtoul(argv[3], &end, 10);
        if (!end || *end || frames == 0ul || frames > 10000ul) return 2;
        unsigned long width = 640ul;
        unsigned long height = 480ul;
        if (argc >= 6) {
            width = strtoul(argv[4], &end, 10);
            if (!end || *end || width > 16384ul) return 2;
            height = strtoul(argv[5], &end, 10);
            if (!end || *end || height > 16384ul) return 2;
        }
        unsigned slots = 1u;
        if (argc == 7) {
            if (strcmp(argv[6], "2") != 0) return 2;
            slots = 2u;
        }
        return run_real_rom_capture(argv[1], argv[2], (unsigned)frames,
                                    (unsigned)width, (unsigned)height, slots);
    }
    if (argc != 1) {
        fprintf(stderr, "usage: %s [ROM OUTPUT_BMP FRAMES [WIDTH HEIGHT [2]]]\n", argv[0]);
        return 2;
    }
    if (!getenv("INTEGRAL_EMULATOR_REAL_VIDEO_DRIVER_TEST")) {
        CHECK(SDL_setenv("SDL_VIDEODRIVER", "dummy", 1) == 0);
        CHECK(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    }
    IntegralGBRuntimeVideoWindow *window = NULL;
    CHECK(integral_gb_runtime_video_window_open_titled_unthrottled_sized(
              &window, 1, 1, "invalid target test", 159u, 480u) != 0);
    CHECK(integral_gb_runtime_video_window_open_titled_unthrottled_sized(
              &window, 1, 1, "exit confirmation test", 640u, 480u) == 0);
    SDL_Window *sdl_window = find_test_window();
    int actual_width = 0;
    int actual_height = 0;
    CHECK(sdl_window != NULL);
    SDL_GetWindowSize(sdl_window, &actual_width, &actual_height);
    CHECK(actual_width == 640);
    CHECK(actual_height == 480);
    CHECK((SDL_GetWindowFlags(sdl_window) & SDL_WINDOW_RESIZABLE) != 0u);
    int minimum_width = 0;
    int minimum_height = 0;
    SDL_GetWindowMinimumSize(sdl_window, &minimum_width, &minimum_height);
    CHECK(minimum_width == INTEGRAL_GB_RUNTIME_GB_WIDTH);
    CHECK(minimum_height == INTEGRAL_GB_RUNTIME_GB_HEIGHT);
    SDL_Renderer *renderer = SDL_GetRenderer(sdl_window);
    int logical_width = 0;
    int logical_height = 0;
    CHECK(renderer != NULL);
    SDL_RenderGetLogicalSize(renderer, &logical_width, &logical_height);
    CHECK(logical_width == 640);
    CHECK(logical_height == 480);
    int output_width = 0;
    int output_height = 0;
    CHECK(SDL_GetRendererOutputSize(renderer, &output_width, &output_height) == 0);
    CHECK(output_width >= 640 && output_width % 640 == 0);
    CHECK(output_height >= 480 && output_height % 480 == 0);
    unsigned output_scale_x = (unsigned)output_width / 640u;
    unsigned output_scale_y = (unsigned)output_height / 480u;
    uint32_t *capture = calloc((size_t)output_width * (size_t)output_height,
                               sizeof(*capture));
    CHECK(capture != NULL);
    readback_pixels = capture;
    readback_pitch = output_width * (int)sizeof(*capture);
    IntegralGBRuntimeSlot slot = {0};
    slot.initialized = true;
    for (unsigned i = 0;
         i < INTEGRAL_GB_RUNTIME_GB_WIDTH * INTEGRAL_GB_RUNTIME_GB_HEIGHT;
         i++) {
        slot.pixels[i] = 0xFF12AB34u;
    }
    CHECK(integral_gb_runtime_video_window_render(window, &slot, NULL) == 0);
    CHECK(readback_result == 0);
#define CAPTURE_PIXEL(x, y) \
    capture[((size_t)(y) * output_scale_y) * (size_t)output_width + \
            ((size_t)(x) * output_scale_x)]
    CHECK((CAPTURE_PIXEL(0u, 0u) & 0x00FFFFFFu) == 0u);
    CHECK((CAPTURE_PIXEL(79u, 24u) & 0x00FFFFFFu) == 0u);
    CHECK((CAPTURE_PIXEL(80u, 24u) & 0x00FFFFFFu) == 0x0012AB34u);
    CHECK((CAPTURE_PIXEL(559u, 455u) & 0x00FFFFFFu) == 0x0012AB34u);
    CHECK((CAPTURE_PIXEL(560u, 455u) & 0x00FFFFFFu) == 0u);
    CHECK((CAPTURE_PIXEL(80u, 456u) & 0x00FFFFFFu) == 0u);
#undef CAPTURE_PIXEL
    readback_pixels = NULL;
    readback_pitch = 0;
    free(capture);

    SDL_SetWindowSize(sdl_window, 721, 530);
    SDL_GetWindowSize(sdl_window, &actual_width, &actual_height);
    CHECK(actual_width == 721);
    CHECK(actual_height == 530);
    CHECK(SDL_GetRendererOutputSize(renderer, &output_width, &output_height) == 0);
    CHECK(output_width >= actual_width && output_width % actual_width == 0);
    CHECK(output_height >= actual_height && output_height % actual_height == 0);
    output_scale_x = (unsigned)output_width / (unsigned)actual_width;
    output_scale_y = (unsigned)output_height / (unsigned)actual_height;
    capture = calloc((size_t)output_width * (size_t)output_height, sizeof(*capture));
    CHECK(capture != NULL);
    readback_pixels = capture;
    readback_pitch = output_width * (int)sizeof(*capture);
    readback_result = -1;
    CHECK(integral_gb_runtime_video_window_render(window, &slot, NULL) == 0);
    CHECK(readback_result == 0);
    SDL_RenderGetLogicalSize(renderer, &logical_width, &logical_height);
    CHECK(logical_width == actual_width);
    CHECK(logical_height == actual_height);
#define RESIZED_CAPTURE_PIXEL(x, y) \
    capture[((size_t)(y) * output_scale_y) * (size_t)output_width + \
            ((size_t)(x) * output_scale_x)]
    CHECK((RESIZED_CAPTURE_PIXEL(119u, 49u) & 0x00FFFFFFu) == 0u);
    CHECK((RESIZED_CAPTURE_PIXEL(120u, 49u) & 0x00FFFFFFu) == 0x0012AB34u);
    CHECK((RESIZED_CAPTURE_PIXEL(599u, 480u) & 0x00FFFFFFu) == 0x0012AB34u);
    CHECK((RESIZED_CAPTURE_PIXEL(600u, 480u) & 0x00FFFFFFu) == 0u);
    CHECK((RESIZED_CAPTURE_PIXEL(120u, 481u) & 0x00FFFFFFu) == 0u);
#undef RESIZED_CAPTURE_PIXEL
    readback_pixels = NULL;
    readback_pitch = 0;
    free(capture);
    IntegralGBRuntimeInputRouter input;
    integral_gb_runtime_input_router_init(&input, NULL, NULL);
    integral_gb_runtime_input_router_disable_speed_controls(&input);

    IntegralGBRuntimeInputRouter mobile_input;
    IntegralGBRuntimeKeyConfig mobile_keys;
    CHECK(integral_gb_runtime_key_config_parse(
              &mobile_keys, "D,A,W,S,G,H,R,T") == 0);
    integral_gb_runtime_input_router_init(&mobile_input, NULL, NULL);
    integral_gb_runtime_input_router_set_keymaps(
        &mobile_input, &mobile_keys, NULL, SDLK_v, SDLK_c, SDLK_q, SDLK_n,
        SDLK_m);
    integral_gb_runtime_input_router_disable_speed_controls(&mobile_input);
    CHECK(mobile_input.slot1_keys.a == SDLK_g);
    CHECK(mobile_input.screenshot_key == SDLK_c);
    CHECK(mobile_input.escape_key == SDLK_q);
    CHECK(mobile_input.reset_key == SDLK_m);
    CHECK(mobile_input.fast_key == SDLK_UNKNOWN);
    CHECK(mobile_input.turbo_hold_key == SDLK_UNKNOWN);
    SDL_Event mobile_event;
    memset(&mobile_event, 0, sizeof(mobile_event));
    mobile_event.type = SDL_KEYDOWN;
    mobile_event.key.type = SDL_KEYDOWN;
    mobile_event.key.keysym.sym = SDLK_c;
    CHECK(integral_gb_runtime_input_router_handle_event(
              &mobile_input, &mobile_event));
    CHECK(integral_gb_runtime_input_router_take_screenshot_request(
              &mobile_input));
    mobile_event.key.keysym.sym = SDLK_v;
    CHECK(integral_gb_runtime_input_router_handle_event(
              &mobile_input, &mobile_event));
    CHECK(mobile_input.speed_multiplier == 1u);
    CHECK(!integral_gb_runtime_input_router_take_speed_multiplier_changed(
              &mobile_input));

    push_key(SDL_KEYDOWN, integral_gb_runtime_key_config_fast_default());
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(integral_gb_runtime_input_router_speed_multiplier(&input) == 1u);
    CHECK(!integral_gb_runtime_input_router_take_speed_multiplier_changed(&input));
    push_key(SDL_KEYDOWN, integral_gb_runtime_key_config_turbo_hold_default());
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(!input.turbo_hold_active);
    CHECK(!input.turbo_capture_active);

    push_key(SDL_KEYDOWN, SDLK_z);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(input.slot1_normal_buttons != 0u);

    push_key(SDL_KEYDOWN, SDLK_ESCAPE);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    CHECK(input.slot1_normal_buttons == 0u);
    push_key(SDL_KEYDOWN, SDLK_RETURN);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);

    SDL_Event close_event;
    memset(&close_event, 0, sizeof(close_event));
    close_event.type = SDL_WINDOWEVENT;
    close_event.window.event = SDL_WINDOWEVENT_CLOSE;
    CHECK(SDL_PushEvent(&close_event) == 1);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    push_key(SDL_KEYDOWN, SDLK_ESCAPE);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);

    SDL_Event quit_event;
    memset(&quit_event, 0, sizeof(quit_event));
    quit_event.type = SDL_QUIT;
    CHECK(SDL_PushEvent(&quit_event) == 1);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE);
    push_key(SDL_KEYDOWN, SDLK_RIGHT);
    push_key(SDL_KEYDOWN, SDLK_RETURN);
    CHECK(poll_window(window, &input) == INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_RETURN_MENU);

    integral_gb_runtime_video_window_close(window);
    CHECK(integral_gb_runtime_video_window_open_titled_unthrottled_sized(
              &window, 1, 2, "SERVER2 resize test", 360u, 360u) == 0);
    sdl_window = find_test_window();
    CHECK(sdl_window != NULL);
    renderer = SDL_GetRenderer(sdl_window);
    const int pair_sizes[][2] = {{360, 360}, {944, 648}, {2240, 1100}, {360, 360}};
    for (unsigned n = 0; n < sizeof(pair_sizes) / sizeof(pair_sizes[0]); n++) {
        SDL_SetWindowSize(sdl_window, pair_sizes[n][0], pair_sizes[n][1]);
        SDL_PumpEvents();
        CHECK(SDL_GetRendererOutputSize(renderer, &output_width, &output_height) == 0);
        capture = calloc((size_t)output_width * output_height, sizeof(*capture));
        CHECK(capture != NULL);
        readback_pixels = capture;
        readback_pitch = output_width * (int)sizeof(*capture);
        readback_result = -1;
        CHECK(integral_gb_runtime_video_window_render(window, &slot, &slot) == 0);
        CHECK(readback_result == 0);
        SDL_RenderGetLogicalSize(renderer, &logical_width, &logical_height);
        CHECK(logical_width == output_width && logical_height == output_height);
        int scale = output_width / 320;
        if (scale > output_height / 144) scale = output_height / 144;
        CHECK(scale >= 1);
        int left = (output_width - 320 * scale) / 2;
        int top = (output_height - 144 * scale) / 2;
        CHECK((capture[(size_t)top * output_width + left] & 0xFFFFFFu) == 0x12AB34u);
        CHECK((capture[(size_t)(top + 144 * scale - 1) * output_width + left + 320 * scale - 1] & 0xFFFFFFu) == 0x12AB34u);
        if (left > 0) CHECK((capture[(size_t)top * output_width + left - 1] & 0xFFFFFFu) == 0u);
        if (top > 0) CHECK((capture[(size_t)(top - 1) * output_width + left] & 0xFFFFFFu) == 0u);
        free(capture);
        readback_pixels = NULL;
        readback_pitch = 0;
    }
    integral_gb_runtime_video_window_close(window);
    printf("PASS video window exit confirmation checks=%u\n", checks);
    return 0;
}

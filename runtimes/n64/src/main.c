/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "../../gb/src/common/utf8_file.h"
#include <SDL_opengl.h>

#include "m64p_common.h"
#include "m64p_config.h"
#include "m64p_frontend.h"
#include "m64p_plugin.h"
#include "m64p_types.h"

#include "transfer_pak/media_session.h"
#include "transfer_pak/storage.h"
#include "transfer_pak/memory_session.h"
#include "gui/hotkeys.h"
#include "gui/screenshot_notice.h"
#include "gui/keymap.h"
#include "gui/menu.h"
#include "platform/dynlib.h"
#include "platform/thread.h"
#include "remote_input.h"
#include "remote_media_ipc.h"

#define INTEGRAL_N64_RUNTIME_FRONTEND_API_VERSION 0x020001
#define INTEGRAL_N64_RUNTIME_EXPECTED_API_MAJOR 0x00020000
#define INTEGRAL_N64_RUNTIME_DEFAULT_FRAME 120u
#define INTEGRAL_N64_RUNTIME_DEFAULT_WIDTH 640
#define INTEGRAL_N64_RUNTIME_DEFAULT_HEIGHT 480
#define INTEGRAL_N64_RUNTIME_REMOTE_MEDIA_CAPTURE_INTERVAL_US 33333u

typedef struct Options {
    const char *rom_path;
    const char *core_path;
    const char *config_dir;
    const char *data_dir;
    const char *screenshot_dir;
    const char *save_dir;
    const char *save_name;
    const char *transfer_sav_session;
    int transfer_sav_fd;
    const char *video_path;
    const char *audio_path;
    const char *input_path;
    const char *rsp_path;
    const char *transfer_storage;
    const char *remote_input_file;
    const char *remote_media_file;
    const char *stop_request_file;
    bool interactive;
    bool pure_interpreter;
    bool verbose_log;
    int controller_modes[4];
    const char *controller_maps[4];
    const char *hotkeys;
    unsigned int transfer_mask;
    unsigned int screenshot_frame;
    int width;
    int height;
} Options;

typedef struct CoreApi {
    IntegralN64RuntimeDynlib library;
    ptr_PluginGetVersion get_version;
    ptr_CoreGetAPIVersions get_api_versions;
    ptr_CoreErrorMessage error_message;
    ptr_CoreStartup startup;
    ptr_CoreShutdown shutdown;
    ptr_CoreAttachPlugin attach_plugin;
    ptr_CoreDetachPlugin detach_plugin;
    ptr_CoreDoCommand do_command;
    ptr_ConfigOpenSection config_open_section;
    ptr_ConfigSetParameter config_set_parameter;
    ptr_ConfigSaveFile config_save_file;
} CoreApi;

typedef struct Plugin {
    m64p_plugin_type type;
    const char *label;
    const char *path;
    IntegralN64RuntimeDynlib library;
    ptr_PluginShutdown shutdown;
    ptr_ReadScreen2 read_screen;
    ptr_ReadScreen2 read_room_screen;
    bool started;
    bool attached;
} Plugin;

typedef struct Frontend {
    Options options;
    CoreApi core;
    Plugin plugins[4];
    bool core_started;
    bool rom_open;
    bool screenshot_requested;
    atomic_int screenshot_result;
    IntegralN64ScreenshotNotice screenshot_notice;
    bool stop_requested;
    unsigned int last_frame;
    TransferPakMediaSession media_session;
    bool media_prepared;
    TransferPakMemorySession transfer_memory;
    atomic_int latest_core_state;
    atomic_int core_stop_sent;
    unsigned char *remote_media_frame;
    unsigned char *remote_media_stream_frame;
    size_t remote_media_frame_capacity;
    uint64_t remote_media_next_capture_us;
    uint64_t remote_media_last_callback_us;
    IntegralN64RuntimeRemoteMediaProducerMetrics remote_media_metrics;
    bool remote_media_open;
} Frontend;

typedef struct RemoteMediaGuard {
    Frontend *frontend;
    atomic_int stop;
} RemoteMediaGuard;

typedef struct StopRequestMonitor {
    Frontend *frontend;
    atomic_int stop;
    atomic_int request_sent;
} StopRequestMonitor;

static _Atomic(Frontend *) g_frontend;
static volatile sig_atomic_t g_stop_requested;

static int remote_media_guard_main(void *context);
static int stop_request_monitor_main(void *context);

static uint64_t monotonic_us(void)
{
    uint64_t frequency = SDL_GetPerformanceFrequency();
    if (frequency == 0) return (uint64_t)SDL_GetTicks64() * 1000u;
    return SDL_GetPerformanceCounter() * 1000000u / frequency;
}

static uint32_t duration_us_u32(uint64_t value)
{
    return value > UINT32_MAX ? UINT32_MAX : (uint32_t)value;
}

static bool remote_media_capture_due(Frontend *frontend, uint64_t now_us)
{
    if (frontend->remote_media_next_capture_us != 0 &&
        now_us < frontend->remote_media_next_capture_us) return false;

    uint64_t next_us = frontend->remote_media_next_capture_us;
    if (next_us == 0 || now_us - next_us >= INTEGRAL_N64_RUNTIME_REMOTE_MEDIA_CAPTURE_INTERVAL_US) {
        next_us = now_us;
    }
    frontend->remote_media_next_capture_us =
        next_us + INTEGRAL_N64_RUNTIME_REMOTE_MEDIA_CAPTURE_INTERVAL_US;
    return true;
}

static void stop_signal_handler(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

static void print_usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --rom FILE --core FILE --config-dir DIR --data-dir DIR\n"
            "          --screenshot-dir DIR --video FILE --audio FILE|dummy\n"
            "          --input FILE --rsp FILE --save-dir SESSION_DIR --save-name NAME\n"
            "          [--transfer-storage DIR]\n"
            "          [--remote-input-file FILE]\n"
            "          [--remote-media-file FILE]\n"
            "          [--stop-request-file FILE]\n"
            "          [--controller1 auto|keyboard] ... [--controller4 auto|keyboard]\n"
            "          [--controller-map1 SPEC] ... [--controller-map4 SPEC]\n"
            "          [--hotkeys SPEC]\n"
            "          [--interactive] [--pure-interpreter] [--verbose-log]\n"
            "          [--frame N] [--width N --height N]\n"
            "       %s --menu\n",
            program,
            program);
}

static bool parse_unsigned(const char *text, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;
    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 || parsed > UINT_MAX) {
        return false;
    }
    *value = (unsigned int)parsed;
    return true;
}

static bool parse_dimension(const char *text, int *value)
{
    unsigned int parsed;
    if (!parse_unsigned(text, &parsed) || parsed > INT_MAX) {
        return false;
    }
    *value = (int)parsed;
    return true;
}

static bool require_value(int argc, char **argv, int *index, const char **value)
{
    if (*index + 1 >= argc) {
        fprintf(stderr, "Missing value after %s\n", argv[*index]);
        return false;
    }
    *index += 1;
    *value = argv[*index];
    return true;
}

static bool parse_controller_mode(const char *text, int *mode)
{
    if (strcmp(text, "auto") == 0) {
        *mode = 2;
        return true;
    }
    if (strcmp(text, "keyboard") == 0) {
        *mode = 1;
        return true;
    }
    return false;
}

static bool parse_options(int argc, char **argv, Options *options)
{
    int i;
    memset(options, 0, sizeof(*options));
    options->screenshot_frame = INTEGRAL_N64_RUNTIME_DEFAULT_FRAME;
    options->transfer_sav_fd = -1;
    options->width = INTEGRAL_N64_RUNTIME_DEFAULT_WIDTH;
    options->height = INTEGRAL_N64_RUNTIME_DEFAULT_HEIGHT;
    options->transfer_mask = 0x0fu;
    for (i = 0; i < 4; ++i) options->controller_modes[i] = 2;

    for (i = 1; i < argc; ++i) {
        const char *value = NULL;
        if (strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        }
        if (strcmp(argv[i], "--interactive") == 0) {
            options->interactive = true;
            continue;
        }
        if (strcmp(argv[i], "--pure-interpreter") == 0) {
            options->pure_interpreter = true;
            continue;
        }
        if (strcmp(argv[i], "--verbose-log") == 0) {
            options->verbose_log = true;
            continue;
        }
        if (strcmp(argv[i], "--rom") == 0) {
            if (!require_value(argc, argv, &i, &options->rom_path)) return false;
        }
        else if (strcmp(argv[i], "--core") == 0) {
            if (!require_value(argc, argv, &i, &options->core_path)) return false;
        }
        else if (strcmp(argv[i], "--config-dir") == 0) {
            if (!require_value(argc, argv, &i, &options->config_dir)) return false;
        }
        else if (strcmp(argv[i], "--data-dir") == 0) {
            if (!require_value(argc, argv, &i, &options->data_dir)) return false;
        }
        else if (strcmp(argv[i], "--screenshot-dir") == 0) {
            if (!require_value(argc, argv, &i, &options->screenshot_dir)) return false;
        }
        else if (strcmp(argv[i], "--save-dir") == 0) {
            if (!require_value(argc, argv, &i, &options->save_dir)) return false;
        }
        else if (strcmp(argv[i], "--save-name") == 0) {
            if (!require_value(argc, argv, &i, &options->save_name)) return false;
        }
        else if (strcmp(argv[i], "--video") == 0) {
            if (!require_value(argc, argv, &i, &options->video_path)) return false;
        }
        else if (strcmp(argv[i], "--audio") == 0) {
            if (!require_value(argc, argv, &i, &options->audio_path)) return false;
        }
        else if (strcmp(argv[i], "--input") == 0) {
            if (!require_value(argc, argv, &i, &options->input_path)) return false;
        }
        else if (strcmp(argv[i], "--rsp") == 0) {
            if (!require_value(argc, argv, &i, &options->rsp_path)) return false;
        }
        else if (strcmp(argv[i], "--transfer-storage") == 0) {
            if (!require_value(argc, argv, &i, &options->transfer_storage)) return false;
        }
        else if (strcmp(argv[i], "--transfer-sav-session") == 0) {
            if (!require_value(argc, argv, &i, &options->transfer_sav_session)) return false;
        }
        else if (strcmp(argv[i], "--transfer-sav-fd") == 0) {
            unsigned int fd;
            if (!require_value(argc, argv, &i, &value) || !parse_unsigned(value, &fd) || fd < 3 || fd > INT_MAX) return false;
            options->transfer_sav_fd = (int)fd;
        }
        else if (strcmp(argv[i], "--remote-input-file") == 0) {
            if (!require_value(argc, argv, &i, &options->remote_input_file)) return false;
        }
        else if (strcmp(argv[i], "--remote-media-file") == 0) {
            if (!require_value(argc, argv, &i, &options->remote_media_file)) return false;
        }
        else if (strcmp(argv[i], "--stop-request-file") == 0) {
            if (!require_value(argc, argv, &i, &options->stop_request_file)) return false;
        }
        else if (strncmp(argv[i], "--controller", 12u) == 0 &&
                 argv[i][12] >= '1' && argv[i][12] <= '4' && argv[i][13] == '\0') {
            int controller = argv[i][12] - '1';
            if (!require_value(argc, argv, &i, &value) ||
                !parse_controller_mode(value, &options->controller_modes[controller])) return false;
        }
        else if (strncmp(argv[i], "--controller-map", 16u) == 0 &&
                 argv[i][16] >= '1' && argv[i][16] <= '4' && argv[i][17] == '\0') {
            int controller = argv[i][16] - '1';
            if (!require_value(argc, argv, &i, &options->controller_maps[controller]))
                return false;
            options->controller_modes[controller] = 1;
        }
        else if (strcmp(argv[i], "--hotkeys") == 0) {
            if (!require_value(argc, argv, &i, &options->hotkeys)) return false;
        }
        else if (strcmp(argv[i], "--frame") == 0) {
            if (!require_value(argc, argv, &i, &value) ||
                !parse_unsigned(value, &options->screenshot_frame)) return false;
        }
        else if (strcmp(argv[i], "--width") == 0) {
            if (!require_value(argc, argv, &i, &value) ||
                !parse_dimension(value, &options->width)) return false;
        }
        else if (strcmp(argv[i], "--height") == 0) {
            if (!require_value(argc, argv, &i, &value) ||
                !parse_dimension(value, &options->height)) return false;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    return ((options->transfer_sav_fd < 0 && !options->transfer_sav_session && !options->remote_media_file) ||
            (options->transfer_sav_fd >= 3 && options->transfer_sav_session &&
             options->transfer_storage && options->remote_media_file)) &&
           options->rom_path != NULL &&
           options->core_path != NULL &&
           options->config_dir != NULL &&
           options->data_dir != NULL &&
           options->screenshot_dir != NULL &&
           options->video_path != NULL &&
           options->audio_path != NULL &&
           options->input_path != NULL &&
           options->rsp_path != NULL &&
           options->save_dir != NULL &&
           options->save_name != NULL;
}

static bool g_verbose_log_enabled;

static void debug_callback(void *context, int level, const char *message)
{
    if (level == M64MSG_VERBOSE && !g_verbose_log_enabled) return;
    const char *source = context != NULL ? (const char *)context : "Mupen64Plus";
    const char *kind = "INFO";
    if (level == M64MSG_ERROR) kind = "ERROR";
    else if (level == M64MSG_WARNING) kind = "WARNING";
    else if (level == M64MSG_STATUS) kind = "STATUS";
    else if (level == M64MSG_VERBOSE) kind = "VERBOSE";
    printf("%s %s: %s\n", source, kind, message);
    fflush(stdout);
}

static void state_callback(void *context, m64p_core_param parameter, int value)
{
    Frontend *frontend = atomic_load(&g_frontend);
    (void)context;
    if (frontend && parameter == M64CORE_SCREENSHOT_CAPTURED) {
        atomic_store(&frontend->screenshot_result, value ? 1 : -1);
        if (frontend->remote_media_open)
            integral_n64_runtime_remote_media_finish_screenshot(value != 0);
    }
    if (parameter == M64CORE_EMU_STATE) {
        if (frontend != NULL)
            atomic_store(&frontend->latest_core_state, value);
        printf("N64 Runtime STATE: emulator=%d\n", value);
        fflush(stdout);
    }
}

static void frame_callback(unsigned int frame_index)
{
    Frontend *frontend = atomic_load(&g_frontend);
    if (frontend == NULL) {
        return;
    }
    frontend->last_frame = frame_index;
    if (frontend->remote_media_open &&
        integral_n64_runtime_remote_media_take_screenshot_request() &&
        frontend->core.do_command(M64CMD_TAKE_NEXT_SCREENSHOT, 0, NULL) != M64ERR_SUCCESS) {
        integral_n64_runtime_remote_media_finish_screenshot(0);
        atomic_store(&frontend->screenshot_result, -1);
    }
    Uint64 notice_now = SDL_GetTicks64();
    int captured = atomic_exchange(&frontend->screenshot_result, 0);
    if (captured)
        integral_n64_screenshot_notice_show(&frontend->screenshot_notice,
            SDL_GL_GetCurrentWindow(), captured > 0, notice_now);
    integral_n64_screenshot_notice_tick(&frontend->screenshot_notice, notice_now);
    uint64_t callback_us = monotonic_us();
    if (frontend->remote_media_open) {
        IntegralN64RuntimeRemoteMediaProducerMetrics *metrics = &frontend->remote_media_metrics;
        metrics->core_callbacks++;
        if (frontend->remote_media_last_callback_us != 0 &&
            callback_us >= frontend->remote_media_last_callback_us) {
            uint64_t interval_us = callback_us - frontend->remote_media_last_callback_us;
            uint32_t bounded_interval_us = duration_us_u32(interval_us);
            metrics->core_callback_interval_samples++;
            metrics->core_callback_interval_total_us += interval_us;
            if (bounded_interval_us > metrics->core_callback_interval_max_us) {
                metrics->core_callback_interval_max_us = bounded_interval_us;
            }
        }
        frontend->remote_media_last_callback_us = callback_us;
    }
    ptr_ReadScreen2 capture_screen = frontend->plugins[0].read_room_screen != NULL
        ? frontend->plugins[0].read_room_screen
        : frontend->plugins[0].read_screen;
    if (frontend->remote_media_open && capture_screen != NULL &&
        remote_media_capture_due(frontend, callback_us)) {
        IntegralN64RuntimeRemoteMediaProducerMetrics *metrics = &frontend->remote_media_metrics;
        metrics->capture_due++;
        int width = 0;
        int height = 0;
        capture_screen(NULL, &width, &height, 0);
        typedef void (APIENTRY *GetInteger)(GLenum, GLint *);
        GetInteger get_integer = NULL;
        void *get_integer_address = SDL_GL_GetProcAddress("glGetIntegerv");
        memcpy(&get_integer, &get_integer_address, sizeof(get_integer));
        GLint alignment = 0;
        if (get_integer) get_integer(GL_PACK_ALIGNMENT, &alignment);
        if (width > 0 && height > 0 && width <= 8192 && height <= 8192 &&
            (alignment == 1 || alignment == 2 || alignment == 4 || alignment == 8)) {
            size_t pitch = ((size_t)width * 3u + (size_t)alignment - 1u) &
                           ~((size_t)alignment - 1u);
            size_t required = pitch * (size_t)height;
            if (!frontend->remote_media_stream_frame)
                frontend->remote_media_stream_frame = malloc(INTEGRAL_N64_RUNTIME_STREAM_BYTES);
            if (frontend->remote_media_frame_capacity < required) {
                unsigned char *resized = realloc(frontend->remote_media_frame, required);
                if (resized != NULL) {
                    frontend->remote_media_frame = resized;
                    frontend->remote_media_frame_capacity = required;
                }
            }
            if (frontend->remote_media_frame_capacity >= required &&
                frontend->remote_media_stream_frame) {
                int captured_width = width, captured_height = height;
                uint64_t readback_started_us = monotonic_us();
                capture_screen(frontend->remote_media_frame,
                                                 &width,
                                                 &height,
                                                 0);
                uint64_t readback_us = monotonic_us() - readback_started_us;
                uint32_t bounded_readback_us = duration_us_u32(readback_us);
                metrics->readback_samples++;
                metrics->readback_total_us += readback_us;
                if (bounded_readback_us > metrics->readback_max_us) {
                    metrics->readback_max_us = bounded_readback_us;
                }
                uint64_t write_started_us = monotonic_us();
                int write_result = -1;
                if (width == captured_width && height == captured_height &&
                    integral_n64_runtime_scale_stream_frame(frontend->remote_media_frame,
                        (uint32_t)width, (uint32_t)height, pitch,
                        frontend->remote_media_stream_frame) == 0) {
                    write_result = integral_n64_runtime_remote_media_write_video(
                        frontend->remote_media_stream_frame,
                        INTEGRAL_N64_RUNTIME_STREAM_WIDTH,
                        INTEGRAL_N64_RUNTIME_STREAM_HEIGHT,
                        INTEGRAL_N64_RUNTIME_MEDIA_VIDEO_BOTTOM_UP);
                }
                uint64_t write_us = monotonic_us() - write_started_us;
                uint32_t bounded_write_us = duration_us_u32(write_us);
                metrics->ipc_write_samples++;
                metrics->ipc_write_total_us += write_us;
                if (bounded_write_us > metrics->ipc_write_max_us) {
                    metrics->ipc_write_max_us = bounded_write_us;
                }
                if (write_result == 0) metrics->capture_success++;
                else metrics->capture_failures++;
            }
            else {
                metrics->capture_failures++;
            }
        }
        else {
            metrics->capture_failures++;
        }
    }
    if (frontend->remote_media_open) {
        integral_n64_runtime_remote_media_write_producer_metrics(&frontend->remote_media_metrics);
    }
    if (g_stop_requested && !frontend->stop_requested) {
        if (frontend->core.do_command(M64CMD_STOP, 0, NULL) == M64ERR_SUCCESS) {
            frontend->stop_requested = true;
        }
        return;
    }
    if (frontend->options.interactive) {
        return;
    }
    if (!frontend->screenshot_requested &&
        frame_index >= frontend->options.screenshot_frame) {
        if (frontend->core.do_command(M64CMD_TAKE_NEXT_SCREENSHOT, 0, NULL) == M64ERR_SUCCESS) {
            frontend->screenshot_requested = true;
            printf("N64 Runtime STATUS: requested screenshot at frame %u\n", frame_index);
            fflush(stdout);
        }
        else {
            frontend->stop_requested = true;
            (void)frontend->core.do_command(M64CMD_STOP, 0, NULL);
        }
    }
    else if (frontend->screenshot_requested && !frontend->stop_requested &&
             frame_index > frontend->options.screenshot_frame) {
        if (frontend->core.do_command(M64CMD_STOP, 0, NULL) == M64ERR_SUCCESS) {
            frontend->stop_requested = true;
        }
    }
}

static void ignore_dd_region(void *context, uint8_t region)
{
    (void)context;
    (void)region;
}

static char *empty_dd_media(void *context)
{
    (void)context;
    return NULL;
}

static const char *core_error(const Frontend *frontend, m64p_error error)
{
    if (frontend->core.error_message != NULL) {
        return frontend->core.error_message(error);
    }
    return "unknown Mupen64Plus error";
}

static bool load_core_symbol(CoreApi *core, const char *name, void *destination, size_t size)
{
    if (!integral_n64_runtime_dynlib_symbol(&core->library, name, destination, size)) {
        fprintf(stderr, "N64 Runtime: missing core symbol %s: %s\n",
                name, integral_n64_runtime_dynlib_error());
        return false;
    }
    return true;
}

static bool load_core(Frontend *frontend)
{
    CoreApi *core = &frontend->core;
    m64p_plugin_type type = M64PLUGIN_NULL;
    int version = 0;
    int api_version = 0;
    int capabilities = 0;
    int config_api = 0;
    int debug_api = 0;
    int vidext_api = 0;
    int netplay_api = 0;
    const char *name = NULL;

    if (!integral_n64_runtime_dynlib_open(&core->library, frontend->options.core_path)) {
        fprintf(stderr, "N64 Runtime: failed to load core %s: %s\n",
                frontend->options.core_path, integral_n64_runtime_dynlib_error());
        return false;
    }

#define LOAD_CORE(member, symbol) \
    if (!load_core_symbol(core, symbol, &core->member, sizeof(core->member))) return false
    LOAD_CORE(get_version, "PluginGetVersion");
    LOAD_CORE(get_api_versions, "CoreGetAPIVersions");
    LOAD_CORE(error_message, "CoreErrorMessage");
    LOAD_CORE(startup, "CoreStartup");
    LOAD_CORE(shutdown, "CoreShutdown");
    LOAD_CORE(attach_plugin, "CoreAttachPlugin");
    LOAD_CORE(detach_plugin, "CoreDetachPlugin");
    LOAD_CORE(do_command, "CoreDoCommand");
    LOAD_CORE(config_open_section, "ConfigOpenSection");
    LOAD_CORE(config_set_parameter, "ConfigSetParameter");
    LOAD_CORE(config_save_file, "ConfigSaveFile");
#undef LOAD_CORE

    if (core->get_version(&type, &version, &api_version, &name, &capabilities) != M64ERR_SUCCESS ||
        type != M64PLUGIN_CORE ||
        (api_version & 0xffff0000) != INTEGRAL_N64_RUNTIME_EXPECTED_API_MAJOR) {
        fprintf(stderr, "N64 Runtime: incompatible Mupen64Plus core\n");
        return false;
    }
    if (core->get_api_versions(&config_api, &debug_api, &vidext_api, &netplay_api) !=
            M64ERR_SUCCESS ||
        (config_api & 0xffff0000) != INTEGRAL_N64_RUNTIME_EXPECTED_API_MAJOR) {
        fprintf(stderr, "N64 Runtime: incompatible Mupen64Plus configuration API\n");
        return false;
    }
    printf("N64 Runtime: loaded core '%s' version %d.%d.%d from %s\n",
           name != NULL ? name : "unknown",
           (version >> 16) & 0xff, (version >> 8) & 0xff, version & 0xff,
           frontend->options.core_path);
    return true;
}

static bool set_parameter(
    Frontend *frontend,
    m64p_handle section,
    const char *name,
    m64p_type type,
    const void *value)
{
    m64p_error error = frontend->core.config_set_parameter(section, name, type, value);
    if (error != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to set %s: %s\n",
                name, core_error(frontend, error));
        return false;
    }
    return true;
}

static bool configure_core(Frontend *frontend)
{
    m64p_handle core_section = NULL;
    m64p_handle video_section = NULL;
    char save_directory[TRANSFER_PAK_STORAGE_PATH_CAPACITY];
    char save_name[256];
    int disabled = 0;
    int interpreter = frontend->options.pure_interpreter ? 0 : 1;

    if (frontend->core.config_open_section("Core", &core_section) != M64ERR_SUCCESS ||
        frontend->core.config_open_section("Video-General", &video_section) != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to open core configuration sections\n");
        return false;
    }
    size_t dir_len = strlen(frontend->options.save_dir);
    if (dir_len == 0u || dir_len >= sizeof(save_directory) ||
        frontend->options.save_name[0] == '\0' ||
        strlen(frontend->options.save_name) >= sizeof(save_name)) {
        fprintf(stderr, "N64 Runtime: invalid session save path options\n");
        return false;
    }
    memcpy(save_directory, frontend->options.save_dir, dir_len + 1u);
    memcpy(save_name, frontend->options.save_name,
           strlen(frontend->options.save_name) + 1u);
    if (transfer_pak_storage_ensure_directory(save_directory) != 0) {
        fprintf(stderr, "N64 Runtime: failed to prepare session save directory\n");
        return false;
    }

    if (frontend->options.pure_interpreter) {
        printf("N64 Runtime: using Pure Interpreter for homebrew compatibility\n");
    }

    return set_parameter(frontend, core_section, "OnScreenDisplay", M64TYPE_BOOL, &disabled) &&
           set_parameter(frontend, core_section, "Integral Screenshot Mode", M64TYPE_STRING,
                         frontend->options.remote_media_file ? "n64_room" : "local_n64") &&
           set_parameter(frontend, core_section, "Integral Screenshot Role", M64TYPE_STRING,
                         frontend->options.remote_media_file ? "host" : "local") &&
           set_parameter(frontend, core_section, "EnableDebugger", M64TYPE_BOOL, &disabled) &&
           set_parameter(frontend, core_section, "R4300Emulator", M64TYPE_INT, &interpreter) &&
           set_parameter(frontend, core_section, "ScreenshotPath", M64TYPE_STRING,
                         frontend->options.screenshot_dir) &&
           set_parameter(frontend, core_section, "SaveSRAMPath", M64TYPE_STRING,
                         save_directory) &&
           set_parameter(frontend, core_section, "SaveStatePath", M64TYPE_STRING,
                         save_directory) &&
           set_parameter(frontend, core_section, "SaveFilenameOverride",
                         M64TYPE_STRING, save_name) &&
           set_parameter(frontend, video_section, "Fullscreen", M64TYPE_BOOL, &disabled) &&
           set_parameter(frontend, video_section, "ScreenWidth", M64TYPE_INT,
                         &frontend->options.width) &&
           set_parameter(frontend, video_section, "ScreenHeight", M64TYPE_INT,
                         &frontend->options.height);
}

static bool configure_emulator_hotkeys(Frontend *frontend)
{
    IntegralN64RuntimeHotkeys hotkeys;
    m64p_handle section = NULL;
    int i;
    integral_n64_runtime_hotkeys_defaults(&hotkeys);
    if (frontend->options.hotkeys &&
        !integral_n64_runtime_hotkeys_parse(frontend->options.hotkeys, &hotkeys)) {
        fprintf(stderr, "N64 Runtime: invalid emulator hotkey map\n");
        return false;
    }
    if (frontend->core.config_open_section("CoreEvents", &section) !=
        M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to open CoreEvents configuration\n");
        return false;
    }
    for (i = 0; i < INTEGRAL_N64_RUNTIME_HOTKEY_COUNT; ++i) {
        const IntegralN64RuntimeHotkey *entry = &hotkeys.entries[i];
        char legacy_name[64];
        char physical_name[64];
        char joy_name[64];
        char joy_mapping[64];
        int legacy = 0;
        int scancode = entry->binding.kind == INTEGRAL_N64_RUNTIME_BINDING_SCANCODE
                           ? entry->binding.index : 0;
        snprintf(legacy_name, sizeof(legacy_name), "Kbd Mapping %s",
                 integral_n64_runtime_hotkey_core_names[i]);
        snprintf(physical_name, sizeof(physical_name), "Kbd Scancode %s",
                 integral_n64_runtime_hotkey_core_names[i]);
        snprintf(joy_name, sizeof(joy_name), "Joy Mapping %s",
                 integral_n64_runtime_hotkey_core_names[i]);
        integral_n64_runtime_hotkey_joy_mapping(entry, joy_mapping, sizeof(joy_mapping));
        /* ROOM controller screenshots belong to the parent, including when
         * this game window has focus. Keyboard hotkeys remain window-local. */
        if (frontend->options.remote_media_file && i == 8) joy_mapping[0] = '\0';
        if (!set_parameter(frontend, section, legacy_name, M64TYPE_INT, &legacy) ||
            !set_parameter(frontend, section, physical_name, M64TYPE_INT,
                           &scancode) ||
            !set_parameter(frontend, section, joy_name, M64TYPE_STRING,
                           joy_mapping)) return false;
    }
    printf("N64 Runtime: emulator operation keys configured (%s by default)\n",
           frontend->options.hotkeys ? "CUSTOM" : "UNDEFINED");
    return true;
}

static bool load_plugin(Frontend *frontend, Plugin *plugin)
{
    ptr_PluginGetVersion get_version = NULL;
    ptr_PluginStartup startup = NULL;
    m64p_plugin_type actual_type = M64PLUGIN_NULL;
    int version = 0;
    const char *name = NULL;
    m64p_error error;

    if (plugin->path == NULL) {
        return true;
    }
    if (!integral_n64_runtime_dynlib_open(&plugin->library, plugin->path)) {
        fprintf(stderr, "N64 Runtime: failed to load %s plugin %s: %s\n",
                plugin->label, plugin->path, integral_n64_runtime_dynlib_error());
        return false;
    }
    if (!integral_n64_runtime_dynlib_symbol(&plugin->library, "PluginGetVersion",
                                 &get_version, sizeof(get_version)) ||
        !integral_n64_runtime_dynlib_symbol(&plugin->library, "PluginStartup",
                                 &startup, sizeof(startup)) ||
        !integral_n64_runtime_dynlib_symbol(&plugin->library, "PluginShutdown",
                                 &plugin->shutdown, sizeof(plugin->shutdown))) {
        fprintf(stderr, "N64 Runtime: %s plugin is missing its public API\n", plugin->label);
        return false;
    }
    if (plugin->type == M64PLUGIN_GFX &&
        !integral_n64_runtime_dynlib_symbol(&plugin->library,
                                 "ReadScreen2",
                                 &plugin->read_screen,
                                 sizeof(plugin->read_screen))) {
        fprintf(stderr, "N64 Runtime: video plugin cannot capture remote frames\n");
        return false;
    }
    if (plugin->type == M64PLUGIN_GFX) {
        (void)integral_n64_runtime_dynlib_symbol(&plugin->library,
                                 "IntegralReadGameScreen2",
                                 &plugin->read_room_screen,
                                 sizeof(plugin->read_room_screen));
        if (frontend->options.remote_media_file != NULL &&
            plugin->read_room_screen == NULL) {
            fprintf(stderr,
                    "N64 Runtime WARNING: video plugin lacks ROOM game-frame capture; "
                    "falling back to ReadScreen2\n");
        }
    }
    if (get_version(&actual_type, &version, NULL, &name, NULL) != M64ERR_SUCCESS ||
        actual_type != plugin->type) {
        fprintf(stderr, "N64 Runtime: incompatible %s plugin %s\n",
                plugin->label, plugin->path);
        return false;
    }
    error = startup(frontend->core.library.handle, (void *)plugin->label, debug_callback);
    if (error != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to start %s plugin: %s\n",
                plugin->label, core_error(frontend, error));
        return false;
    }
    plugin->started = true;
    printf("N64 Runtime: loaded %s plugin '%s' version %d.%d.%d from %s\n",
           plugin->label, name != NULL ? name : "unknown",
           (version >> 16) & 0xff, (version >> 8) & 0xff, version & 0xff,
           plugin->path);
    return true;
}

static bool configure_transfer_paks(Frontend *frontend)
{
    int controller;
    if (frontend->options.transfer_storage == NULL) {
        return true;
    }
    for (controller = 0; controller < 4; ++controller) {
        char section_name[32];
        m64p_handle section = NULL;
        int plugin = transfer_pak_media_session_ready(&frontend->media_session,
                                               controller) ? 4 : 2;
        (void)snprintf(section_name, sizeof(section_name),
                       "Input-SDL-Control%d", controller + 1);
        if (frontend->core.config_open_section(section_name, &section) !=
                M64ERR_SUCCESS ||
            !set_parameter(frontend, section, "plugin", M64TYPE_INT,
                           &plugin)) {
            fprintf(stderr, "N64 Runtime: failed to enable Transfer Pak %d\n",
                    controller + 1);
            return false;
        }
    }
    return true;
}

static bool configure_controllers(Frontend *frontend)
{
    m64p_handle runtime_section = NULL;
    int local_four_ports = frontend->options.remote_input_file == NULL;
    if (frontend->core.config_open_section("IntegralRuntime", &runtime_section) != M64ERR_SUCCESS ||
        !set_parameter(frontend, runtime_section, "LocalFourPorts", M64TYPE_BOOL, &local_four_ports)) return false;
    int controller;
    for (controller = 0; controller < 4; ++controller) {
        char section_name[32];
        m64p_handle section = NULL;
        int mode = frontend->options.controller_modes[controller];
        int device = -1;
        int plugged = 1;
        const char *name = mode == 1 ? "Keyboard" : "";
        IntegralN64RuntimeKeymap keymap;
        bool custom = frontend->options.controller_maps[controller] != NULL;
        (void)snprintf(section_name, sizeof(section_name),
                       "Input-SDL-Control%d", controller + 1);
        if (custom && !integral_n64_runtime_keymap_parse(
                          frontend->options.controller_maps[controller], &keymap)) {
            fprintf(stderr, "N64 Runtime: invalid controller %d key map\n",
                    controller + 1);
            return false;
        }
        if (custom) {
            mode = 0;
            device = keymap.device;
            name = device < 0 ? "Keyboard" : "N64 Runtime Custom Controller";
        }
        if (frontend->core.config_open_section(section_name, &section) != M64ERR_SUCCESS ||
            !set_parameter(frontend, section, "mode", M64TYPE_INT, &mode) ||
            !set_parameter(frontend, section, "device", M64TYPE_INT, &device) ||
            !set_parameter(frontend, section, "name", M64TYPE_STRING, name) ||
            !set_parameter(frontend, section, "plugged", M64TYPE_BOOL, &plugged)) {
            fprintf(stderr, "N64 Runtime: failed to configure controller %d\n",
                    controller + 1);
            return false;
        }
        if (custom) {
            int binding;
            char parameter[128];
            for (binding = 0; binding < 14; ++binding) {
                integral_n64_runtime_keymap_mupen_button(&keymap.bindings[binding],
                                              parameter, sizeof(parameter));
                if (!set_parameter(frontend, section,
                                   integral_n64_runtime_mupen_button_names[binding],
                                   M64TYPE_STRING, parameter)) return false;
            }
            integral_n64_runtime_keymap_mupen_axis(&keymap.bindings[15],
                                        &keymap.bindings[14], parameter,
                                        sizeof(parameter));
            if (!set_parameter(frontend, section, "X Axis", M64TYPE_STRING,
                               parameter)) return false;
            integral_n64_runtime_keymap_mupen_axis(&keymap.bindings[16],
                                        &keymap.bindings[17], parameter,
                                        sizeof(parameter));
            if (!set_parameter(frontend, section, "Y Axis", M64TYPE_STRING,
                               parameter)) return false;
        }
        printf("N64 Runtime: controller %d profile %s\n", controller + 1,
               custom ? "CUSTOM" : (mode == 1 ? "KEYBOARD" : "AUTO"));
    }
    return true;
}

static bool read_rom(const char *path, unsigned char **data, int *size)
{
    FILE *file = integral_fopen(path, "rb");
    long length;
    unsigned char *buffer;
    if (file == NULL) {
        fprintf(stderr, "N64 Runtime: failed to open ROM %s\n", path);
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0 ||
        (length = ftell(file)) <= 0 || length > INT_MAX ||
        fseek(file, 0, SEEK_SET) != 0) {
        fprintf(stderr, "N64 Runtime: invalid ROM size for %s\n", path);
        fclose(file);
        return false;
    }
    buffer = malloc((size_t)length);
    if (buffer == NULL || fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "N64 Runtime: failed to read ROM %s\n", path);
        free(buffer);
        fclose(file);
        return false;
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "N64 Runtime: failed to close ROM %s\n", path);
        free(buffer);
        return false;
    }
    *data = buffer;
    *size = (int)length;
    return true;
}

static void cleanup(Frontend *frontend)
{
    int i;
    if (frontend->options.transfer_sav_fd >= 0) {
        transfer_sav_close(frontend->options.transfer_sav_fd);
        frontend->options.transfer_sav_fd = -1;
    }
    for (i = 3; i >= 0; --i) {
        if (frontend->plugins[i].attached) {
            (void)frontend->core.detach_plugin(frontend->plugins[i].type);
            frontend->plugins[i].attached = false;
        }
    }
    for (i = 3; i >= 0; --i) {
        Plugin *plugin = &frontend->plugins[i];
        if (plugin->started && plugin->shutdown != NULL) {
            (void)plugin->shutdown();
            plugin->started = false;
        }
        integral_n64_runtime_dynlib_close(&plugin->library);
    }
    if (frontend->rom_open) {
        (void)frontend->core.do_command(M64CMD_ROM_CLOSE, 0, NULL);
        frontend->rom_open = false;
    }
    if (frontend->media_prepared) {
        transfer_pak_media_session_discard(&frontend->media_session);
        frontend->media_prepared = false;
    }
    if (frontend->core_started) {
        if (frontend->core.config_save_file != NULL) {
            (void)frontend->core.config_save_file();
        }
        (void)frontend->core.shutdown();
        frontend->core_started = false;
    }
    integral_n64_runtime_dynlib_close(&frontend->core.library);
    transfer_pak_memory_clear(&frontend->transfer_memory);
    if (frontend->remote_media_open) {
        integral_n64_runtime_remote_media_close();
        frontend->remote_media_open = false;
    }
    free(frontend->remote_media_frame);
    free(frontend->remote_media_stream_frame);
    frontend->remote_media_stream_frame = NULL;
    frontend->remote_media_frame = NULL;
    frontend->remote_media_frame_capacity = 0;
}

static int run_frontend(Frontend *frontend)
{
    unsigned char *rom_data = NULL;
    int rom_size = 0;
    int i;
    void (*callback)(unsigned int) = frame_callback;
    void *callback_argument = NULL;
    m64p_media_loader media_loader;
    m64p_error error;
    RemoteMediaGuard remote_media_guard;
    IntegralN64RuntimeThread remote_media_guard_thread;
    bool remote_media_guard_started = false;
    StopRequestMonitor stop_request_monitor;
    IntegralN64RuntimeThread stop_request_monitor_thread;
    bool stop_request_monitor_started = false;

    if (frontend->options.transfer_sav_fd >= 0) {
        int fd = frontend->options.transfer_sav_fd;
        frontend->options.transfer_sav_fd = -1;
        if (transfer_pak_memory_prepare(&frontend->transfer_memory, &frontend->media_session,
                frontend->options.transfer_storage, frontend->options.transfer_sav_session, fd) != 0) {
            fprintf(stderr, "N64 Runtime: Transfer Pak memory IPC rejected\n");
            return 5;
        }
    }

    if (SDL_WasInit(SDL_INIT_VIDEO) == 0u &&
        SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "N64 Runtime: SDL input initialization failed: %s\n",
                SDL_GetError());
        return 14;
    }

    if (!load_core(frontend)) return 2;
    error = frontend->core.startup(
        INTEGRAL_N64_RUNTIME_FRONTEND_API_VERSION,
        frontend->options.config_dir,
        frontend->options.data_dir,
        (void *)"Core",
        debug_callback,
        NULL,
        state_callback);
    if (error != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to start core: %s\n", core_error(frontend, error));
        return 3;
    }
    frontend->core_started = true;
    if (!configure_core(frontend)) return 4;
    if (!configure_emulator_hotkeys(frontend)) return 4;
    if (frontend->options.transfer_sav_session != NULL) {
        m64p_error (CALL *install_memory)(unsigned int, IntegralTransferMemory *, unsigned int) = NULL;
        if (!integral_n64_runtime_dynlib_symbol(&frontend->core.library,
                "IntegralSetTransferPakMemory", &install_memory, sizeof(install_memory)) ||
            install_memory(INTEGRAL_TRANSFER_MEMORY_VERSION, frontend->transfer_memory.slots, 2) != M64ERR_SUCCESS) {
            fprintf(stderr, "N64 Runtime: Transfer Pak memory backend unavailable\n");
            return 5;
        }
        printf("N64 Runtime: Transfer Pak memory ready ports=2 no_save=1\n");
    } else if (frontend->options.transfer_storage != NULL) {
        if (transfer_pak_media_session_prepare_mask(
                &frontend->media_session, frontend->options.transfer_storage,
                frontend->options.transfer_mask) != 0) {
            fprintf(stderr, "N64 Runtime: failed to prepare Transfer Pak storage %s\n",
                    frontend->options.transfer_storage);
            return 5;
        }
        frontend->media_prepared = true;
    }
    if (!read_rom(frontend->options.rom_path, &rom_data, &rom_size)) return 5;
    error = frontend->core.do_command(M64CMD_ROM_OPEN, rom_size, rom_data);
    free(rom_data);
    if (error != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: core rejected ROM: %s\n", core_error(frontend, error));
        return 6;
    }
    frontend->rom_open = true;

    for (i = 0; i < 4; ++i) {
        if (!load_plugin(frontend, &frontend->plugins[i])) return 7;
    }
    if (!configure_controllers(frontend)) return 8;
    if (!configure_transfer_paks(frontend)) return 8;
    for (i = 0; i < 4; ++i) {
        Plugin *plugin = &frontend->plugins[i];
        error = frontend->core.attach_plugin(plugin->type, plugin->library.handle);
        if (error != M64ERR_SUCCESS) {
            fprintf(stderr, "N64 Runtime: failed to attach %s plugin: %s\n",
                    plugin->label, core_error(frontend, error));
            return 8;
        }
        plugin->attached = true;
    }

    _Static_assert(sizeof(callback) == sizeof(callback_argument),
                   "Mupen64Plus frame callback pointer size mismatch");
    memcpy(&callback_argument, &callback, sizeof(callback_argument));
    if (frontend->core.do_command(M64CMD_SET_FRAME_CALLBACK, 0, callback_argument) !=
        M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to register frame callback\n");
        return 9;
    }

    memset(&media_loader, 0, sizeof(media_loader));
    media_loader.cb_data = &frontend->media_session;
    media_loader.get_gb_cart_rom = transfer_pak_media_session_get_rom;
    media_loader.get_gb_cart_ram = transfer_pak_media_session_get_ram;
    media_loader.set_dd_rom_region = ignore_dd_region;
    media_loader.get_dd_rom = empty_dd_media;
    media_loader.get_dd_disk = empty_dd_media;
    if (frontend->core.do_command(M64CMD_SET_MEDIA_LOADER,
                                  (int)sizeof(media_loader), &media_loader) != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: failed to register media loader\n");
        return 10;
    }

    if (frontend->options.remote_media_file != NULL) {
        if (integral_n64_runtime_remote_media_open_writer(frontend->options.remote_media_file) != 0) {
            fprintf(stderr, "N64 Runtime: failed to open remote media IPC %s\n",
                    frontend->options.remote_media_file);
            return 15;
        }
        frontend->remote_media_open = true;
        printf("N64 Runtime: remote media IPC active at %s\n",
               frontend->options.remote_media_file);
    }

    g_frontend = frontend;
    printf("N64 Runtime: executing ROM %s\n", frontend->options.rom_path);
    if (frontend->remote_media_open) {
        memset(&remote_media_guard, 0, sizeof(remote_media_guard));
        remote_media_guard.frontend = frontend;
        atomic_init(&remote_media_guard.stop, 0);
        if (integral_n64_runtime_thread_start(&remote_media_guard_thread,
                                  remote_media_guard_main,
                                  &remote_media_guard) != 0) {
            fprintf(stderr,
                    "N64 Runtime: could not start remote media pause guard\n");
            g_frontend = NULL;
            return 16;
        }
        remote_media_guard_started = true;
    }
    if (frontend->options.stop_request_file != NULL) {
        memset(&stop_request_monitor, 0, sizeof(stop_request_monitor));
        stop_request_monitor.frontend = frontend;
        atomic_init(&stop_request_monitor.stop, 0);
        atomic_init(&stop_request_monitor.request_sent, 0);
        if (integral_n64_runtime_thread_start(&stop_request_monitor_thread,
                                  stop_request_monitor_main,
                                  &stop_request_monitor) != 0) {
            fprintf(stderr,
                    "N64 Runtime: could not start stop-request monitor\n");
            if (remote_media_guard_started) {
                int guard_result = 0;
                atomic_store(&remote_media_guard.stop, 1);
                (void)integral_n64_runtime_thread_join(&remote_media_guard_thread,
                                                       &guard_result);
            }
            g_frontend = NULL;
            return 17;
        }
        stop_request_monitor_started = true;
        printf("N64 Runtime: stop-request monitor active at %s\n",
               frontend->options.stop_request_file);
        fflush(stdout);
    }
    error = frontend->core.do_command(M64CMD_EXECUTE, 0, NULL);
    if (stop_request_monitor_started) {
        int monitor_result = 0;
        atomic_store(&stop_request_monitor.stop, 1);
        (void)integral_n64_runtime_thread_join(&stop_request_monitor_thread,
                                               &monitor_result);
        if (atomic_load(&stop_request_monitor.request_sent)) {
            frontend->stop_requested = true;
        }
    }
    if (remote_media_guard_started) {
        int guard_result = 0;
        atomic_store(&remote_media_guard.stop, 1);
        (void)integral_n64_runtime_thread_join(&remote_media_guard_thread, &guard_result);
    }
    g_frontend = NULL;
    if (frontend->media_prepared) {
        if (transfer_pak_media_session_finalize(&frontend->media_session) != 0) {
            fprintf(stderr, "N64 Runtime: failed to merge Transfer Pak saves\n");
            return 13;
        }
        frontend->media_prepared = false;
    }
    if (error != M64ERR_SUCCESS) {
        fprintf(stderr, "N64 Runtime: execution failed: %s\n", core_error(frontend, error));
        return 11;
    }
    if (!frontend->options.interactive &&
        (!frontend->screenshot_requested || !frontend->stop_requested)) {
        fprintf(stderr, "N64 Runtime: frame-controlled smoke run did not complete\n");
        return 12;
    }
    printf("N64 Runtime: execution complete at frame %u\n", frontend->last_frame);
    return 0;
}

static void initialize_frontend(Frontend *frontend, const Options *options)
{
    memset(frontend, 0, sizeof(*frontend));
    frontend->options = *options;
    frontend->plugins[0] = (Plugin){M64PLUGIN_GFX, "Video", options->video_path,
                                    {0}, NULL, NULL, NULL, false, false};
    frontend->plugins[1] =
        (Plugin){M64PLUGIN_AUDIO, "Audio",
                 strcmp(options->audio_path, "dummy") == 0
                     ? NULL : options->audio_path,
                 {0}, NULL, NULL, NULL, false, false};
    frontend->plugins[2] = (Plugin){M64PLUGIN_INPUT, "Input", options->input_path,
                                    {0}, NULL, NULL, NULL, false, false};
    frontend->plugins[3] = (Plugin){M64PLUGIN_RSP, "RSP", options->rsp_path,
                                    {0}, NULL, NULL, NULL, false, false};
    atomic_init(&frontend->latest_core_state, M64EMU_STOPPED);
    atomic_init(&frontend->screenshot_result, 0);
    atomic_init(&frontend->core_stop_sent, 0);
    integral_n64_runtime_remote_input_configure(options->remote_input_file);
}

static int remote_media_guard_main(void *context)
{
    RemoteMediaGuard *guard = context;
    while (!atomic_load(&guard->stop)) {
        Frontend *frontend = guard->frontend;
        if (atomic_load(&frontend->latest_core_state) == M64EMU_PAUSED &&
            frontend->core.do_command(M64CMD_RESUME, 0, NULL) ==
                M64ERR_SUCCESS) {
            printf("N64 Runtime REMOTE MEDIA: paused core resumed\n");
            fflush(stdout);
        }
        integral_n64_runtime_thread_sleep(100u);
    }
    return 0;
}

static bool stop_request_file_is_valid(const char *path)
{
    static const unsigned char expected[] = "S64STOP1\n";
    unsigned char record[sizeof(expected) - 1u];
    FILE *file;
    if (path == NULL || path[0] == '\0') return false;
    file = fopen(path, "rb");
    if (file == NULL) return false;
    bool valid = fread(record, 1, sizeof(record), file) == sizeof(record) &&
                 memcmp(record, expected, sizeof(record)) == 0 &&
                 fgetc(file) == EOF;
    (void)fclose(file);
    return valid;
}

static int stop_request_monitor_main(void *context)
{
    StopRequestMonitor *monitor = context;
    bool request_seen = false;
    while (!atomic_load(&monitor->stop)) {
        Frontend *frontend = monitor->frontend;
        if (!request_seen &&
            stop_request_file_is_valid(frontend->options.stop_request_file)) {
            request_seen = true;
            printf("N64 Runtime: session stop request received\n");
            fflush(stdout);
        }
        if (request_seen && !atomic_load(&monitor->request_sent)) {
            int state = atomic_load(&frontend->latest_core_state);
            if ((state == M64EMU_RUNNING || state == M64EMU_PAUSED) &&
                !atomic_exchange(&frontend->core_stop_sent, 1)) {
                m64p_error error =
                    frontend->core.do_command(M64CMD_STOP, 0, NULL);
                if (error == M64ERR_SUCCESS) {
                    atomic_store(&monitor->request_sent, 1);
                    printf("N64 Runtime: session stop dispatched to Core\n");
                    fflush(stdout);
                }
                else {
                    atomic_store(&frontend->core_stop_sent, 0);
                }
            }
        }
        integral_n64_runtime_thread_sleep(25u);
    }
    return 0;
}

int main(int argc, char **argv)
{
    Frontend frontend;
    int result;
    Options options;
    memset(&options, 0, sizeof(options));
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--menu") == 0)) {
        return integral_n64_runtime_gui_run(argv[0], NULL, 0);
    }
    if (argc == 2 && strcmp(argv[1], "--config-load-smoke") == 0) {
        return integral_n64_runtime_gui_config_load_test();
    }
    if (argc == 3 && strcmp(argv[1], "--menu-smoke") == 0) {
        return integral_n64_runtime_gui_run(argv[0], argv[2], 0);
    }
    if (argc == 3 && strcmp(argv[1], "--menu-smoke-controllers") == 0) {
        return integral_n64_runtime_gui_run(argv[0], argv[2], 1);
    }
    if (argc == 3 && strcmp(argv[1], "--menu-smoke-transfer") == 0) {
        return integral_n64_runtime_gui_run(argv[0], argv[2], 2);
    }
    if (argc == 3 && strcmp(argv[1], "--menu-smoke-key-config") == 0) {
        return integral_n64_runtime_gui_run(argv[0], argv[2], 3);
    }
    if (argc == 3 && strcmp(argv[1], "--menu-smoke-hotkeys") == 0) {
        return integral_n64_runtime_gui_run(argv[0], argv[2], 4);
    }
    if (signal(SIGINT, stop_signal_handler) == SIG_ERR ||
        signal(SIGTERM, stop_signal_handler) == SIG_ERR) {
        fprintf(stderr, "N64 Runtime: could not install stop signal handlers\n");
        return 1;
    }
    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return 1;
    }
    g_verbose_log_enabled = options.verbose_log;
    initialize_frontend(&frontend, &options);
    result = run_frontend(&frontend);
    cleanup(&frontend);
    return result;
}

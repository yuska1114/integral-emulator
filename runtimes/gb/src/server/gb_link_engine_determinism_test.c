/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gb_link_engine.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct InputTrace {
    uint8_t *a;
    uint8_t *b;
    bool *seen;
    unsigned frames;
} InputTrace;

typedef struct CheckpointSchedule {
    unsigned *frames;
    size_t count;
} CheckpointSchedule;

static int write_slot_bmp(const char *path, const IntegralGBRuntimeSlot *slot);

static void usage(const char *program)
{
    fprintf(stderr,
            "Usage: %s --rom-a PATH --rom-b PATH [--save-a PATH] [--save-b PATH] "
            "[--input-trace PATH] [--frames N] [--checkpoint N] [--repetitions N] "
            "[--checkpoint-list N,N,...] "
            "[--render-profile same|cross] [--require-serial-events] "
            "[--require-ir-events] [--require-rtc-day-at-least N] "
            "[--rtc-offset-minutes N] "
            "[--screenshot-a PATH --screenshot-b PATH] "
            "[--initial-state-prefix PATH]\n",
            program);
}

static int write_buffer_file(const char *path, const uint8_t *data, size_t size)
{
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    if (fwrite(data, 1, size, file) != size) {
        fclose(file);
        return -1;
    }
    return fclose(file) == 0 ? 0 : -1;
}

static int write_initial_state_pair(const char *prefix,
                                    IntegralGBRuntimeLinkEngine *engine)
{
    char path_a[4096], path_b[4096];
    int length_a = snprintf(path_a, sizeof(path_a), "%s_a.state", prefix);
    int length_b = snprintf(path_b, sizeof(path_b), "%s_b.state", prefix);
    if (length_a < 0 || (size_t)length_a >= sizeof(path_a) ||
        length_b < 0 || (size_t)length_b >= sizeof(path_b)) {
        return -1;
    }
    IntegralGBRuntimeLinkSnapshot snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    if (integral_gb_runtime_link_engine_snapshot(engine, &snapshot) != 0) return -1;
    int result = write_buffer_file(path_a, snapshot.state_a, snapshot.state_a_size);
    if (result == 0) {
        result = write_buffer_file(path_b, snapshot.state_b, snapshot.state_b_size);
    }
    integral_gb_runtime_link_snapshot_free(&snapshot);
    return result;
}

static void encode_u32_le(uint8_t *dest, uint32_t value)
{
    dest[0] = (uint8_t)value;
    dest[1] = (uint8_t)(value >> 8);
    dest[2] = (uint8_t)(value >> 16);
    dest[3] = (uint8_t)(value >> 24);
}

static int write_slot_bmp(const char *path, const IntegralGBRuntimeSlot *slot)
{
    enum {
        width = INTEGRAL_GB_RUNTIME_GB_WIDTH,
        height = INTEGRAL_GB_RUNTIME_GB_HEIGHT,
        header_size = 54,
        pixel_size = width * height * 4,
    };
    FILE *file = fopen(path, "wb");
    if (!file) return -1;
    uint8_t header[header_size] = {0};
    header[0] = 'B';
    header[1] = 'M';
    encode_u32_le(header + 2, header_size + pixel_size);
    encode_u32_le(header + 10, header_size);
    encode_u32_le(header + 14, 40);
    encode_u32_le(header + 18, width);
    encode_u32_le(header + 22, (uint32_t)(-(int32_t)height)); /* top-down */
    header[26] = 1;
    header[28] = 32;
    encode_u32_le(header + 34, pixel_size);
    if (fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
        fclose(file);
        return -1;
    }
    uint8_t row[width * 4];
    for (unsigned y = 0; y < height; y++) {
        for (unsigned x = 0; x < width; x++) {
            uint32_t pixel = slot->pixels[y * width + x];
            row[x * 4] = (uint8_t)pixel;
            row[x * 4 + 1] = (uint8_t)(pixel >> 8);
            row[x * 4 + 2] = (uint8_t)(pixel >> 16);
            row[x * 4 + 3] = 0xFF;
        }
        if (fwrite(row, 1, sizeof(row), file) != sizeof(row)) {
            fclose(file);
            return -1;
        }
    }
    return fclose(file) == 0 ? 0 : -1;
}

static int read_file(const char *path, uint8_t **data, size_t *size)
{
    *data = NULL;
    *size = 0;
    if (!path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    long length = ftell(file);
    if (length < 0 || length > 16 * 1024 * 1024) { fclose(file); return -1; }
    rewind(file);
    uint8_t *buffer = length > 0 ? malloc((size_t)length) : NULL;
    if (length > 0 && (!buffer || fread(buffer, 1, (size_t)length, file) != (size_t)length)) {
        free(buffer);
        fclose(file);
        return -1;
    }
    fclose(file);
    *data = buffer;
    *size = (size_t)length;
    return 0;
}

static int parse_unsigned(const char *text, unsigned *value)
{
    char *end = NULL;
    errno = 0;
    unsigned long parsed = strtoul(text, &end, 10);
    if (errno || !end || *end || parsed == 0 || parsed > 10000000ul) return -1;
    *value = (unsigned)parsed;
    return 0;
}

static int load_trace(const char *path, unsigned frames, InputTrace *trace)
{
    memset(trace, 0, sizeof(*trace));
    trace->frames = frames;
    trace->a = calloc(frames, 1);
    trace->b = calloc(frames, 1);
    trace->seen = calloc(frames, sizeof(bool));
    if (!trace->a || !trace->b || !trace->seen) return -1;
    if (!path) {
        for (unsigned i = 0; i < frames; i++) trace->seen[i] = true;
        return 0;
    }
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    char line[256];
    unsigned last = 0;
    bool have_last = false;
    unsigned line_number = 0;
    while (fgets(line, sizeof(line), file)) {
        line_number++;
        char *cursor = line;
        while (*cursor == ' ' || *cursor == '\t') cursor++;
        if (*cursor == '\0' || *cursor == '\n' || *cursor == '#') continue;
        unsigned frame, a, b;
        char extra;
        if (sscanf(cursor, "%u %2x %2x %c", &frame, &a, &b, &extra) != 3 ||
            frame >= frames || a > 255 || b > 255 || (have_last && frame <= last)) {
            fprintf(stderr, "Invalid input trace at line %u\n", line_number);
            fclose(file);
            return -1;
        }
        trace->a[frame] = (uint8_t)a;
        trace->b[frame] = (uint8_t)b;
        trace->seen[frame] = true;
        last = frame;
        have_last = true;
    }
    fclose(file);
    for (unsigned i = 0; i < frames; i++) {
        if (!trace->seen[i]) {
            fprintf(stderr, "Input trace missing frame %u\n", i);
            return -1;
        }
    }
    return 0;
}

static void free_trace(InputTrace *trace)
{
    free(trace->a);
    free(trace->b);
    free(trace->seen);
    memset(trace, 0, sizeof(*trace));
}

static int build_checkpoint_schedule(const char *list,
                                     unsigned interval,
                                     unsigned total_frames,
                                     CheckpointSchedule *schedule)
{
    memset(schedule, 0, sizeof(*schedule));
    if (!list) {
        size_t capacity = (total_frames + interval - 1) / interval + 1;
        schedule->frames = calloc(capacity, sizeof(*schedule->frames));
        if (!schedule->frames) return -1;
        for (unsigned frame = interval; frame <= total_frames; frame += interval) {
            schedule->frames[schedule->count++] = frame;
            if (frame > total_frames - interval) break;
        }
        if (schedule->count == 0 || schedule->frames[schedule->count - 1] != total_frames) {
            schedule->frames[schedule->count++] = total_frames;
        }
        return 0;
    }

    size_t length = strlen(list);
    char *copy = malloc(length + 1);
    schedule->frames = calloc(length / 2 + 2, sizeof(*schedule->frames));
    if (!copy || !schedule->frames) {
        free(copy);
        free(schedule->frames);
        memset(schedule, 0, sizeof(*schedule));
        return -1;
    }
    memcpy(copy, list, length + 1);
    char *cursor = copy;
    unsigned previous = 0;
    while (*cursor) {
        char *comma = strchr(cursor, ',');
        if (comma) *comma = '\0';
        unsigned frame;
        if (parse_unsigned(cursor, &frame) != 0 || frame > total_frames ||
            (schedule->count > 0 && frame <= previous)) {
            fprintf(stderr, "Invalid checkpoint list: %s\n", list);
            free(copy);
            free(schedule->frames);
            memset(schedule, 0, sizeof(*schedule));
            return -1;
        }
        schedule->frames[schedule->count++] = frame;
        previous = frame;
        if (!comma) break;
        cursor = comma + 1;
        if (!*cursor) {
            fprintf(stderr, "Invalid checkpoint list: %s\n", list);
            free(copy);
            free(schedule->frames);
            memset(schedule, 0, sizeof(*schedule));
            return -1;
        }
    }
    free(copy);
    if (schedule->count == 0) return -1;
    if (schedule->frames[schedule->count - 1] != total_frames) {
        schedule->frames[schedule->count++] = total_frames;
    }
    return 0;
}

static void free_checkpoint_schedule(CheckpointSchedule *schedule)
{
    free(schedule->frames);
    memset(schedule, 0, sizeof(*schedule));
}

static size_t first_difference(const uint8_t *a, const uint8_t *b, size_t size)
{
    for (size_t i = 0; i < size; i++) if (a[i] != b[i]) return i;
    return size;
}

static int compare_buffer(const char *label,
                          uint64_t frame,
                          const uint8_t *a,
                          size_t a_size,
                          const uint8_t *b,
                          size_t b_size)
{
    if (a_size != b_size) {
        fprintf(stderr, "DESYNC frame=%llu buffer=%s size=%zu/%zu\n",
                (unsigned long long)frame, label, a_size, b_size);
        return -1;
    }
    size_t offset = first_difference(a, b, a_size);
    if (offset != a_size) {
        fprintf(stderr, "DESYNC frame=%llu buffer=%s offset=%zu bytes=%02x/%02x\n",
                (unsigned long long)frame, label, offset, a[offset], b[offset]);
        return -1;
    }
    return 0;
}

static int compare_snapshot(const IntegralGBRuntimeLinkSnapshot *left,
                            const IntegralGBRuntimeLinkSnapshot *right)
{
    if (left->logical_frame != right->logical_frame ||
        left->serial_events != right->serial_events || left->ir_events != right->ir_events ||
        compare_buffer("core-a", left->logical_frame, left->state_a, left->state_a_size,
                       right->state_a, right->state_a_size) != 0 ||
        compare_buffer("core-b", left->logical_frame, left->state_b, left->state_b_size,
                       right->state_b, right->state_b_size) != 0 ||
        compare_buffer("battery-a", left->logical_frame, left->battery_a, left->battery_a_size,
                       right->battery_a, right->battery_a_size) != 0 ||
        compare_buffer("battery-b", left->logical_frame, left->battery_b, left->battery_b_size,
                       right->battery_b, right->battery_b_size) != 0 ||
        memcmp(left->serial_sha256, right->serial_sha256, 32) != 0 ||
        memcmp(left->ir_sha256, right->ir_sha256, 32) != 0 ||
        memcmp(left->input_sha256, right->input_sha256, 32) != 0) {
        return -1;
    }
    return 0;
}

static void print_receipt(unsigned repetition, const IntegralGBRuntimeLinkSnapshot *snapshot)
{
    char a[65], b[65], pair[65], battery_a[65], battery_b[65];
    char input[65], serial[65], ir[65];
    integral_gb_runtime_content_sha256_hex(snapshot->state_a_sha256, a);
    integral_gb_runtime_content_sha256_hex(snapshot->state_b_sha256, b);
    integral_gb_runtime_content_sha256_hex(snapshot->pair_sha256, pair);
    integral_gb_runtime_content_sha256_hex(snapshot->battery_a_sha256, battery_a);
    integral_gb_runtime_content_sha256_hex(snapshot->battery_b_sha256, battery_b);
    integral_gb_runtime_content_sha256_hex(snapshot->input_sha256, input);
    integral_gb_runtime_content_sha256_hex(snapshot->serial_sha256, serial);
    integral_gb_runtime_content_sha256_hex(snapshot->ir_sha256, ir);
    printf("{\"schema\":\"ilp-state-receipt-v2\",\"determinism_abi\":\"%s\","
           "\"repetition\":%u,\"logical_frame\":%llu,\"core_a_state_sha256\":\"%s\","
           "\"core_b_state_sha256\":\"%s\",\"pair_sha256\":\"%s\","
           "\"battery_a_size\":%zu,\"battery_a_sha256\":\"%s\","
           "\"battery_b_size\":%zu,\"battery_b_sha256\":\"%s\","
           "\"input_transcript_sha256\":\"%s\",\"serial_event_sha256\":\"%s\","
           "\"ir_event_sha256\":\"%s\","
           "\"serial_event_count\":%llu,\"ir_event_count\":%llu,"
           "\"matched\":true}\n",
           INTEGRAL_GB_RUNTIME_LINK_ABI_ID, repetition,
           (unsigned long long)snapshot->logical_frame, a, b, pair,
           snapshot->battery_a_size, battery_a,
           snapshot->battery_b_size, battery_b,
           input, serial, ir,
           (unsigned long long)snapshot->serial_events,
           (unsigned long long)snapshot->ir_events);
}

static void hash_u64_be(IntegralGBRuntimeContentSha256 *state, uint64_t value)
{
    uint8_t encoded[8];
    for (unsigned i = 0; i < sizeof(encoded); i++) {
        encoded[sizeof(encoded) - i - 1] = (uint8_t)(value >> (i * 8));
    }
    integral_gb_runtime_content_sha256_update(state, encoded, sizeof(encoded));
}

static void comparison_digest(const IntegralGBRuntimeLinkSnapshot *snapshot, uint8_t digest[32])
{
    IntegralGBRuntimeContentSha256 state;
    integral_gb_runtime_content_sha256_init(&state);
    integral_gb_runtime_content_sha256_update(&state, snapshot->pair_sha256, 32);
    integral_gb_runtime_content_sha256_update(&state, snapshot->battery_a_sha256, 32);
    integral_gb_runtime_content_sha256_update(&state, snapshot->battery_b_sha256, 32);
    integral_gb_runtime_content_sha256_update(&state, snapshot->serial_sha256, 32);
    integral_gb_runtime_content_sha256_update(&state, snapshot->ir_sha256, 32);
    integral_gb_runtime_content_sha256_update(&state, snapshot->input_sha256, 32);
    hash_u64_be(&state, snapshot->serial_events);
    hash_u64_be(&state, snapshot->ir_events);
    integral_gb_runtime_content_sha256_finish(&state, digest);
}

static int verify_checkpoint(IntegralGBRuntimeLinkEngine *first,
                             IntegralGBRuntimeLinkEngine *second,
                             unsigned repetition,
                             size_t checkpoint_index,
                             uint8_t *baseline)
{
    IntegralGBRuntimeLinkSnapshot left, right;
    memset(&left, 0, sizeof(left));
    memset(&right, 0, sizeof(right));
    int result = 0;
    if (integral_gb_runtime_link_engine_snapshot(first, &left) != 0 ||
        integral_gb_runtime_link_engine_snapshot(second, &right) != 0) {
        result = -1;
    }
    else if (compare_snapshot(&left, &right) != 0) {
        result = -1;
    }
    else {
        uint8_t digest[32];
        comparison_digest(&left, digest);
        if (repetition == 1) {
            memcpy(baseline + checkpoint_index * 32, digest, 32);
        }
        else if (memcmp(baseline + checkpoint_index * 32, digest, 32) != 0) {
            fprintf(stderr, "DESYNC repetition=%u checkpoint=%zu logical_frame=%llu\n",
                    repetition, checkpoint_index,
                    (unsigned long long)left.logical_frame);
            result = -1;
        }
        if (result == 0) print_receipt(repetition, &left);
    }
    integral_gb_runtime_link_snapshot_free(&left);
    integral_gb_runtime_link_snapshot_free(&right);
    return result;
}

int main(int argc, char **argv)
{
    const char *rom_a = NULL, *rom_b = NULL, *save_a_path = NULL, *save_b_path = NULL;
    const char *trace_path = NULL, *checkpoint_list = NULL;
    const char *screenshot_a = NULL, *screenshot_b = NULL;
    const char *initial_state_prefix = NULL;
    bool cross_render = true;
    bool require_serial_events = false, require_ir_events = false;
    unsigned require_rtc_day = 0;
    unsigned rtc_offset_minutes = 0;
    unsigned frames = 120, checkpoint = 60, repetitions = 1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--require-serial-events")) {
            require_serial_events = true;
            continue;
        }
        if (!strcmp(argv[i], "--require-ir-events")) {
            require_ir_events = true;
            continue;
        }
        if (i + 1 >= argc) { usage(argv[0]); return 2; }
        const char *value = argv[++i];
        if (!strcmp(argv[i - 1], "--rom-a")) rom_a = value;
        else if (!strcmp(argv[i - 1], "--rom-b")) rom_b = value;
        else if (!strcmp(argv[i - 1], "--save-a")) save_a_path = value;
        else if (!strcmp(argv[i - 1], "--save-b")) save_b_path = value;
        else if (!strcmp(argv[i - 1], "--input-trace")) trace_path = value;
        else if (!strcmp(argv[i - 1], "--frames")) { if (parse_unsigned(value, &frames)) return 2; }
        else if (!strcmp(argv[i - 1], "--checkpoint")) { if (parse_unsigned(value, &checkpoint)) return 2; }
        else if (!strcmp(argv[i - 1], "--checkpoint-list")) checkpoint_list = value;
        else if (!strcmp(argv[i - 1], "--screenshot-a")) screenshot_a = value;
        else if (!strcmp(argv[i - 1], "--screenshot-b")) screenshot_b = value;
        else if (!strcmp(argv[i - 1], "--initial-state-prefix")) {
            initial_state_prefix = value;
        }
        else if (!strcmp(argv[i - 1], "--require-rtc-day-at-least")) {
            if (parse_unsigned(value, &require_rtc_day)) return 2;
        }
        else if (!strcmp(argv[i - 1], "--rtc-offset-minutes")) {
            if (parse_unsigned(value, &rtc_offset_minutes)) return 2;
        }
        else if (!strcmp(argv[i - 1], "--repetitions")) { if (parse_unsigned(value, &repetitions)) return 2; }
        else if (!strcmp(argv[i - 1], "--render-profile")) {
            if (!strcmp(value, "same")) cross_render = false;
            else if (!strcmp(value, "cross")) cross_render = true;
            else return 2;
        }
        else { usage(argv[0]); return 2; }
    }
    if (!rom_a || !rom_b) { usage(argv[0]); return 2; }
    if ((screenshot_a == NULL) != (screenshot_b == NULL)) {
        fprintf(stderr, "Both --screenshot-a and --screenshot-b are required together\n");
        return 2;
    }
    uint8_t test_digest[32];
    char test_hex[65];
    integral_gb_runtime_content_sha256("abc", 3, test_digest);
    integral_gb_runtime_content_sha256_hex(test_digest, test_hex);
    if (strcmp(test_hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        fprintf(stderr, "SHA-256 self-test failed\n");
        return 1;
    }
    uint8_t *save_a = NULL, *save_b = NULL;
    size_t save_a_size = 0, save_b_size = 0;
    if (read_file(save_a_path, &save_a, &save_a_size) != 0 ||
        read_file(save_b_path, &save_b, &save_b_size) != 0) {
        fprintf(stderr, "Failed to read SAV input\n");
        return 1;
    }
    InputTrace trace;
    if (load_trace(trace_path, frames, &trace) != 0) return 1;
    CheckpointSchedule schedule;
    if (build_checkpoint_schedule(checkpoint_list, checkpoint, frames, &schedule) != 0) {
        free_trace(&trace);
        return 1;
    }
    size_t checkpoint_count = schedule.count + 1; /* frame 0 is always canonical. */
    uint8_t *baseline = calloc(checkpoint_count, 32);
    if (!baseline) return 1;
    int result = 0;
    for (unsigned repetition = 1; repetition <= repetitions && result == 0; repetition++) {
        IntegralGBRuntimeLinkEngine first, second;
        memset(&first, 0, sizeof(first));
        memset(&second, 0, sizeof(second));
        IntegralGBRuntimeLinkEngineConfig first_config = {
            .rom_a = rom_a, .rom_b = rom_b, .save_a = save_a, .save_a_size = save_a_size,
            .save_b = save_b, .save_b_size = save_b_size,
            .input_a = trace.a, .input_b = trace.b, .input_frames = trace.frames,
            .rtc_offset_seconds = (uint64_t)rtc_offset_minutes * 60u,
            .display_role = 0,
        };
        IntegralGBRuntimeLinkEngineConfig second_config = first_config;
        second_config.display_role = cross_render ? 1 : 0;
        if (integral_gb_runtime_link_engine_init(&first, &first_config) != 0 ||
            integral_gb_runtime_link_engine_init(&second, &second_config) != 0) {
            fprintf(stderr, "GB link engine initialization failed\n");
            integral_gb_runtime_link_engine_free(&first);
            integral_gb_runtime_link_engine_free(&second);
            result = 1;
            break;
        }
        size_t checkpoint_index = 0;
        size_t schedule_index = 0;
        if (initial_state_prefix && repetition == 1 &&
            write_initial_state_pair(initial_state_prefix, &first) != 0) {
            fprintf(stderr, "Failed to write initial GB link state buffers\n");
            result = 1;
        }
        if (verify_checkpoint(&first, &second, repetition, checkpoint_index++, baseline) != 0) {
            result = 1;
        }
        for (unsigned frame = 0; frame < frames && result == 0; frame++) {
            if (integral_gb_runtime_link_engine_run_frame(&first, trace.a[frame], trace.b[frame]) != 0 ||
                integral_gb_runtime_link_engine_run_frame(&second, trace.a[frame], trace.b[frame]) != 0) {
                result = 1;
                break;
            }
            if (schedule_index < schedule.count &&
                frame + 1 == schedule.frames[schedule_index]) {
                if (verify_checkpoint(&first, &second, repetition,
                                      checkpoint_index++, baseline) != 0) {
                    result = 1;
                }
                schedule_index++;
            }
        }
        if (result == 0 && require_serial_events &&
            (first.serial_events == 0 || second.serial_events == 0)) {
            fprintf(stderr, "Required serial transcript is empty at frame %u\n", frames);
            result = 1;
        }
        if (result == 0 && require_ir_events &&
            (first.ir_events == 0 || second.ir_events == 0)) {
            fprintf(stderr, "Required IR transcript is empty at frame %u\n", frames);
            result = 1;
        }
        if (result == 0 && require_rtc_day > 0) {
            IntegralGBRuntimeLinkRTCRegisters rtc[4];
            if (integral_gb_runtime_link_engine_get_rtc(&first, 0, &rtc[0]) != 0 ||
                integral_gb_runtime_link_engine_get_rtc(&first, 1, &rtc[1]) != 0 ||
                integral_gb_runtime_link_engine_get_rtc(&second, 0, &rtc[2]) != 0 ||
                integral_gb_runtime_link_engine_get_rtc(&second, 1, &rtc[3]) != 0) {
                fprintf(stderr, "Required RTC registers are unavailable\n");
                result = 1;
            }
            else {
                for (size_t rtc_index = 0; rtc_index < 4; rtc_index++) {
                    if (rtc[rtc_index].days < require_rtc_day) {
                        fprintf(stderr,
                                "RTC boundary not reached pair_role=%zu day=%u required=%u "
                                "time=%02u:%02u:%02u halted=%s overflowed=%s\n",
                                rtc_index, rtc[rtc_index].days, require_rtc_day,
                                rtc[rtc_index].hours, rtc[rtc_index].minutes,
                                rtc[rtc_index].seconds,
                                rtc[rtc_index].halted ? "yes" : "no",
                                rtc[rtc_index].overflowed ? "yes" : "no");
                        result = 1;
                        break;
                    }
                }
            }
        }
        if (result == 0 && screenshot_a && repetition == repetitions) {
            if (write_slot_bmp(screenshot_a, &first.a) != 0 ||
                write_slot_bmp(screenshot_b, &first.b) != 0) {
                fprintf(stderr, "Failed to write GB link final screenshots\n");
                result = 1;
            }
            else {
                printf("SCREENSHOTS role_a=%s role_b=%s\n", screenshot_a, screenshot_b);
            }
        }
        integral_gb_runtime_link_engine_free(&first);
        integral_gb_runtime_link_engine_free(&second);
    }
    if (save_a) { memset(save_a, 0, save_a_size); free(save_a); }
    if (save_b) { memset(save_b, 0, save_b_size); free(save_b); }
    free_trace(&trace);
    free_checkpoint_schedule(&schedule);
    free(baseline);
    if (result == 0) printf("PASS GB link same-process deterministic comparison\n");
    return result;
}

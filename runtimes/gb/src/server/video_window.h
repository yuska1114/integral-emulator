/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_H
#define INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_H

#include <stdbool.h>

#include "input_router.h"
#include "slot.h"

typedef struct IntegralGBRuntimeVideoWindow IntegralGBRuntimeVideoWindow;

typedef enum IntegralGBRuntimeVideoWindowPollResult {
    INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CONTINUE,
    INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_CLOSE,
    INTEGRAL_GB_RUNTIME_VIDEO_WINDOW_RETURN_MENU,
} IntegralGBRuntimeVideoWindowPollResult;

int integral_gb_runtime_video_window_open(IntegralGBRuntimeVideoWindow **window_out, unsigned scale);
int integral_gb_runtime_video_window_open_slots(IntegralGBRuntimeVideoWindow **window_out, unsigned scale, unsigned slot_count);
int integral_gb_runtime_video_window_open_titled(IntegralGBRuntimeVideoWindow **window_out,
                                        unsigned scale,
                                        unsigned slot_count,
                                        const char *title);
int integral_gb_runtime_video_window_open_titled_sized(
    IntegralGBRuntimeVideoWindow **window_out,
    unsigned scale,
    unsigned slot_count,
    const char *title,
    unsigned window_width,
    unsigned window_height);
int integral_gb_runtime_video_window_open_titled_unthrottled(
    IntegralGBRuntimeVideoWindow **window_out,
    unsigned scale,
    unsigned slot_count,
    const char *title);
int integral_gb_runtime_video_window_open_titled_unthrottled_sized(
    IntegralGBRuntimeVideoWindow **window_out,
    unsigned scale,
    unsigned slot_count,
    const char *title,
    unsigned window_width,
    unsigned window_height);
IntegralGBRuntimeVideoWindowPollResult integral_gb_runtime_video_window_poll(IntegralGBRuntimeVideoWindow *window,
                                                           IntegralGBRuntimeInputRouter *input_router,
                                                           const IntegralGBRuntimeSlot *slot1,
                                                           const IntegralGBRuntimeSlot *slot2);
bool integral_gb_runtime_video_window_is_active(const IntegralGBRuntimeVideoWindow *window);
void integral_gb_runtime_video_window_set_status_message(IntegralGBRuntimeVideoWindow *window, const char *message);
void integral_gb_runtime_video_window_show_message(IntegralGBRuntimeVideoWindow *window, const char *message);
void integral_gb_runtime_video_window_show_speed_message(IntegralGBRuntimeVideoWindow *window, const char *message);
int integral_gb_runtime_video_window_render(IntegralGBRuntimeVideoWindow *window,
                                   const IntegralGBRuntimeSlot *slot1,
                                   const IntegralGBRuntimeSlot *slot2);
int integral_gb_runtime_video_window_render_pixels(IntegralGBRuntimeVideoWindow *window,
                                           const uint32_t *pixels);
void integral_gb_runtime_video_window_close(IntegralGBRuntimeVideoWindow *window);

#endif

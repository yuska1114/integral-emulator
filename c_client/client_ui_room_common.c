/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_ui_room_common.h"

#include "client_ui_common.h"
#include "sdl_text.h"
#include "sdl_unicode_text.h"

void integral_client_ui_draw_room_chat_messages(SDL_Renderer *renderer,
                                                const IntegralRoomCommonState *state,
                                                int x,
                                                int y,
                                                int width)
{
    const IntegralClientUiTheme *theme = integral_client_ui_theme();
    unsigned message_count = chat_message_count(
        (char (*)[INTEGRAL_CHAT_MESSAGE_MAX])state->room_chat_log);
    unsigned start = message_count > INTEGRAL_CHAT_VISIBLE_LINES
                         ? message_count - INTEGRAL_CHAT_VISIBLE_LINES
                         : 0;
    if (state->room_chat_scroll > 0 && message_count > INTEGRAL_CHAT_VISIBLE_LINES) {
        unsigned max_scroll = message_count - INTEGRAL_CHAT_VISIBLE_LINES;
        unsigned offset = state->room_chat_scroll > max_scroll
                              ? max_scroll
                              : state->room_chat_scroll;
        start = message_count - INTEGRAL_CHAT_VISIBLE_LINES - offset;
    }

    unsigned seen = 0;
    unsigned drawn = 0;
    for (unsigned i = 0; i < INTEGRAL_CHAT_LOG_LINES && drawn < INTEGRAL_CHAT_VISIBLE_LINES; i++) {
        if (state->room_chat_log[i][0] == '\0') continue;
        if (seen++ < start) continue;
        integral_sdl_draw_utf8_text(renderer,
                                    x,
                                    y + (int)drawn * 18,
                                    state->room_chat_log[i],
                                    14,
                                    theme->value,
                                    width);
        drawn++;
    }
    if (message_count == 0) {
        integral_sdl_draw_text(renderer, x, y + 34, "NO MESSAGES", 1, theme->muted);
    }
}

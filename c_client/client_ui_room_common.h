/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_UI_ROOM_COMMON_H
#define INTEGRAL_CLIENT_UI_ROOM_COMMON_H

#include "client_room_common.h"

void integral_client_ui_draw_room_chat_messages(SDL_Renderer *renderer,
                                                const IntegralRoomCommonState *state,
                                                int x,
                                                int y,
                                                int width);

#endif

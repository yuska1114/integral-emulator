/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_UI_ACCOUNT_H
#define INTEGRAL_CLIENT_UI_ACCOUNT_H

#include <SDL.h>
#include <stdbool.h>

#include "client_account.h"

typedef struct IntegralClientPasswordChangeView {
    const char *username;
    const char *server;
    const char *version;
    const char *new_display;
    const char *confirm_display;
    const char *status;
    PasswordChangeField selected;
    bool editing;
} IntegralClientPasswordChangeView;

typedef struct IntegralClientLoginView {
    const char *server;
    const char *server_label;
    const char *username;
    const char *password_display;
    const char *status;
    const char *version;
    LoginField selected;
    bool editing;
    bool remember_login;
} IntegralClientLoginView;

void integral_client_ui_draw_login(SDL_Renderer *renderer, const IntegralClientLoginView *view);
void integral_client_ui_draw_password_change(SDL_Renderer *renderer,
                                             const IntegralClientPasswordChangeView *view);

#endif

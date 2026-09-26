/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_INPUT_ALIAS_H
#define INTEGRAL_CLIENT_INPUT_ALIAS_H

#include "client_key_config.h"

bool integral_client_alias_keyboard_event(const IntegralConfigKeys *keys,
                                          const SDL_KeyboardEvent *event,
                                          SDL_KeyboardEvent *translated);
bool integral_client_alias_controller_event(const IntegralConfigKeys *keys,
                                            bool held[INTEGRAL_CLIENT_ALIAS_KEYS],
                                            const SDL_Event *event,
                                            SDL_KeyboardEvent *translated);
bool integral_client_alias_has_controller_binding(const IntegralConfigKeys *keys);
bool integral_client_alias_key_reserved(SDL_Keycode key);
void integral_client_alias_reset_state(bool held[INTEGRAL_CLIENT_ALIAS_KEYS]);

#endif

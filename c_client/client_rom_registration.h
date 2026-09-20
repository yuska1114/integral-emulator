/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ROM_REGISTRATION_H
#define INTEGRAL_CLIENT_ROM_REGISTRATION_H
#include "client_rom_editor.h"
#include "http_client.h"

typedef struct IntegralRomRegistration {
    IntegralConfigRomSlot *rom_slots;
    IntegralApiRomSlot *server_rom_slots;
    IntegralClientRomEditor *editor;
    const char *server;
    const char *token;
    const char *server_id;
    const char *config_path;
    bool allow_user_initial_save_import;
    char *status;
    size_t status_size;
    void (*log)(void *context, const char *event, const char *detail);
    void *log_context;
} IntegralRomRegistration;
bool integral_rom_registration_refresh(IntegralRomRegistration *);
void integral_rom_registration_enter(IntegralRomRegistration *);
void integral_rom_registration_leave(IntegralRomRegistration *);
void integral_rom_registration_apply(IntegralRomRegistration *, bool confirm_delete_saves,
                                     bool confirm_initial_save_import);
#endif

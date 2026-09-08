/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CREDENTIAL_STORE_H
#define INTEGRAL_CREDENTIAL_STORE_H

#include <stddef.h>

int integral_credential_store_load(const char *server,
                                   const char *username,
                                   char *password_out,
                                   size_t password_out_size);
int integral_credential_store_save(const char *server,
                                   const char *username,
                                   const char *password);
int integral_credential_store_delete(const char *server, const char *username);

#endif

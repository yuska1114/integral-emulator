/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_MDNS_H
#define INTEGRAL_GB_RUNTIME_MDNS_H

#include <stddef.h>

#define INTEGRAL_GB_RUNTIME_MDNS_SERVICE "_gb_runtime._tcp.local"
#define INTEGRAL_GB_RUNTIME_MDNS_INSTANCE "GB Runtime._gb_runtime._tcp.local"

int integral_gb_runtime_mdns_advertise_once(unsigned port);
int integral_gb_runtime_mdns_discover(char *host_out, size_t host_out_size, unsigned *port_out, unsigned timeout_ms);
int integral_gb_runtime_mdns_advertise_named_once(const char *service,
                                         const char *instance,
                                         const char *target,
                                         unsigned port);
int integral_gb_runtime_mdns_discover_named(const char *service,
                                  char *host_out,
                                  size_t host_out_size,
                                  unsigned *port_out,
                                  unsigned timeout_ms);

#endif

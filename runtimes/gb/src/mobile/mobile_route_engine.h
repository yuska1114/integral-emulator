/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_MOBILE_ROUTE_ENGINE_H
#define INTEGRAL_GB_RUNTIME_MOBILE_ROUTE_ENGINE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mobile.h"
#include "mobile_session_manifest.h"

typedef struct IntegralGBRuntimeMobileRouteEngine IntegralGBRuntimeMobileRouteEngine;
typedef struct IntegralGBRuntimeMobileRouteStats {
    unsigned dns_requests;
    unsigned http_requests;
    unsigned matched_responses;
    unsigned unauthorized_responses;
    unsigned body_requests;
    uint64_t body_bytes;
    unsigned replay_responses;
} IntegralGBRuntimeMobileRouteStats;

IntegralGBRuntimeMobileRouteEngine *integral_gb_runtime_mobile_route_engine_new(
    const IntegralGBRuntimeMobileSessionManifest *manifest,
    char *error_out,
    size_t error_out_size);
void integral_gb_runtime_mobile_route_engine_free(IntegralGBRuntimeMobileRouteEngine *engine);
bool integral_gb_runtime_mobile_route_sock_open(IntegralGBRuntimeMobileRouteEngine *engine,
                                        unsigned conn,
                                        enum mobile_socktype type,
                                        enum mobile_addrtype addrtype,
                                        unsigned bindport);
void integral_gb_runtime_mobile_route_sock_close(IntegralGBRuntimeMobileRouteEngine *engine,
                                         unsigned conn);
int integral_gb_runtime_mobile_route_sock_connect(IntegralGBRuntimeMobileRouteEngine *engine,
                                          unsigned conn,
                                          const struct mobile_addr *addr);
int integral_gb_runtime_mobile_route_sock_send(IntegralGBRuntimeMobileRouteEngine *engine,
                                       unsigned conn,
                                       const void *data,
                                       unsigned size,
                                       const struct mobile_addr *addr);
int integral_gb_runtime_mobile_route_sock_recv(IntegralGBRuntimeMobileRouteEngine *engine,
                                       unsigned conn,
                                       void *data,
                                       unsigned size,
                                       struct mobile_addr *addr);
unsigned integral_gb_runtime_mobile_route_error_count(const IntegralGBRuntimeMobileRouteEngine *engine);
void integral_gb_runtime_mobile_route_stats(const IntegralGBRuntimeMobileRouteEngine *engine,
                                    IntegralGBRuntimeMobileRouteStats *stats);

#endif

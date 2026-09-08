/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_GB_RUNTIME_MOBILE_SESSION_MANIFEST_H
#define INTEGRAL_GB_RUNTIME_MOBILE_SESSION_MANIFEST_H

#include <stddef.h>

#define INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_MAX_ARTIFACTS 32u
#define INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_PATH_MAX 1024u

typedef struct IntegralGBRuntimeMobileManifestArtifact {
    char role[96];
    char content_id[96];
    char path[INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_PATH_MAX];
    char sha256[65];
    size_t size;
} IntegralGBRuntimeMobileManifestArtifact;

typedef struct IntegralGBRuntimeMobileSessionManifest {
    char adapter_id[96];
    char package_id[96];
    char release_id[96];
    char package_digest[65];
    unsigned runtime_capability_version;
    IntegralGBRuntimeMobileManifestArtifact artifacts[INTEGRAL_GB_RUNTIME_MOBILE_MANIFEST_MAX_ARTIFACTS];
    size_t artifact_count;
} IntegralGBRuntimeMobileSessionManifest;

int integral_gb_runtime_mobile_session_manifest_load(const char *manifest_path,
                                             IntegralGBRuntimeMobileSessionManifest *manifest,
                                             char *error_out,
                                             size_t error_out_size);

#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_MOBILE_SESSION_CONTRACT_H
#define INTEGRAL_MOBILE_SESSION_CONTRACT_H

#include <stddef.h>

#define INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACTS 32u
#define INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACT_SIZE (16u * 1024u * 1024u)
#define INTEGRAL_MOBILE_CONTRACT_MAX_TOTAL_SIZE (32u * 1024u * 1024u)

typedef struct IntegralMobileArtifact {
    char role[96];
    char content_id[96];
    char sha256[65];
    unsigned char *data;
    size_t size;
} IntegralMobileArtifact;

typedef struct IntegralMobileRuntimeContract {
    int schema_version;
    char adapter_id[96];
    char package_id[96];
    char release_id[96];
    char package_digest[65];
    int runtime_capability_version;
    IntegralMobileArtifact artifacts[INTEGRAL_MOBILE_CONTRACT_MAX_ARTIFACTS];
    size_t artifact_count;
} IntegralMobileRuntimeContract;

void integral_mobile_runtime_contract_init(IntegralMobileRuntimeContract *contract);
void integral_mobile_runtime_contract_free(IntegralMobileRuntimeContract *contract);
int integral_mobile_runtime_contract_parse(const char *json,
                                           IntegralMobileRuntimeContract *contract,
                                           char *error_out,
                                           size_t error_out_size);
int integral_mobile_runtime_contract_write_manifest(
    const IntegralMobileRuntimeContract *contract,
    const char *session_directory,
    const char *manifest_path,
    char *error_out,
    size_t error_out_size);

#endif

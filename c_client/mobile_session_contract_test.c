/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "mobile_session_contract.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef _WIN32
static int private_manifest_files(const IntegralMobileRuntimeContract *contract)
{
    char directory[256];
    char manifest_path[320];
    char artifact_path[320];
    snprintf(directory, sizeof(directory), "/tmp/integral-mobile-contract-%ld", (long)getpid());
    snprintf(manifest_path, sizeof(manifest_path), "%s/session.manifest", directory);
    snprintf(artifact_path, sizeof(artifact_path), "%s/artifact-00.bin", directory);
    (void)remove(manifest_path);
    (void)remove(artifact_path);
    (void)rmdir(directory);
    if (mkdir(directory, 0700) != 0) return 0;

    FILE *permissive = fopen(manifest_path, "wb");
    if (!permissive) return 0;
    fclose(permissive);
    if (chmod(manifest_path, 0644) != 0) return 0;

    char error[128];
    if (integral_mobile_runtime_contract_write_manifest(
            contract, directory, manifest_path, error, sizeof(error)) != 0) {
        fprintf(stderr, "manifest write rejected: %s\n", error);
        return 0;
    }
    struct stat manifest_status;
    struct stat artifact_status;
    int ok = stat(manifest_path, &manifest_status) == 0 &&
             stat(artifact_path, &artifact_status) == 0 &&
             (manifest_status.st_mode & 0777) == 0600 &&
             (artifact_status.st_mode & 0777) == 0600;
    (void)remove(manifest_path);
    (void)remove(artifact_path);
    (void)rmdir(directory);
    return ok;
}
#endif

static int rejected(const char *json)
{
    IntegralMobileRuntimeContract contract;
    char error[128];
    int result = integral_mobile_runtime_contract_parse(json, &contract, error, sizeof(error));
    integral_mobile_runtime_contract_free(&contract);
    const char *valid_v3 = "{\"runtime_contract\":{\"schema_version\":2,\"adapter_id\":\"gb_mobile_v2\",\"package_id\":\"synthetic_auth\",\"release_id\":\"2026-09-02.2\",\"package_digest\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"runtime_capability_version\":3,\"artifacts\":[{\"role\":\"route_payload\",\"content_id\":\"challenge_v1\",\"size\":3,\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"data\":\"YWJj\"}]}}";
    if (integral_mobile_runtime_contract_parse(valid_v3, &contract, error, sizeof(error)) != 0 ||
        contract.runtime_capability_version != 3) {
        fprintf(stderr, "valid capability-3 contract rejected: %s\n", error);
        return 1;
    }
    integral_mobile_runtime_contract_free(&contract);
    return result != 0;
}

int main(void)
{
    const char *valid = "{\"runtime_contract\":{\"schema_version\":2,\"adapter_id\":\"gb_mobile_v2\",\"package_id\":\"synthetic_numbers\",\"release_id\":\"2026-09-02.1\",\"package_digest\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"runtime_capability_version\":2,\"artifacts\":[{\"role\":\"number_payload\",\"content_id\":\"random_absent_number_v1\",\"size\":3,\"sha256\":\"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\",\"data\":\"YWJj\"}]}}";
    IntegralMobileRuntimeContract contract;
    char error[128];
    if (integral_mobile_runtime_contract_parse(valid, &contract, error, sizeof(error)) != 0 ||
        contract.artifact_count != 1u || strcmp(contract.package_id, "synthetic_numbers") != 0) {
        fprintf(stderr, "valid contract rejected: %s\n", error);
        return 1;
    }
#ifndef _WIN32
    if (!private_manifest_files(&contract)) {
        fprintf(stderr, "runtime manifest files are not private\n");
        integral_mobile_runtime_contract_free(&contract);
        return 1;
    }
#endif
    integral_mobile_runtime_contract_free(&contract);
    if (!rejected("{}") ||
        !rejected("{\"schema_version\":1,\"adapter_id\":\"gb_mobile_v1\",\"package_id\":\"x\",\"release_id\":\"x\",\"package_digest\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"runtime_capability_version\":2,\"artifacts\":[]}") ||
        !rejected("{\"schema_version\":2,\"adapter_id\":\"gb_mobile_v2\",\"package_id\":\"x\",\"release_id\":\"x\",\"package_digest\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"runtime_capability_version\":2,\"artifacts\":[{\"role\":\"../x\",\"content_id\":\"x\",\"size\":0,\"sha256\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"data\":\"\"}]}") ||
        !rejected("{\"schema_version\":2,\"adapter_id\":\"gb_mobile_v2\",\"package_id\":\"x\",\"release_id\":\"x\",\"package_digest\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"runtime_capability_version\":2,\"artifacts\":[{\"role\":\"a\",\"content_id\":\"x\",\"size\":0,\"sha256\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"data\":\"\"},{\"role\":\"a\",\"content_id\":\"y\",\"size\":0,\"sha256\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"data\":\"\"}]}") ||
        !rejected("{\"schema_version\":2,\"adapter_id\":\"gb_mobile_v2\",\"package_id\":\"x\",\"release_id\":\"x\",\"package_digest\":\"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\",\"runtime_capability_version\":2,\"artifacts\":[{\"role\":\"a\",\"content_id\":\"x\",\"size\":3,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\",\"data\":\"YWJj\"}]}") ) {
        fprintf(stderr, "malformed contract accepted\n");
        return 1;
    }
    puts("mobile session contract tests passed");
    return 0;
}

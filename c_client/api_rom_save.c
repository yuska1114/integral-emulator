/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "http_client.h"
#include "http_client_internal.h"
#include "http_payload_codec.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INTEGRAL_MAX_SAVE_BYTES (128u * 1024u + 48u)
#define INTEGRAL_SAVE_RESPONSE_MAX (220u * 1024u)

static int parse_rom_slots_response(const char *response,
                                    IntegralApiRomSlot *slots,
                                    size_t slot_count,
                                    char *error_out,
                                    size_t error_out_size)
{
    for (size_t i = 0; i < slot_count; i++) {
        memset(&slots[i], 0, sizeof(slots[i]));
        slots[i].slot = (unsigned)i + 1;
    }

    const char *p = strstr(response, "\"slots\"");
    if (!p) {
        http_set_error(error_out, error_out_size, "ROM SLOTS RESPONSE MISSING SLOTS");
        return -1;
    }
    p = strchr(p, '[');
    if (!p) {
        http_set_error(error_out, error_out_size, "ROM SLOTS RESPONSE INVALID");
        return -1;
    }

    while ((p = strchr(p, '{')) != NULL) {
        const char *object_end = strchr(p, '}');
        if (!object_end) {
            break;
        }
        int slot_number = 0;
        const char *slot_key = strstr(p, "\"slot\"");
        if (!slot_key || slot_key > object_end) {
            p = object_end + 1;
            continue;
        }
        const char *slot_colon = strchr(slot_key, ':');
        if (!slot_colon || slot_colon > object_end || sscanf(slot_colon + 1, "%d", &slot_number) != 1 ||
            slot_number < 1 || (size_t)slot_number > slot_count) {
            p = object_end + 1;
            continue;
        }

        IntegralApiRomSlot *slot = &slots[slot_number - 1];
        slot->slot = (unsigned)slot_number;
        const char *key = NULL;
        const char *colon = NULL;
        if ((key = strstr(p, "\"rom_id\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->rom_id, sizeof(slot->rom_id));
        }
        if ((key = strstr(p, "\"save_id\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->save_id, sizeof(slot->save_id));
        }
        if ((key = strstr(p, "\"filename\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->filename, sizeof(slot->filename));
        }
        if ((key = strstr(p, "\"game_type\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->game_type, sizeof(slot->game_type));
        }
        if ((key = strstr(p, "\"sha256\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->sha256, sizeof(slot->sha256));
        }
        if ((key = strstr(p, "\"platform\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->platform, sizeof(slot->platform));
        }
        if ((key = strstr(p, "\"rom_header_title\"")) && key < object_end && (colon = strchr(key, ':')) && colon < object_end) {
            (void)copy_json_string_token(colon + 1, slot->rom_header_title, sizeof(slot->rom_header_title));
        }
        p = object_end + 1;
    }
    return 0;
}

int integral_api_get_rom_slots(const char *server_url,
                          const char *token,
                          IntegralApiRomSlot *slots,
                          size_t slot_count,
                          char *error_out,
                          size_t error_out_size)
{
    char response[32768];
    if (api_get_json(server_url, "/rom-slots", token, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    return parse_rom_slots_response(response, slots, slot_count, error_out, error_out_size);
}

int integral_api_register_rom(const char *server_url,
                         const char *token,
                         const char *sha256,
                         const char *sha1,
                         const char *title,
                         const char *platform,
                         const char *region,
                         const char *rom_header_title,
                         char *rom_id_out,
                         size_t rom_id_out_size,
                         char *error_out,
                         size_t error_out_size)
{
    rom_id_out[0] = '\0';
    char escaped_title[160];
    char escaped_header_title[64];
    json_escape(title, escaped_title, sizeof(escaped_title));
    json_escape(rom_header_title, escaped_header_title, sizeof(escaped_header_title));
    char body[512];
    snprintf(body,
             sizeof(body),
             "{\"sha256\":\"%s\",\"sha1\":\"%s\",\"title\":\"%s\",\"platform\":\"%s\",\"region\":\"%s\",\"rom_header_title\":\"%s\"}",
             sha256,
             sha1,
             escaped_title,
             platform,
             region,
             escaped_header_title);
    char response[8192];
    if (api_post_json(server_url, "/roms", token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        return -1;
    }
    if (extract_json_string(response, "id", rom_id_out, rom_id_out_size) != 0) {
        http_set_error(error_out, error_out_size, "ROM RESPONSE MISSING ID");
        return -1;
    }
    return 0;
}

static int download_save_path(const char *server_url,
                              const char *token,
                              const char *path,
                              unsigned char *save_data_out,
                              size_t save_data_capacity,
                              size_t *save_data_size_out,
                              int *revision_out,
                              char *error_out,
                              size_t error_out_size)
{
    *save_data_size_out = 0;
    *revision_out = 0;

    char *response = malloc(INTEGRAL_SAVE_RESPONSE_MAX);
    if (!response) {
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    int rc = api_get_json(server_url, path, token, response, INTEGRAL_SAVE_RESPONSE_MAX, error_out, error_out_size);
    if (rc != 0) {
        free(response);
        return rc;
    }

    if (extract_json_int(response, "revision", revision_out) != 0) {
        free(response);
        http_set_error(error_out, error_out_size, "SAVE RESPONSE MISSING REVISION");
        return -2;
    }

    char *encoded = malloc(INTEGRAL_SAVE_RESPONSE_MAX);
    if (!encoded) {
        free(response);
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    if (extract_json_string_value(response, "save_data", encoded,
                                  INTEGRAL_SAVE_RESPONSE_MAX, true) != 0 ||
        base64_decode(encoded, save_data_out, save_data_capacity, save_data_size_out) != 0) {
        free(encoded);
        free(response);
        http_set_error(error_out, error_out_size, "SAVE DATA DECODE FAILED");
        return -2;
    }
    free(encoded);
    free(response);
    return 0;
}

int integral_api_download_save(const char *server_url,
                          const char *token,
                          const char *save_id,
                          unsigned char *save_data_out,
                          size_t save_data_capacity,
                          size_t *save_data_size_out,
                          int *revision_out,
                          char *error_out,
                          size_t error_out_size)
{
    char path[160];
    snprintf(path, sizeof(path), "/saves/%s", save_id);
    return download_save_path(server_url,
                              token,
                              path,
                              save_data_out,
                              save_data_capacity,
                              save_data_size_out,
                              revision_out,
                              error_out,
                              error_out_size);
}

int integral_api_download_n64_runtime_save(const char *server_url,
                                      const char *token,
                                      const char *media_session_id,
                                      const char *kind,
                                      unsigned char *save_data_out,
                                      size_t save_data_capacity,
                                      size_t *save_data_size_out,
                                      int *revision_out,
                                      char *error_out,
                                      size_t error_out_size)
{
    if (!media_session_id || !media_session_id[0] ||
        (!kind || (strcmp(kind, "n64") != 0 && strcmp(kind, "host-gb") != 0 &&
                   strcmp(kind, "remote-gb") != 0))) {
        http_set_error(error_out, error_out_size, "N64 RUNTIME SAVE REQUEST INVALID");
        return -1;
    }
    for (const unsigned char *cursor = (const unsigned char *)media_session_id;
         *cursor;
         cursor++) {
        if (!isalnum(*cursor) && *cursor != '-' && *cursor != '_') {
            http_set_error(error_out, error_out_size, "N64 MEDIA SESSION ID INVALID");
            return -1;
        }
    }
    char path[256];
    snprintf(path,
             sizeof(path),
             "/n64-runtime-media-sessions/%s/runtime-saves/%s",
             media_session_id,
             kind);
    return download_save_path(server_url,
                              token,
                              path,
                              save_data_out,
                              save_data_capacity,
                              save_data_size_out,
                              revision_out,
                              error_out,
                              error_out_size);
}

int integral_api_upload_save(const char *server_url,
                        const char *token,
                        const char *save_id,
                        int expected_revision,
                        const unsigned char *save_data,
                        size_t save_data_size,
                        int *revision_out,
                        char *error_out,
                        size_t error_out_size)
{
    return integral_api_upload_save_fenced(server_url,
                                      token,
                                      save_id,
                                      expected_revision,
                                      save_data,
                                      save_data_size,
                                      NULL,
                                      0,
                                      NULL,
                                      revision_out,
                                      error_out,
                                      error_out_size);
}

int integral_api_upload_save_with_request_id(const char *server_url,
                                        const char *token,
                                        const char *save_id,
                                        int expected_revision,
                                        const unsigned char *save_data,
                                        size_t save_data_size,
                                        const char *request_id,
                                        int *revision_out,
                                        char *error_out,
                                        size_t error_out_size)
{
    return integral_api_upload_save_fenced(server_url,
                                      token,
                                      save_id,
                                      expected_revision,
                                      save_data,
                                      save_data_size,
                                      NULL,
                                      0,
                                      request_id,
                                      revision_out,
                                      error_out,
                                      error_out_size);
}

int integral_api_upload_save_fenced(const char *server_url,
                               const char *token,
                               const char *save_id,
                               int expected_revision,
                               const unsigned char *save_data,
                               size_t save_data_size,
                               const char *game_session_id,
                               long long fencing_token,
                               const char *request_id,
                               int *revision_out,
                               char *error_out,
                               size_t error_out_size)
{
    *revision_out = 0;
    if (save_data_size > INTEGRAL_MAX_SAVE_BYTES) {
        http_set_error(error_out, error_out_size, "SAVE DATA TOO LARGE");
        return -1;
    }
    size_t encoded_size = ((save_data_size + 2) / 3) * 4 + 1;
    char *encoded = malloc(encoded_size);
    if (!encoded) {
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    base64_encode(save_data, save_data_size, encoded, encoded_size);

    size_t body_size = encoded_size + 512;
    char *body = malloc(body_size);
    if (!body) {
        free(encoded);
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    if (game_session_id && game_session_id[0] != '\0' && fencing_token > 0) {
        snprintf(body,
                 body_size,
                 "{\"expected_revision\":%d,\"save_data\":\"%s\",\"game_session_id\":\"%s\",\"fencing_token\":%lld,\"request_id\":\"%s\"}",
                 expected_revision,
                 encoded,
                 game_session_id,
                 fencing_token,
                 request_id ? request_id : "");
    }
    else {
        snprintf(body,
                 body_size,
                 "{\"expected_revision\":%d,\"save_data\":\"%s\",\"request_id\":\"%s\"}",
                 expected_revision,
                 encoded,
                 request_id ? request_id : "");
    }
    free(encoded);

    char path[160];
    snprintf(path, sizeof(path), "/saves/%s", save_id);
    char response[8192];
    int rc = api_put_json(server_url, path, token, body, response, sizeof(response), error_out, error_out_size);
    free(body);
    if (rc != 0) {
        return rc;
    }
    size_t response_body_size = 0;
    const char *response_body = http_body_start(response, strlen(response), &response_body_size);
    while (response_body_size && isspace((unsigned char)response_body[response_body_size - 1]))
        response_body_size--;
    if (!response_body || response_body_size < 2 || response_body[0] != '{' ||
        response_body[response_body_size - 1] != '}') {
        http_set_error(error_out, error_out_size, "SAVE UPLOAD RESPONSE INCOMPLETE");
        return -1;
    }
    if (extract_json_int(response, "revision", revision_out) != 0) {
        http_set_error(error_out, error_out_size, "SAVE UPLOAD RESPONSE MISSING REVISION");
        return -1;
    }
    return 0;
}

int integral_api_apply_rom_slot(const char *server_url,
                           const char *token,
                           unsigned slot,
                           const char *filename,
                           const char *sha256,
                           const char *sha1,
                           const char *platform,
                           const char *region,
                           const char *rom_header_title,
                           const unsigned char *initial_save_data,
                           size_t initial_save_data_size,
                           int initial_save_generated,
                           int confirm_delete_saves,
                           char *rom_id_out,
                           size_t rom_id_out_size,
                           char *save_id_out,
                           size_t save_id_out_size,
                           int *requires_confirmation_out,
                           char *error_out,
                           size_t error_out_size)
{
    rom_id_out[0] = '\0';
    save_id_out[0] = '\0';
    *requires_confirmation_out = 0;
    char escaped_filename[256];
    json_escape(filename, escaped_filename, sizeof(escaped_filename));
    char escaped_header_title[64];
    json_escape(rom_header_title ? rom_header_title : "", escaped_header_title, sizeof(escaped_header_title));
    char *initial_save_encoded = NULL;
    size_t initial_save_encoded_size = 0;
    if ((initial_save_data && initial_save_data_size > 0) || initial_save_generated) {
        if (initial_save_data_size > INTEGRAL_MAX_SAVE_BYTES) {
            http_set_error(error_out, error_out_size, "INITIAL SAVE DATA TOO LARGE");
            return -1;
        }
        initial_save_encoded_size = ((initial_save_data_size + 2) / 3) * 4 + 1;
        initial_save_encoded = malloc(initial_save_encoded_size);
        if (!initial_save_encoded) {
            http_set_error(error_out, error_out_size, "OUT OF MEMORY");
            return -1;
        }
        base64_encode(initial_save_data, initial_save_data_size, initial_save_encoded, initial_save_encoded_size);
    }
    size_t body_size = 768 + (initial_save_encoded ? initial_save_encoded_size + 32 : 0);
    char *body = malloc(body_size);
    if (!body) {
        free(initial_save_encoded);
        http_set_error(error_out, error_out_size, "OUT OF MEMORY");
        return -1;
    }
    if (initial_save_encoded) {
        const char *initial_save_key =
            initial_save_generated ? "generated_initial_save_data" : "initial_save_data";
        snprintf(body,
                 body_size,
                 "{\"confirm_delete_saves\":%s,\"slots\":[{\"slot\":%u,\"filename\":\"%s\",\"sha256\":\"%s\",\"sha1\":\"%s\",\"platform\":\"%s\",\"region\":\"%s\",\"rom_header_title\":\"%s\",\"%s\":\"%s\"}]}",
                 confirm_delete_saves ? "true" : "false",
                 slot,
                 escaped_filename,
                 sha256,
                 sha1,
                 platform,
                 region,
                 escaped_header_title,
                 initial_save_key,
                 initial_save_encoded);
    }
    else {
        snprintf(body,
                 body_size,
                 "{\"confirm_delete_saves\":%s,\"slots\":[{\"slot\":%u,\"filename\":\"%s\",\"sha256\":\"%s\",\"sha1\":\"%s\",\"platform\":\"%s\",\"region\":\"%s\",\"rom_header_title\":\"%s\"}]}",
                 confirm_delete_saves ? "true" : "false",
                 slot,
                 escaped_filename,
                 sha256,
                 sha1,
                 platform,
                 region,
                 escaped_header_title);
    }
    free(initial_save_encoded);
    char response[8192];
    if (api_post_json(server_url, "/rom-slots/apply", token, body, response, sizeof(response), error_out, error_out_size) != 0) {
        free(body);
        return -1;
    }
    free(body);
    (void)extract_json_bool(response, "requires_confirmation", requires_confirmation_out);
    if (*requires_confirmation_out) {
        return 0;
    }
    IntegralApiRomSlot response_slots[8];
    if (slot == 0u || slot > sizeof(response_slots) / sizeof(response_slots[0]) ||
        parse_rom_slots_response(response, response_slots,
                                 sizeof(response_slots) / sizeof(response_slots[0]),
                                 error_out, error_out_size) != 0 ||
        response_slots[slot - 1u].rom_id[0] == '\0' ||
        response_slots[slot - 1u].save_id[0] == '\0') {
        http_set_error(error_out, error_out_size, "ROM SLOT APPLY RESPONSE MISSING IDS");
        return -1;
    }
    http_copy_text(rom_id_out, rom_id_out_size, response_slots[slot - 1u].rom_id);
    http_copy_text(save_id_out, save_id_out_size, response_slots[slot - 1u].save_id);
    return 0;
}

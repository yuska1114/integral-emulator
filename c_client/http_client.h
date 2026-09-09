/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_LEAGUE_HTTP_CLIENT_H
#define INTEGRAL_LEAGUE_HTTP_CLIENT_H

#include <stddef.h>

#define INTEGRAL_EXECUTION_MODE_LOCAL_CLIENT "LOCAL_CLIENT"
#define INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT "N64_RUNTIME_CLIENT"

#include "mobile_session_contract.h"
#include <stdint.h>

#define INTEGRAL_API_CONTROL_JSON_MAX (32u * 1024u)


#define INTEGRAL_API_ROOMS 128
#define INTEGRAL_API_ROOM_USER_MAX 64
#define INTEGRAL_API_ROOM_SLOT_MAX 32
#define INTEGRAL_API_ROOM_FILENAME_MAX 256
#define INTEGRAL_API_ROOM_GAME_TYPE_MAX 65
#define INTEGRAL_API_ROM_HEADER_TITLE_MAX 21
#define INTEGRAL_API_ROM_PLATFORM_MAX 4
#define INTEGRAL_API_ROOM_SESSION_MAX 96
#define INTEGRAL_API_LINK_MODE_MAX 24
#define INTEGRAL_API_ROOM_CODE_MAX 6
#define INTEGRAL_API_ROOM_TYPE_MAX 16
#define INTEGRAL_API_ROOM_CHAT_MAX 16
#define INTEGRAL_API_ROOM_CHAT_TEXT_MAX 160
#define INTEGRAL_API_MOBILE_SCENARIOS_MAX 16
#define INTEGRAL_API_MOBILE_SCENARIO_ID_MAX 65
#define INTEGRAL_API_MOBILE_SCENARIO_NAME_MAX 49

typedef struct IntegralApiRoom {
    unsigned room_number;
    char room_code[INTEGRAL_API_ROOM_CODE_MAX];
    char room_type[INTEGRAL_API_ROOM_TYPE_MAX];
    int creator;
    char user1[INTEGRAL_API_ROOM_USER_MAX];
    char user2[INTEGRAL_API_ROOM_USER_MAX];
    char slot1[INTEGRAL_API_ROOM_SLOT_MAX];
    char slot2[INTEGRAL_API_ROOM_SLOT_MAX];
    char n64_slot1[INTEGRAL_API_ROOM_SLOT_MAX];
    char n64_slot2[INTEGRAL_API_ROOM_SLOT_MAX];
    char slot_filename1[INTEGRAL_API_ROOM_FILENAME_MAX];
    char slot_filename2[INTEGRAL_API_ROOM_FILENAME_MAX];
    char slot_game_type1[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
    char slot_game_type2[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
    char slot_header_title1[INTEGRAL_API_ROM_HEADER_TITLE_MAX];
    char slot_header_title2[INTEGRAL_API_ROM_HEADER_TITLE_MAX];
    char n64_slot_filename1[INTEGRAL_API_ROOM_FILENAME_MAX];
    char n64_slot_game_type1[INTEGRAL_API_ROOM_GAME_TYPE_MAX];
    char n64_slot_header_title1[INTEGRAL_API_ROM_HEADER_TITLE_MAX];
    char link_session_id[INTEGRAL_API_ROOM_SESSION_MAX];
    char link_mode[INTEGRAL_API_LINK_MODE_MAX];
    int game_started;
    int ready1;
    int ready2;
    unsigned chat_count;
    char chat[INTEGRAL_API_ROOM_CHAT_MAX][INTEGRAL_API_ROOM_CHAT_TEXT_MAX];
} IntegralApiRoom;

typedef struct IntegralApiRomSlot {
    unsigned slot;
    char rom_id[96];
    char save_id[96];
    char filename[256];
    char sha256[65];
    char game_type[65];
    char platform[INTEGRAL_API_ROM_PLATFORM_MAX];
    char region[17];
    char rom_header_title[INTEGRAL_API_ROM_HEADER_TITLE_MAX];
} IntegralApiRomSlot;

typedef struct IntegralApiMobileScenario {
    char scenario_id[INTEGRAL_API_MOBILE_SCENARIO_ID_MAX];
    char display_name[INTEGRAL_API_MOBILE_SCENARIO_NAME_MAX];
    char release_id[65];
    int is_default;
} IntegralApiMobileScenario;

typedef struct IntegralApiHeartbeatStatus {
    char lifecycle_kind[16];
    char lifecycle_session_id[INTEGRAL_API_ROOM_SESSION_MAX];
    char lifecycle_status[24];
    char termination_reason[64];
    char expires_at[40];
    long long server_unix_time;
} IntegralApiHeartbeatStatus;

int integral_api_login(const char *server_url,
                  const char *username,
                  const char *password,
                  const char *server_id,
                  char *token_out,
                  size_t token_out_size,
                  char *authenticated_username_out,
                  size_t authenticated_username_out_size,
                  int *must_change_password_out,
                  int *allow_user_initial_save_import_out,
                  char *error_out,
                  size_t error_out_size);

int integral_api_change_password(const char *server_url,
                            const char *token,
                            const char *new_password,
                            char *error_out,
                            size_t error_out_size);

int integral_api_parse_room_matching_response(const char *response,
                                         IntegralApiRoom *room_out,
                                         int *has_room_out);

int integral_api_create_room(const char *server_url,
                        const char *token,
                        const char *mode,
                        IntegralApiRoom *room_out,
                        char *error_out,
                        size_t error_out_size);

int integral_api_join_room_code(const char *server_url,
                           const char *token,
                           const char *room_code,
                           IntegralApiRoom *room_out,
                           char *error_out,
                           size_t error_out_size);

int integral_api_get_current_room(const char *server_url,
                             const char *token,
                             IntegralApiRoom *room_out,
                             int *has_room_out,
                             char *error_out,
                             size_t error_out_size);

int integral_api_leave_room(const char *server_url,
                        const char *token,
                        char *error_out,
                        size_t error_out_size);

int integral_api_stop_game(const char *server_url,
                      const char *token,
                      const char *game_session_id,
                      long long fencing_token,
                      char *error_out,
                      size_t error_out_size);

int integral_api_stop_local_game(const char *server_url,
                            const char *token,
                            const char *game_session_id,
                            long long fencing_token,
                            char *error_out,
                            size_t error_out_size);

int integral_api_heartbeat_game(const char *server_url,
                           const char *token,
                           const char *game_session_id,
                           long long fencing_token,
                           char *error_out,
                           size_t error_out_size);

int integral_api_get_link_game_fence(const char *server_url,
                                const char *token,
                                const char *expected_link_session_id,
                                char *game_session_id_out,
                                size_t game_session_id_out_size,
                                long long *fencing_token_out,
                                char *error_out,
                                size_t error_out_size);

int integral_api_start_local_game(const char *server_url,
                             const char *token,
                             const char *save_id1,
                             const char *save_id2,
                             char *game_session_id_out,
                             size_t game_session_id_out_size,
                             long long *fencing_token_out,
                             char *error_out,
                             size_t error_out_size);


int integral_api_start_mobile_session_contract(
    const char *server_url,
    const char *token,
    const char *save_id,
    const char *rom_id,
    const char *create_request_id,
    const char *scenario_id,
    char *mobile_session_id_out,
    size_t mobile_session_id_out_size,
    char *game_session_id_out,
    size_t game_session_id_out_size,
    long long *fencing_token_out,
    IntegralMobileRuntimeContract *contract_out,
    char *error_out,
    size_t error_out_size);

#define INTEGRAL_API_MOBILE_CREATE_ABORTED (-2)
#define INTEGRAL_API_MOBILE_CREATE_AUTH_SESSION_CONFLICT (-3)

int integral_api_list_mobile_scenarios(
    const char *server_url,
    const char *token,
    const char *save_id,
    const char *rom_id,
    IntegralApiMobileScenario *scenarios,
    unsigned *scenario_count,
    char *error_out,
    size_t error_out_size);

int integral_api_heartbeat_mobile_session(const char *server_url,
                                     const char *token,
                                     const char *mobile_session_id,
                                     const char *game_session_id,
                                     long long fencing_token,
                                     char *error_out,
                                     size_t error_out_size);

int integral_api_complete_mobile_session(const char *server_url,
                                    const char *token,
                                    const char *mobile_session_id,
                                    const char *game_session_id,
                                    long long fencing_token,
                                    char *error_out,
                                    size_t error_out_size);

int integral_api_cancel_mobile_session(const char *server_url,
                                  const char *token,
                                  const char *mobile_session_id,
                                  const char *game_session_id,
                                  long long fencing_token,
                                  const char *reason,
                                  char *error_out,
                                  size_t error_out_size);

int integral_api_start_game_with_saves(const char *server_url,
                                  const char *token,
                                  const char *execution_mode,
                                  const char *const *save_ids,
                                  unsigned save_count,
                                  char *game_session_id_out,
                                  size_t game_session_id_out_size,
                                  long long *fencing_token_out,
                                  char *error_out,
                                  size_t error_out_size);

int integral_api_room_heartbeat(const char *server_url,
                            const char *token,
                            char *error_out,
                            size_t error_out_size);

int integral_api_room_heartbeat_status(const char *server_url,
                            const char *token,
                            IntegralApiHeartbeatStatus *status_out,
                            char *error_out,
                            size_t error_out_size);

int integral_api_get_server_time(const char *server_url,
                            long long *unix_time_out,
                            char *error_out,
                            size_t error_out_size);

int integral_api_get_rom_slots(const char *server_url,
                          const char *token,
                          IntegralApiRomSlot *slots,
                          size_t slot_count,
                          char *error_out,
                          size_t error_out_size);

int integral_api_update_room_state(const char *server_url,
                                    const char *token,
                                    unsigned room_number,
                                    const char *slot,
                                    int ready,
                                    const char *link_mode,
                                    char *error_out,
                                    size_t error_out_size);

int integral_api_update_n64_room_state(const char *server_url,
                                        const char *token,
                                        unsigned room_number,
                                        const char *slot,
                                        const char *n64_slot,
                                        int ready,
                                        char *error_out,
                                        size_t error_out_size);

int integral_api_send_room_chat(const char *server_url,
                                 const char *token,
                                 unsigned room_number,
                                 const char *message,
                                 char *error_out,
                                 size_t error_out_size);

int integral_api_start_room(const char *server_url,
                             const char *token,
                             unsigned room_number,
                             const char *link_mode,
                             char *session_id_out,
                             size_t session_id_out_size,
                             char *error_out,
                             size_t error_out_size);

int integral_api_start_n64_room(const char *server_url,
                                 const char *token,
                                 unsigned room_number,
                                 char *session_id_out,
                                 size_t session_id_out_size,
                                 char *relay_host_out,
                                 size_t relay_host_out_size,
                                 unsigned *relay_port_out,
                                 char *relay_transport_out,
                                 size_t relay_transport_out_size,
                                 char *role_out,
                                 size_t role_out_size,
                                 char *scope_out,
                                 size_t scope_out_size,
                                 char *ticket_out,
                                 size_t ticket_out_size,
                                 char *error_out,
                                 size_t error_out_size);

int integral_api_gb_runtime_fixed_host_get_manifest(const char *server_url,
                                    const char *token,
                                    const char *session_id,
                                    char *role_out,
                                    size_t role_out_size,
                                    char *manifest_digest_out,
                                    size_t manifest_digest_out_size,
                                    char *host_game_type_out,
                                    size_t host_game_type_out_size,
                                    char *remote_game_type_out,
                                    size_t remote_game_type_out_size,
                                    char *host_platform_out,
                                    size_t host_platform_out_size,
                                    char *remote_platform_out,
                                    size_t remote_platform_out_size,
                                    char *host_header_title_out,
                                    size_t host_header_title_out_size,
                                    char *remote_header_title_out,
                                    size_t remote_header_title_out_size,
                                    char *runtime_build_id_out,
                                    size_t runtime_build_id_out_size,
                                    char *state_out,
                                    size_t state_out_size,
                                    unsigned *pause_remaining_seconds_out,
                                    char *error_out,
                                    size_t error_out_size);
int integral_api_gb_runtime_fixed_host_submit_preflight(const char *server_url,
                                        const char *token,
                                        const char *session_id,
                                        const char *manifest_digest,
                                        const char *runtime_build_id,
                                        const char *game_type_a,
                                        const char *platform_a,
                                        const char *header_title_a,
                                        const char *game_type_b,
                                        const char *platform_b,
                                        const char *header_title_b,
                                        char *state_out,
                                        size_t state_out_size,
                                        char *error_out,
                                        size_t error_out_size);
int integral_api_gb_runtime_fixed_host_issue_relay_ticket(const char *server_url,
                                          const char *token,
                                          const char *session_id,
                                          char *relay_host_out,
                                          size_t relay_host_out_size,
                                          unsigned *relay_port_out,
                                          char *relay_transport_out,
                                          size_t relay_transport_out_size,
                                          char *role_out,
                                          size_t role_out_size,
                                          char *scope_out,
                                          size_t scope_out_size,
                                          char *ticket_out,
                                          size_t ticket_out_size,
                                          char *save_policy_out,
                                          size_t save_policy_out_size,
                                          char *error_out,
                                          size_t error_out_size);
int integral_api_gb_runtime_fixed_host_submit_host_finish(
    const char *server_url, const char *token, const char *session_id,
    const char *game_session_id, long long fencing_token,
    uint64_t final_frame, const uint8_t terminal_digest[32],
    const uint8_t *host_candidate, size_t host_candidate_size,
    const uint8_t *remote_candidate, size_t remote_candidate_size,
    char *state_out, size_t state_out_size,
    char *error_out, size_t error_out_size);
int integral_api_gb_runtime_fixed_host_submit_terminal_receipt(
    const char *server_url, const char *token, const char *session_id,
    uint64_t final_frame, const uint8_t terminal_digest[32],
    char *state_out, size_t state_out_size,
    char *error_out, size_t error_out_size);
int integral_api_gb_runtime_fixed_host_download_snapshots(const char *server_url,
                                          const char *token,
                                          const char *session_id,
                                          unsigned char *host_save_out,
                                          size_t host_save_capacity,
                                          size_t *host_save_size_out,
                                          unsigned char *remote_save_out,
                                          size_t remote_save_capacity,
                                          size_t *remote_save_size_out,
                                          char *error_out,
                                          size_t error_out_size);
int integral_api_get_link_session_info(const char *server_url,
                                  const char *token,
                                  const char *session_id,
                                  char *status_out,
                                  size_t status_out_size,
                                  int *room_number_out,
                                  char *error_out,
                                  size_t error_out_size);
int integral_api_get_link_session_protocol(const char *server_url,
                                      const char *token,
                                      const char *session_id,
                                      char *protocol_id_out,
                                      size_t protocol_id_out_size,
                                      char *error_out,
                                      size_t error_out_size);

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
                         size_t error_out_size);

int integral_api_download_save(const char *server_url,
                          const char *token,
                          const char *save_id,
                          unsigned char *save_data_out,
                          size_t save_data_capacity,
                          size_t *save_data_size_out,
                          int *revision_out,
                          char *error_out,
                          size_t error_out_size);

int integral_api_download_n64_runtime_save(const char *server_url,
                                      const char *token,
                                      const char *media_session_id,
                                      const char *kind,
                                      unsigned char *save_data_out,
                                      size_t save_data_capacity,
                                      size_t *save_data_size_out,
                                      int *revision_out,
                                      char *error_out,
                                      size_t error_out_size);

int integral_api_upload_save(const char *server_url,
                        const char *token,
                        const char *save_id,
                        int expected_revision,
                        const unsigned char *save_data,
                        size_t save_data_size,
                        int *revision_out,
                        char *error_out,
                        size_t error_out_size);

int integral_api_upload_save_with_request_id(const char *server_url,
                                        const char *token,
                                        const char *save_id,
                                        int expected_revision,
                                        const unsigned char *save_data,
                                        size_t save_data_size,
                                        const char *request_id,
                                        int *revision_out,
                                        char *error_out,
                                        size_t error_out_size);

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
                               size_t error_out_size);

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
                           int confirm_delete_saves,
                           char *rom_id_out,
                           size_t rom_id_out_size,
                           char *save_id_out,
                           size_t save_id_out_size,
                           int *requires_confirmation_out,
                           char *error_out,
                           size_t error_out_size);

#endif

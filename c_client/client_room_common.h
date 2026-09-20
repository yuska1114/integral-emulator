/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef INTEGRAL_CLIENT_ROOM_COMMON_H
#define INTEGRAL_CLIENT_ROOM_COMMON_H
#include "client_room_link.h"
#include "client_room_n64.h"
#include "client_account.h"
#include "client_ui_common.h"
#define INTEGRAL_CHAT_LOG_LINES 32
#define INTEGRAL_CHAT_VISIBLE_LINES 5
#define INTEGRAL_CHAT_MESSAGE_MAX 160
#define INTEGRAL_LINK_ROOM_HEARTBEAT_MS 15000u
#define INTEGRAL_ROOM_HEARTBEAT_MS 5000u
#define INTEGRAL_ROOM_ROWS 5
#define INTEGRAL_N64_RUNTIME_ROOM_ROWS 5
#define INTEGRAL_N64_RUNTIME_STOP_WAIT_MS 5000u
typedef struct IntegralRoomCommonState {
    unsigned room_mode_selected;
    char room_code_input[INTEGRAL_API_ROOM_CODE_MAX];
    bool room_code_editing;
    IntegralApiRoom current_room;
    Uint32 last_room_heartbeat_ticks;
    Uint32 last_room_heartbeat_attempt_ticks;
    bool room_heartbeat_attempted;
    unsigned room_heartbeat_failures;
    char room_lifecycle_status[24];
    char room_termination_reason[64];
    long long room_remaining_seconds;
    uint32_t room_poll_epoch;
    uint32_t room_poll_last_ms;
    uint32_t room_poll_max_ms;
    unsigned room_number;
    unsigned room_selected;
    bool room_ready_self;
    bool room_ready_peer;
    bool room_chat_editing;
    unsigned room_chat_scroll;
    char room_chat_input[INTEGRAL_CHAT_MESSAGE_MAX];
    char room_chat_composition[INTEGRAL_CHAT_MESSAGE_MAX];
    char room_chat_log[INTEGRAL_CHAT_LOG_LINES][INTEGRAL_CHAT_MESSAGE_MAX];
    Uint32 last_room_poll_ticks;
} IntegralRoomCommonState;
/* ROOM owns its session/process state. UI/config pointers are borrowed for the
 * AppState lifetime; initialize bindings after zeroing AppState. Do not copy a
 * live context: the Fixed Host result reader retains its address until joined. */
struct IntegralRoomContext {
    IntegralRoomCommonState common;
    IntegralLinkRoomState link;
    IntegralN64RoomState n64;
    LoginState *login;
    const IntegralConfigKeys *keys;
    const char *config_path;
    const IntegralConfigRomSlot *rom_slots;
    const IntegralApiRomSlot *server_rom_slots;
    const int *local_slot_indices;
    AppScreen *screen;
    bool *quit;
    void *ui;
    SDL_Window *main_window; /* Borrowed; restored once when Remote closes. */
};
int current_room_user_position(const IntegralRoomContext *state);
bool current_room_is_user1(const IntegralRoomContext *state);
void load_room_chat_from_api(IntegralRoomContext *state);
bool room_status_is_actionable_error(const char *status);
const char *create_room_mode_api_name(unsigned selection);
void handle_room_mode_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key);
void handle_join_room_key(IntegralRoomContext *state, const SDL_KeyboardEvent *key);
void handle_join_room_text_input(IntegralRoomContext *state, const SDL_TextInputEvent *text);
void refresh_room(IntegralRoomContext *state);
void refresh_room_quiet(IntegralRoomContext *state);
void activate_matched_room(IntegralRoomContext *state, const IntegralApiRoom *matched_room);
void leave_current_room(IntegralRoomContext *state, bool update_status);
void finish_n64_room_locally(IntegralRoomContext *state);
void handle_room_heartbeat_result(IntegralRoomContext *state,
                                          const IntegralApiHeartbeatStatus *heartbeat,
                                          bool succeeded,
                                          const char *error);
void send_room_heartbeat(IntegralRoomContext *state, Uint32 now);
void handle_room_text_input(IntegralRoomContext *state, const SDL_TextInputEvent *text);
void handle_room_text_editing(IntegralRoomContext *state, const SDL_TextEditingEvent *edit);
unsigned chat_message_count(char log[INTEGRAL_CHAT_LOG_LINES][INTEGRAL_CHAT_MESSAGE_MAX]);
const IntegralConfigRomSlot *room_registered_rom_slot_at(const IntegralRoomContext *, int);
void client_room_log(const IntegralRoomContext *, const char *, const char *, ...);
void client_room_window_size(const IntegralRoomContext *, unsigned *, unsigned *);
bool client_room_recover(IntegralRoomContext *, const char *);
Uint32 main_loop_delay_ms(const IntegralRoomContext *state);
void poll_server_state(IntegralRoomContext *state);
void integral_room_reset_selection(IntegralRoomContext *state);
void integral_room_validate_selection(IntegralRoomContext *state, int local_indices[2]);
bool integral_room_create_media(IntegralRoomContext *, SDL_Window *, SDL_Renderer *);
bool integral_room_create_poll_worker(IntegralRoomContext *);
void integral_room_destroy_resources(IntegralRoomContext *);
#endif

/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_view.h"
#include "client_app.h"
#include "client_rom_catalog.h"
#include "client_runtime_support.h"
#include "client_version.h"
#include "client_ui_common.h"
#include "client_key_config.h"
#include "client_room_common.h"
#include "sdl_text.h"
#include "sdl_unicode_text.h"
#include <stdio.h>
#include <string.h>

#define INTEGRAL_WINDOW_WIDTH INTEGRAL_CLIENT_UI_WIDTH
#define INTEGRAL_WINDOW_HEIGHT 480

enum {
    ROM_SLOT_WARNING_SERVER_UNREGISTERED = 1u << 0,
    ROM_SLOT_WARNING_LOCAL_NOT_FOUND = 1u << 1,
};

static void draw_header(SDL_Renderer *renderer, const char *subtitle,
                        const char *login_id, const char *server_url)
{
    integral_client_ui_draw_header(renderer, subtitle, login_id, server_url, INTEGRAL_CLIENT_VERSION);
}

static unsigned format_rom_slot_summary(const IntegralConfigRomSlot *slot,
                                        const IntegralApiRomSlot *server_slot,
                                        const IntegralClientRomMetadataCache *metadata,
                                        char *out,
                                        size_t out_size)
{
    const char *filename = slot->rom_path[0] ? path_file_name(slot->rom_path) : "";
    bool registered = slot_has_server_registration(slot);
    bool local_found = slot->rom_path[0] != '\0' && metadata &&
                       strcmp(metadata->source_path, slot->rom_path) == 0 && metadata->file_found;

    /*
     * A pending local replacement deliberately has its local IDs cleared for
     * presentation, so do not let the previous server slot make it look
     * registered. Only fall back to server registration when there is no local
     * path at all.
     */
    if (!registered && slot->rom_path[0] == '\0' && server_slot &&
        server_slot->rom_id[0] != '\0' && server_slot->save_id[0] != '\0') {
        registered = true;
    }

    if (!filename[0] && server_slot && server_slot->filename[0]) {
        filename = server_slot->filename;
    }

    bool has_slot = slot->rom_path[0] != '\0' ||
                    (server_slot && (server_slot->filename[0] != '\0' ||
                                     server_slot->rom_id[0] != '\0' ||
                                     server_slot->save_id[0] != '\0'));
    if (!has_slot) {
        copy_text(out, out_size, "<EMPTY>");
        return 0u;
    }

    const char *header_title = "";
    if (local_found && metadata->header_valid) {
        header_title = metadata->metadata.header_title;
    }
    else if (registered && server_slot && server_slot->rom_header_title[0] != '\0') {
        header_title = server_slot->rom_header_title;
    }

    if (filename[0] && header_title[0]) {
        snprintf(out, out_size, "%s (%s)", filename, header_title);
    }
    else if (filename[0]) {
        copy_text(out, out_size, filename);
    }
    else if (header_title[0]) {
        snprintf(out, out_size, "SERVER ROM (%s)", header_title);
    }
    else {
        copy_text(out, out_size, "SERVER ROM");
    }

    unsigned warnings = 0u;
    if (!registered) {
        warnings |= ROM_SLOT_WARNING_SERVER_UNREGISTERED;
    }
    if (!local_found) {
        warnings |= ROM_SLOT_WARNING_LOCAL_NOT_FOUND;
    }
    return warnings;
}

void integral_client_ui_draw_rom_register(SDL_Renderer *renderer, const AppState *state)
{
    SDL_SetRenderDrawColor(renderer, 20, 24, 28, 255);
    SDL_RenderClear(renderer);

    SDL_Color label = {160, 180, 196, 255};
    SDL_Color value = {238, 238, 238, 255};
    SDL_Color selected = {86, 162, 126, 255};
    SDL_Color muted = {112, 122, 130, 255};
    SDL_Color warning = {230, 92, 76, 255};

    draw_header(renderer, "ROM REGISTER", state->login.username, state->login.server);
    integral_sdl_draw_text(renderer, 24, 88, "MAX 8 ROMS  SAV IS SERVER MANAGED", 1, muted);

    for (unsigned i = 0; i < INTEGRAL_ROM_ROWS; i++) {
        int y = 112 + (int)i * 27;
        if (state->catalog.rom_editor.rom_selected == i) {
            SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
            SDL_Rect rect = {.x = 14, .y = y - 6, .w = INTEGRAL_WINDOW_WIDTH - 28, .h = 27};
            SDL_RenderFillRect(renderer, &rect);
            integral_sdl_draw_text(renderer, 24, y, ">", 2, selected);
        }
        if (i < INTEGRAL_ROM_SLOTS) {
            char label_text[24];
            char summary[320];
            snprintf(label_text, sizeof(label_text), "ROM%u", i + 1);
            IntegralConfigRomSlot display_slot = state->catalog.rom_slots[i];
            if (state->catalog.rom_editor.pending_paths[i] &&
                strcmp(display_slot.rom_path, state->catalog.rom_editor.confirmed_slots[i].rom_path) != 0) {
                /* Presentation only: the authoritative IDs remain intact. */
                display_slot.rom_id[0] = '\0';
                display_slot.save_id[0] = '\0';
            }
            unsigned warnings =
                format_rom_slot_summary(&display_slot, &state->catalog.server_rom_slots[i],
                                        client_rom_metadata_for_slot(state, (int)i), summary, sizeof(summary));
            char warning_text[64] = "";
            if ((warnings & ROM_SLOT_WARNING_SERVER_UNREGISTERED) &&
                (warnings & ROM_SLOT_WARNING_LOCAL_NOT_FOUND)) {
                copy_text(warning_text, sizeof(warning_text), "SERVER UNREGISTERED  LOCAL NOT FOUND");
            }
            else if (warnings & ROM_SLOT_WARNING_SERVER_UNREGISTERED) {
                copy_text(warning_text, sizeof(warning_text), "SERVER UNREGISTERED");
            }
            else if (warnings & ROM_SLOT_WARNING_LOCAL_NOT_FOUND) {
                copy_text(warning_text, sizeof(warning_text), "LOCAL NOT FOUND");
            }

            integral_sdl_draw_text(renderer, 48, y, label_text, 2, state->catalog.rom_editor.rom_selected == i ? selected : label);

            const int detail_width = 300;
            const int warning_gap = warning_text[0] ? 12 : 0;
            int warning_width = (int)strlen(warning_text) * 6;
            int summary_width = detail_width - warning_gap - warning_width;
            if (summary_width < 6) {
                summary_width = 6;
            }
            if (state->catalog.rom_editor.rom_selected == i) {
                integral_client_ui_draw_marquee_text_fit(renderer, 146, y + 4, summary, 1, value, summary_width, i * 7u);
            }
            else {
                integral_client_ui_draw_text_fit(renderer, 146, y + 4, summary, 1, value, summary_width);
            }
            if (warning_text[0]) {
                integral_sdl_draw_text(renderer,
                                       146 + summary_width + warning_gap,
                                       y + 4,
                                       warning_text,
                                       1,
                                       warning);
            }
        }
        else if (i == INTEGRAL_ROM_EXPORT_ROW) {
            integral_sdl_draw_text(renderer, 48, y, "EXPORT", 2, state->catalog.rom_editor.rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "DOWNLOAD SAVS", 1, muted);
        }
        else {
            integral_sdl_draw_text(renderer, 48, y, "BACK", 2, state->catalog.rom_editor.rom_selected == i ? selected : label);
            integral_sdl_draw_text(renderer, 146, y + 4, "RETURN MENU", 1, muted);
        }
    }

    if (state->catalog.rom_editor.rom_edit_target != ROM_EDIT_NONE && state->catalog.rom_editor.rom_selected < INTEGRAL_ROM_SLOTS) {
        const IntegralConfigRomSlot *slot = &state->catalog.rom_slots[state->catalog.rom_editor.rom_selected];
        const char *label_text = state->catalog.rom_editor.rom_edit_target == ROM_EDIT_INITIAL_SAVE
                                      ? "SELECT INITIAL SAV PATH"
                                      : "EDIT ROM PATH";
        const char *value_text = state->catalog.rom_editor.rom_edit_target == ROM_EDIT_INITIAL_SAVE
                                     ? state->catalog.rom_editor.rom_initial_save_import_path
                                     : slot->rom_path;
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 240);
        SDL_Rect overlay = {.x = 42, .y = 320, .w = 396, .h = 88};
        SDL_RenderFillRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 62, 342, label_text, 2, selected);
        integral_sdl_draw_utf8_scrolled(renderer, 62, 372, value_text[0] ? value_text : "<EMPTY>",
                                       12, value, 328, 0, true);
        integral_sdl_draw_text(renderer, 394, 372, "_", 1, selected);
    }
    else if (state->catalog.rom_editor.rom_browser_active) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 86, .w = 412, .h = 322};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 106, "ROMS FOLDER", 2, selected);
        unsigned visible = state->catalog.rom_editor.rom_browser_count < 8 ? state->catalog.rom_editor.rom_browser_count : 8;
        unsigned start = 0;
        if (state->catalog.rom_editor.rom_browser_selected >= visible) {
            start = state->catalog.rom_editor.rom_browser_selected - visible + 1;
        }
        for (unsigned i = 0; i < visible; i++) {
            unsigned index = start + i;
            int y = 142 + (int)i * 28;
            if (index == state->catalog.rom_editor.rom_browser_selected) {
                SDL_SetRenderDrawColor(renderer, 38, 72, 62, 255);
                SDL_Rect row = {.x = 46, .y = y - 6, .w = 388, .h = 24};
                SDL_RenderFillRect(renderer, &row);
                integral_sdl_draw_text(renderer, 56, y, ">", 1, selected);
            }
            integral_client_ui_draw_text_fit(renderer,
                                  76,
                                  y,
                                  path_file_name(state->catalog.rom_editor.rom_browser_entries[index]),
                                  1,
                                  value,
                                  340);
        }
        integral_sdl_draw_text(renderer, 54, 374, "LEFT/RIGHT MOVE  ENTER CHOOSE  ESC CLOSE", 1, muted);
    }
    else if (state->catalog.rom_editor.rom_confirm_delete) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 242, .w = 412, .h = 166};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        unsigned index = state->catalog.rom_editor.rom_selected;
        char target[160];
        snprintf(target, sizeof(target), "ACCOUNT %s / ROM%u", state->login.username, index + 1);
        integral_sdl_draw_text(renderer, 54, 252, "REPLACE ROM AND DELETE OLD SAV?", 1, selected);
        integral_client_ui_draw_text_fit(renderer, 54, 274, target, 1, value, 372);
        if (index < INTEGRAL_ROM_SLOTS) {
            char old_rom[384], new_rom[384];
            const IntegralClientRomMetadataCache *cached = client_rom_metadata_for_slot(state, (int)index);
            const char *header_title = cached && cached->header_valid ? cached->metadata.header_title : "";
            snprintf(old_rom, sizeof(old_rom), "%s (%s)", state->catalog.server_rom_slots[index].filename,
                     state->catalog.server_rom_slots[index].rom_header_title);
            snprintf(new_rom, sizeof(new_rom), "%s (%s)", path_file_name(state->catalog.rom_slots[index].rom_path), header_title);
            integral_sdl_draw_text(renderer, 54, 298, "OLD:", 1, label);
            integral_client_ui_draw_text_fit(renderer, 86, 298, old_rom, 1, value, 340);
            integral_sdl_draw_text(renderer, 54, 324, "NEW:", 1, label);
            integral_client_ui_draw_text_fit(renderer, 86, 324, new_rom, 1, value, 340);
        }
        integral_sdl_draw_text(renderer, 54, 354, "OLD SAVE WILL BE LOST", 1, selected);
        integral_sdl_draw_text(renderer, 54, 382, "ENTER YES   ESC NO", 1, value);
    }
    else if (state->catalog.rom_editor.rom_confirm_initial_save_import) {
        SDL_SetRenderDrawColor(renderer, 8, 12, 16, 245);
        SDL_Rect overlay = {.x = 34, .y = 316, .w = 412, .h = 92};
        SDL_RenderFillRect(renderer, &overlay);
        SDL_SetRenderDrawColor(renderer, selected.r, selected.g, selected.b, selected.a);
        SDL_RenderDrawRect(renderer, &overlay);
        integral_sdl_draw_text(renderer, 54, 338, "SEND SELECTED INITIAL SAV?", 2, selected);
        integral_sdl_draw_text(renderer, 54, 368, "ENTER YES   ESC NO", 1, value);
    }

    integral_client_ui_draw_text_fit(renderer, 22, 424, state->login.status, 1, muted, INTEGRAL_WINDOW_WIDTH - 44);
    if (state->catalog.rom_editor.rom_edit_target != ROM_EDIT_NONE) {
        integral_sdl_draw_text(renderer, 22, 442, "ENTER APPLY INPUT  ESC CANCEL EDIT", 1, muted);
        integral_sdl_draw_text(renderer, 22, 460, "BACKSPACE DELETE CHAR", 1, muted);
    }
    else if (state->catalog.allow_user_initial_save_import) {
        integral_sdl_draw_text(renderer, 22, 442, "LEFT/RIGHT SET ROM  ENTER REGISTER  F2 EDIT  F4 LIST", 1, muted);
        if (state->catalog.rom_editor.rom_initial_save_import_slot >= 0) {
            char import_status[64];
            snprintf(import_status, sizeof(import_status),
                     "F5 SAV  INITIAL SAV SELECTED FOR ROM%d",
                     state->catalog.rom_editor.rom_initial_save_import_slot + 1);
            integral_sdl_draw_text(renderer, 22, 460, import_status, 1, muted);
        }
        else {
            integral_sdl_draw_text(renderer, 22, 460, "F5 SAV", 1, muted);
        }
    }
    else {
        integral_sdl_draw_text(renderer, 22, 442, "LEFT/RIGHT SET ROM  ENTER REGISTER  F2 EDIT  F4 LIST", 1, muted);
    }
    SDL_RenderPresent(renderer);
}

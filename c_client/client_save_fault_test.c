/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_save_outbox.h"
#include "client_file_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

void client_save_log(const char *event, const char *format, ...)
{ (void)event; (void)format; }

int main(int argc, char **argv)
{
    assert(argc == 3);
    if (!strcmp(argv[2], "start")) {
        LocalSyncSlot slot = {0};
        strcpy(slot.account, "test-owner");
        strcpy(slot.save_id, "test-save");
        strcpy(slot.save_path, "runtime/working.sav");
        strcpy(slot.last_hash, "old");
        slot.revision = 7;
        const unsigned char first[] = {1,2,3,4}, latest[] = {5,6,7,8};
        assert(ensure_private_runtime_directory("runtime") == 0);
        assert(atomic_replace_binary_file(slot.save_path, first, sizeof(first)) == 0);
        assert(!upload_changed_save(argv[1], "test-token", "test-game", 41, &slot, false));
        assert(slot.revision == 7 && slot.preserve_save_path);
        assert(atomic_replace_binary_file(slot.save_path, latest, sizeof(latest)) == 0);
    } else {
        unsigned pending = 99;
        assert(replay_save_upload_outbox(argv[1], "test-token", "test-save", &pending, "test-owner") == 1);
        assert(pending == 0);
        assert(!save_upload_outbox_pending(argv[1], "test-save", "test-owner", NULL, 0));
    }
    return 0;
}

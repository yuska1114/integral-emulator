/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "transfer_pak/memory_session.h"
#include "../../../gb/src/common/utf8_file.h"
#include <string.h>

void transfer_pak_memory_clear(TransferPakMemorySession *session)
{
    transfer_sav_pair_clear(&session->pair);
    integral_gb_runtime_secure_zero(session,sizeof(*session));
}

int transfer_pak_memory_prepare(TransferPakMemorySession *session, TransferPakMediaSession *media,
                                const char *storage, const char *id, int fd)
{
    memset(session,0,sizeof(*session));
    memset(media,0,sizeof(*media));
    int result=transfer_sav_receive(transfer_sav_pipe_read,&fd,id,&session->pair);
    transfer_sav_close(fd);
    if(result || !storage) goto fail;
    for(int i=0;i<2;i++) {
        unsigned char header[0x150];
        if(transfer_pak_storage_slot_path(storage,(uint32_t)i+1,"gbc",media->rom_paths[i],sizeof(media->rom_paths[i]))) goto fail;
        FILE *rom=integral_fopen(media->rom_paths[i],"rb");
        if(!rom) goto fail;
        size_t count=fread(header,1,sizeof(header),rom);
        fclose(rom);
        if(count!=sizeof(header)) goto fail;
        static const size_t sizes[]={0,2048,8192,32768,131072,65536};
        if(header[0x149]>=sizeof(sizes)/sizeof(*sizes)) goto fail;
        size_t ram=sizes[header[0x149]];
        if(header[0x149]==0 && (header[0x147]==5 || header[0x147]==6)) ram=512;
        size_t size=session->pair.lengths[i];
        if(!transfer_sav_ready(header,session->pair.saves[i].data,size)) goto fail;
        IntegralTransferMemory *slot=&session->slots[i];
        slot->data=session->pair.saves[i].data;
        slot->size=ram;
        slot->has_rtc=header[0x147]==0x0f || header[0x147]==0x10;
        if(slot->has_rtc) memcpy(slot->rtc,slot->data+ram,48);
        media->ready[i]=1;
    }
    return 0;
fail:
    transfer_pak_memory_clear(session);
    memset(media,0,sizeof(*media));
    return -1;
}

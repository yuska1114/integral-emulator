/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "transfer_pak/memory_session.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include "../../../c_client/windows_process.h"
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

typedef struct Bytes { unsigned char data[2 * TRANSFER_SAV_MAX + 256]; size_t size, pos, chunk; } Bytes;
static int store(void *opaque, void *data, size_t size)
{
    Bytes *b=opaque; if(size>sizeof(b->data)-b->size) return -1;
    memcpy(b->data+b->size,data,size); b->size+=size; return (int)size;
}
static int take(void *opaque, void *data, size_t size)
{
    Bytes *b=opaque;
    if(size>b->size-b->pos) size=b->size-b->pos;
    if(b->chunk && size>b->chunk) size=b->chunk;
    memcpy(data,b->data+b->pos,size); b->pos+=size; return (int)size;
}
static void fill(TransferSavPair *p, int rtc)
{
    memset(p,0,sizeof(*p));
    for(int i=0;i<2;i++) {
        size_t n=32768+((i==0 && rtc)?48:0);
        assert(integral_gb_runtime_secure_buffer_init(&p->saves[i],n,INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
        memset(p->saves[i].data,0x31+i,32768);
        p->lengths[i]=n; p->revisions[i]=(uint32_t)i+7;
    }
    if(rtc) {
        unsigned char *t=p->saves[0].data+32768;
        memset(t,0,48); t[0]=12; t[4]=34; t[8]=5;
        memcpy(t+20,t,20);
        t[40]=0x80;t[41]=0x43;t[42]=0x6d;t[43]=0x38; /* 2000-01-01 */
    }
}
static void protocol_test(void)
{
    unsigned char header[0x150]={0},save[65536+48]={0};
    header[0x147]=3;header[0x149]=3;
    assert(!transfer_sav_ready(header,save,32768));
    save[0]=1;assert(transfer_sav_ready(header,save,32768));
    header[0x149]=5;assert(!transfer_sav_ready(header,save,32768));
    header[0x147]=0x10;
    assert(!transfer_sav_ready(header,save,65536));
    save[65536+40]=0x80;save[65536+41]=0x43;save[65536+42]=0x6d;save[65536+43]=0x38;
    assert(transfer_sav_ready(header,save,sizeof(save)));
    save[65536]=60;assert(!transfer_sav_ready(header,save,sizeof(save)));
    save[65536]=0;save[65536+1]=1;assert(!transfer_sav_ready(header,save,sizeof(save)));
    puts("ROOM initial/mismatched/missing/invalid RTC rejection, valid SAV acceptance PASS");
    TransferSavPair limit={0}, received;
    Bytes *boundary=calloc(1,sizeof(*boundary));assert(boundary);
    for (int i=0;i<2;i++) {
        assert(integral_gb_runtime_secure_buffer_init(&limit.saves[i],TRANSFER_SAV_MAX+1,
            INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT));
        memset(limit.saves[i].data,0,TRANSFER_SAV_MAX+1);
        limit.lengths[i]=TRANSFER_SAV_MAX;limit.revisions[i]=1;
    }
    assert(TRANSFER_SAV_MAX==131072+48);
    assert(!transfer_sav_send(store,boundary,"boundary",&limit));
    boundary->chunk=13;
    assert(!transfer_sav_receive(take,boundary,"boundary",&received));
    assert(received.lengths[0]==TRANSFER_SAV_MAX && received.lengths[1]==TRANSFER_SAV_MAX);
    unsigned char rtc[48];
    assert(transfer_pak_memory_rtc(1,131072,received.saves[0].data,received.lengths[0],rtc)>=0);
    transfer_sav_pair_clear(&received);
    limit.lengths[0]++;
    assert(transfer_sav_send(store,boundary,"boundary",&limit)!=0);
    /* First record length at offset 52: maximum + 1 = 0x00020031. */
    boundary->pos=0;boundary->data[55]=0x31;
    assert(transfer_sav_receive(take,boundary,"boundary",&received)!=0);
    assert(!received.saves[0].data);
    transfer_sav_pair_clear(&limit);free(boundary);
    puts("128 KiB + 48-byte RTC accepted; sender/receiver maximum + 1 rejected PASS");
    unsigned char ram[32768+48]={0},clock[48];
    assert(transfer_pak_memory_rtc(1,32768,ram,32767,clock)==-1);
    assert(transfer_pak_memory_rtc(1,32768,ram,32768,clock)==1);
    memset(ram+32768,0xff,48);
    assert(transfer_pak_memory_rtc(1,32768,ram,sizeof(ram),clock)==1);
    assert(clock[0]<60 && clock[4]<60 && clock[8]<24);
    assert(transfer_pak_memory_rtc(0,32768,ram,32768,clock)==0);
    TransferSavPair p,out; Bytes b={0}; fill(&p,1);
    assert(transfer_sav_send(store,&b,"session_test",&p)==0);
    size_t full=b.size;
    for(size_t chunk=1;chunk<600;chunk+=97) {
        b.pos=0;b.chunk=chunk;
        assert(transfer_sav_receive(take,&b,"session_test",&out)==0);
        assert(out.revisions[0]==7 && out.revisions[1]==8);
        assert(!memcmp(p.saves[0].data,out.saves[0].data,p.lengths[0]));
        transfer_sav_pair_clear(&out); assert(!out.saves[0].data);
    }
    size_t corrupt[]={0,7,11,12,47,51,52,56,60,100};
    for(size_t i=0;i<sizeof(corrupt)/sizeof(*corrupt);i++) {
        b.pos=0; b.data[corrupt[i]]^=0x80;
        assert(transfer_sav_receive(take,&b,"session_test",&out)!=0);
        assert(!out.saves[0].data && !out.saves[1].data);
        b.data[corrupt[i]]^=0x80;
    }
    for(size_t size=0;size<full;size+=1019) {
        b.pos=0;b.size=size;
        assert(transfer_sav_receive(take,&b,"session_test",&out)!=0);
    }
    b.pos=0;b.size=full+1; b.data[full]=1;
    assert(transfer_sav_receive(take,&b,"session_test",&out)!=0);
    b.pos=0;b.size=full;
    assert(transfer_sav_receive(take,&b,"wrong_session",&out)!=0);
    integral_gb_runtime_secure_buffer_clear(&p.saves[0]);
    for(size_t i=0;i<p.saves[0].size;i++) assert(p.saves[0].data[i]==0);
    transfer_sav_pair_clear(&p);
    puts("IPC fragmentation, truncation, version/order/size/revision/hash/session rejection and wipe PASS");
}
static void create_roms(const char *dir)
{
    for(int i=0;i<2;i++) {
        char path[2048]; unsigned char rom[0x150]={0};
        snprintf(path,sizeof(path),"%s/slot%d.gbc",dir,i+1);
        rom[0x147]=(unsigned char)(i==0?0x10:3);rom[0x149]=3;
        FILE *f=fopen(path,"wb");assert(f);
        assert(fwrite(rom,1,sizeof(rom),f)==sizeof(rom));assert(!fclose(f));
    }
}
static int child(int fd,const char *dir,int rtc)
{
    TransferPakMemorySession s;TransferPakMediaSession media;
    if (rtc != 1) {
        assert(transfer_pak_memory_prepare(&s,&media,dir,"session_test",fd)!=0);
        assert(!s.pair.saves[0].data && !s.pair.saves[1].data);
        return 0;
    }
    assert(transfer_pak_memory_prepare(&s,&media,dir,"session_test",fd)==0);
    assert(media.ready[0] && media.ready[1] && !media.ram_paths[0][0]);
    assert(s.slots[0].has_rtc==1 && s.slots[1].has_rtc==0);
    if(rtc == 1) assert(s.slots[0].rtc[0]==12 && s.slots[0].rtc[4]==34);
    for(int i=0;i<2;i++) {
        IntegralTransferMemory *slot=&s.slots[i];
        assert(integral_transfer_memory_size(slot)==32768);
        unsigned char *ram=integral_transfer_memory_data(slot);
        assert(ram[0]==0x31+i && ram[32767]==0x31+i);
        ram[32767]=99;integral_transfer_memory_save(slot,32767,1);
        assert(ram[32767]==99);
    }
    transfer_pak_memory_clear(&s);
    const unsigned char *p=(const unsigned char *)&s;
    for(size_t i=0;i<sizeof(s);i++) assert(p[i]==0);
    return 0;
}
int main(int argc,char **argv)
{
    if (argc==4 && !strcmp(argv[1],"--check-save")) {
        unsigned char header[0x150],save[TRANSFER_SAV_MAX+1];
        FILE *f=fopen(argv[2],"rb");assert(f);
        assert(fread(header,1,sizeof(header),f)==sizeof(header));assert(!fclose(f));
        f=fopen(argv[3],"rb");assert(f);
        size_t size=fread(save,1,sizeof(save),f);assert(!ferror(f));assert(!fclose(f));
        int ready=transfer_sav_ready(header,save,size);
        integral_gb_runtime_secure_zero(save,sizeof(save));
        printf("ROOM SAV ready=%d bytes=%zu\n",ready,size);
        return ready ? 0 : 2;
    }
    if(argc==3 && !strcmp(argv[1],"--hold")) {
        create_roms(argv[2]);
        TransferSavPair pair; fill(&pair,1);
        puts("MEMORY READY"); fflush(stdout);
        for(;;) {
#ifdef _WIN32
            Sleep(100);
#else
            sleep(1);
#endif
        }
    }
    if(argc==5 && !strcmp(argv[1],"--child")) return child(atoi(argv[2]),argv[3],atoi(argv[4]));
    assert(argc==2);protocol_test();create_roms(argv[1]);
    /* An exited receiver must report failure, not kill/hang the sender. */
    int broken[2]; assert(!transfer_sav_pipe(broken));
    transfer_sav_close(broken[0]);
    unsigned char byte=1;
    assert(transfer_sav_pipe_write(&broken[1],&byte,1)<0);
    transfer_sav_close(broken[1]);
    for(int rtc=0;rtc<4;rtc++) {
        if (rtc == 2) {
            for (int i=0;i<2;i++) {
                char path[2048]; snprintf(path,sizeof(path),"%s/slot%d.gbc",argv[1],i+1);
                FILE *f=fopen(path,"r+b"); assert(f);
                assert(!fseek(f,0x149,SEEK_SET)); assert(fputc(5,f)==5); assert(!fclose(f));
            }
        }
        int fd[2];assert(!transfer_sav_pipe(fd));
        char number[32],rtc_text[2]={(char)('0'+rtc),0};snprintf(number,sizeof(number),"%d",fd[0]);
#ifdef _WIN32
        const char *args[]={argv[0],"--child",number,argv[1],rtc_text,NULL};
        intptr_t pid=integral_windows_spawnv(_P_NOWAIT,argv[0],args);assert(pid!=-1);
#else
        pid_t pid=fork();assert(pid>=0);
        if(!pid) { transfer_sav_close(fd[1]);execl(argv[0],argv[0],"--child",number,argv[1],rtc_text,(char*)NULL);_exit(127); }
#endif
        transfer_sav_close(fd[0]);TransferSavPair pair;fill(&pair,rtc == 1);
        if (rtc >= 2) {
            for (int i=0;i<2;i++) memset(pair.saves[i].data,0,pair.lengths[i]);
            if (rtc == 3) pair.saves[1].data[123]=1;
        }
        assert(!transfer_sav_send(transfer_sav_pipe_write,&fd[1],"session_test",&pair));
        transfer_sav_close(fd[1]);transfer_sav_pair_clear(&pair);
#ifdef _WIN32
        assert(WaitForSingleObject((HANDLE)pid,15000)==WAIT_OBJECT_0);
        DWORD code;assert(GetExitCodeProcess((HANDLE)pid,&code));CloseHandle((HANDLE)pid);assert(code==0);
#else
        int code;assert(waitpid(pid,&code,0)==pid && WIFEXITED(code) && WEXITSTATUS(code)==0);
#endif
    }
    puts("Inherited pipe, RAM storage, RTC preserved; unprepared saves rejected and zeroized PASS");
    return 0;
}

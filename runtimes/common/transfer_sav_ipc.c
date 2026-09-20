/* SPDX-License-Identifier: GPL-3.0-or-later */
#if !defined(_WIN32)
#define _POSIX_C_SOURCE 200809L
#endif
#include "transfer_sav_ipc.h"
#include "../gb/src/server/content_hash.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <time.h>

int transfer_sav_ready(const unsigned char *header, const unsigned char *save, size_t size)
{
    static const size_t sizes[] = {0,2048,8192,32768,131072,65536};
    if (!header || !save || header[0x149] >= sizeof(sizes)/sizeof(*sizes)) return 0;
    if (size == 32768) {
        size_t i = 0;
        while (i < size && save[i] == 0) ++i;
        if (i == size) return 0;
    }
    size_t ram = sizes[header[0x149]];
    if (!ram && (header[0x147] == 5 || header[0x147] == 6)) ram = 512;
    if (header[0x147] != 0x0f && header[0x147] != 0x10) return size == ram;
    if (size != ram + 48) return 0;
    const unsigned char *rtc = save + ram;
    for (unsigned bank = 0; bank < 2; ++bank) {
        const unsigned char *r = rtc + bank * 20;
        if (r[0] >= 60 || r[4] >= 60 || r[8] >= 24 || (r[16] & 0x3e)) return 0;
        for (unsigned i = 0; i < 20; ++i) if (i % 4 && r[i]) return 0;
    }
    uint64_t last = 0;
    for (unsigned i = 0; i < 8; ++i) last |= (uint64_t)rtc[40+i] << (8*i);
    time_t now = time(NULL);
    return now != (time_t)-1 && last >= 852076800u && last <= (uint64_t)now;
}
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#endif

static void put32(uint8_t *p, uint32_t n)
{ for (int i=3; i>=0; --i) { p[i]=(uint8_t)n; n>>=8; } }
static uint32_t get32(const uint8_t *p)
{ return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3]; }
static int all(TransferSavIO io, void *ctx, void *data, size_t size)
{
    uint8_t *p=data;
    while (size) {
        int n=io(ctx,p,size);
        if (n<=0 || (size_t)n>size) return -1;
        p+=n; size-=(size_t)n;
    }
    return 0;
}
static int session_hash(const char *session, uint8_t hash[32])
{
    if (!session || !session[0] || strlen(session)>160 ||
        strspn(session,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(session)) return -1;
    integral_gb_runtime_content_sha256(session,strlen(session),hash);
    return 0;
}
void transfer_sav_pair_clear(TransferSavPair *pair)
{
    for (int i=0;i<2;i++) integral_gb_runtime_secure_buffer_release(&pair->saves[i]);
    integral_gb_runtime_secure_zero(pair,sizeof(*pair));
}
int transfer_sav_send(TransferSavIO io, void *ctx, const char *session, const TransferSavPair *pair)
{
    uint8_t preamble[44]={ 'I','T','P','S',0,0,0,1,0,0,0,2 };
    if (session_hash(session,preamble+12)) return -1;
    for(int i=0;i<2;i++) if (!pair->saves[i].data || !pair->lengths[i] || pair->lengths[i]>TRANSFER_SAV_MAX || pair->lengths[i]>pair->saves[i].size || pair->revisions[i]>INT_MAX) return -1;
    if(all(io,ctx,preamble,sizeof(preamble))) return -1;
    for(int i=0;i<2;i++) {
        uint8_t header[48]={0};
        put32(header,48); put32(header+4,(uint32_t)i+1);
        put32(header+8,(uint32_t)pair->lengths[i]); put32(header+12,pair->revisions[i]);
        integral_gb_runtime_content_sha256(pair->saves[i].data,pair->lengths[i],header+16);
        if(all(io,ctx,header,sizeof(header)) || all(io,ctx,pair->saves[i].data,pair->lengths[i])) return -1;
    }
    return 0;
}
int transfer_sav_receive(TransferSavIO io, void *ctx, const char *session, TransferSavPair *pair)
{
    uint8_t preamble[44],expected[32],extra;
    memset(pair,0,sizeof(*pair));
    if(session_hash(session,expected) || all(io,ctx,preamble,sizeof(preamble)) ||
        memcmp(preamble,"ITPS",4) || get32(preamble+4)!=1 || get32(preamble+8)!=2 || memcmp(preamble+12,expected,32)) goto fail;
    for(int i=0;i<2;i++) {
        uint8_t header[48],hash[32];
        if(all(io,ctx,header,sizeof(header)) || get32(header)!=48 || get32(header+4)!=(uint32_t)i+1) goto fail;
        uint32_t size=get32(header+8),revision=get32(header+12);
        if(!size || size>TRANSFER_SAV_MAX || revision>INT_MAX ||
            !integral_gb_runtime_secure_buffer_init(&pair->saves[i],size,INTEGRAL_GB_RUNTIME_MEMORY_LOCK_BEST_EFFORT)) goto fail;
        pair->lengths[i]=size; pair->revisions[i]=revision;
        if(all(io,ctx,pair->saves[i].data,size)) goto fail;
        integral_gb_runtime_content_sha256(pair->saves[i].data,size,hash);
        if(memcmp(hash,header+16,32)) goto fail;
    }
    if(io(ctx,&extra,1)!=0) goto fail;
    return 0;
fail:
    transfer_sav_pair_clear(pair);
    return -1;
}
int transfer_sav_pipe(int fd[2])
{
#ifdef _WIN32
    if(_pipe(fd,4096,_O_BINARY)!=0) return -1;
    if(!SetHandleInformation((HANDLE)_get_osfhandle(fd[1]),HANDLE_FLAG_INHERIT,0)) {
        _close(fd[0]); _close(fd[1]); return -1;
    }
#else
    if(pipe(fd)!=0) return -1;
    if(fcntl(fd[1],F_SETFD,FD_CLOEXEC)!=0) { close(fd[0]); close(fd[1]); return -1; }
#endif
    return 0;
}
void transfer_sav_close(int fd)
{
    if(fd<0) return;
#ifdef _WIN32
    _close(fd);
#else
    close(fd);
#endif
}
int transfer_sav_pipe_read(void *ctx, void *data, size_t size)
{
    int fd=*(int *)ctx,n;
#ifdef _WIN32
    if(GetFileType((HANDLE)_get_osfhandle(fd))!=FILE_TYPE_PIPE) return -1;
#else
    struct stat st;
    if(fstat(fd,&st)!=0 || !S_ISFIFO(st.st_mode)) return -1;
#endif
    do {
#ifdef _WIN32
        n=_read(fd,data,(unsigned)size);
#else
        n=(int)read(fd,data,size);
#endif
    } while(n<0 && errno==EINTR);
    return n;
}
int transfer_sav_pipe_write(void *ctx, void *data, size_t size)
{
    int fd=*(int *)ctx,n;
#ifndef _WIN32
    sigset_t set,old;
    sigemptyset(&set); sigaddset(&set,SIGPIPE);
    if(pthread_sigmask(SIG_BLOCK,&set,&old)!=0) return -1;
#endif
    do {
#ifdef _WIN32
        n=_write(fd,data,(unsigned)size);
#else
        n=(int)write(fd,data,size);
#endif
    } while(n<0 && errno==EINTR);
#ifndef _WIN32
    if(n<0 && errno==EPIPE && !sigismember(&old,SIGPIPE)) {
        sigset_t pending;
        if(sigpending(&pending)==0 && sigismember(&pending,SIGPIPE)) {
            int sig; (void)sigwait(&set,&sig);
        }
    }
    (void)pthread_sigmask(SIG_SETMASK,&old,NULL);
#endif
    return n;
}

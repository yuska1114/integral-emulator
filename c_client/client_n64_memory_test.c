/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Substitute only the HTTP boundary; use the real Client RAM preparation/sender. */
#define integral_api_download_n64_runtime_save memory_test_download
#define integral_api_reject_n64_preflight memory_test_reject
#define integral_api_n64_media_state memory_test_media_state
#include "client_room_n64.c"
#undef integral_api_download_n64_runtime_save
#undef integral_api_reject_n64_preflight
#undef integral_api_n64_media_state
#include <assert.h>
static int invalid_save_slot;
static int reject_calls, reject_result, download_result;
static bool media_terminal;
int memory_test_media_state(const char *server, const char *token, const char *session,
                           int recover, IntegralApiHeartbeatStatus *out, char *error, size_t size)
{
    (void)server;(void)token;(void)recover;(void)error;(void)size;
    memset(out,0,sizeof(*out));strcpy(out->lifecycle_kind,"media");
    copy_text(out->lifecycle_session_id,sizeof(out->lifecycle_session_id),session);
    strcpy(out->lifecycle_status,media_terminal?"CANCELLED":"RECOVERING");
    if(media_terminal) strcpy(out->termination_reason,"preflight_failed");
    return 0;
}
int memory_test_reject(const char *server, const char *token, const char *session,
                      const char *code, char *error, size_t size)
{
    (void)server; (void)token; (void)error; (void)size;
    assert(!strcmp(session,"client_memory_test") && !strcmp(code,"12345"));
    reject_calls++;
    return reject_result;
}

int memory_test_download(const char *server, const char *token, const char *session,
                         const char *kind, unsigned char *out, size_t capacity,
                         size_t *size, int *revision, char *error, size_t error_size)
{
    (void)server; (void)token; (void)error; (void)error_size;
    if (download_result) return download_result;
    assert(!strcmp(session,"client_memory_test"));
    assert(capacity==TRANSFER_SAV_MAX);
    assert(!strcmp(kind,"host-gb") || !strcmp(kind,"remote-gb"));
    memset(out,!strcmp(kind,"host-gb")?0x17:0x29,TRANSFER_SAV_MAX);
    memset(out+131072,0,48);
    out[131112]=0x80; out[131113]=0x43; out[131114]=0x6d; out[131115]=0x38;
    *size=TRANSFER_SAV_MAX; *revision=!strcmp(kind,"host-gb")?7:9;
    if (invalid_save_slot == (!strcmp(kind,"host-gb")?1:2)) {
        memset(out,0,32768); *size=32768;
    }
    return 0;
}

static void retired_files_test(void)
{
    LoginState login={0};IntegralRoomContext state={.login=&login};
    char id[80];snprintf(id,sizeof(id),"cleanup_test_%lu",(unsigned long)getpid());
    strcpy(state.n64.n64_runtime_media_session_id,id);
    char session[1024],transfer[1024],path[1200];
    assert(format_runtime_session_path(session,sizeof(session),INTEGRAL_N64_RUNTIME_MEDIA_DIR,id,NULL));
    assert(format_runtime_session_path(transfer,sizeof(transfer),INTEGRAL_N64_RUNTIME_MEDIA_DIR,id,"transfer"));
    assert(n64_transfer_path_type(session,true)==0);
    assert(!ensure_private_runtime_directory("runtime"));
    assert(!ensure_private_runtime_directory(INTEGRAL_N64_RUNTIME_MEDIA_DIR));
    assert(!ensure_private_runtime_directory(session));
    assert(!ensure_private_runtime_directory(transfer));
    for(size_t i=0;i<sizeof(n64_retired_transfer_files)/sizeof(*n64_retired_transfer_files);i++) {
        snprintf(path,sizeof(path),"%s/%s",transfer,n64_retired_transfer_files[i]);
        FILE *f=fopen(path,"wb");assert(f);fputs("old save",f);assert(!fclose(f));
    }
    state.n64.n64_runtime_media_host_pid=1;
    assert(!remove_retired_n64_transfer_files(&state));
    assert(n64_transfer_path_type(path,false)==1);
    state.n64.n64_runtime_media_host_pid=0;
    /* A directory at an expected file path must abort without deleting any files. */
    assert(!remove(path));assert(!ensure_private_runtime_directory(path));
    assert(!remove_retired_n64_transfer_files(&state));assert(!rmdir(path));
    char protected_path[1200];snprintf(protected_path,sizeof(protected_path),"%s/slot1.sav.merge.part",transfer);
    FILE *f=fopen(protected_path,"wb");assert(f);fputs("unresolved recovery",f);fclose(f);
    assert(remove_retired_n64_transfer_files(&state));
    assert(remove_retired_n64_transfer_files(&state));
    for(size_t i=0;i<sizeof(n64_retired_transfer_files)/sizeof(*n64_retired_transfer_files);i++) {
        snprintf(path,sizeof(path),"%s/%s",transfer,n64_retired_transfer_files[i]);
        assert(n64_transfer_path_type(path,false)==0);
    }
    assert(n64_transfer_path_type(protected_path,false)==1);
    assert(!remove(protected_path));assert(!rmdir(transfer));assert(!rmdir(session));
    strcpy(state.n64.n64_runtime_media_session_id,"../invalid");
    assert(!remove_retired_n64_transfer_files(&state));
    puts("Retired ROOM SAV cleanup, live-process/path/type rejection, recovery preservation PASS");
}

static void reap_test(const char *self)
{
    for (int exit_code=0;exit_code<=5;exit_code+=5) {
        LoginState login={0};AppScreen screen=SCREEN_N64_ROOM;
        IntegralRoomContext state={.login=&login,.screen=&screen};
        strcpy(state.n64.n64_runtime_media_launched_session_id,"previous-session");
#ifdef _WIN32
        const char *args[]={self,"--exit",exit_code?"5":"0",NULL};
        state.n64.n64_runtime_media_host_pid=integral_windows_spawnv(_P_NOWAIT,self,args);
        assert(state.n64.n64_runtime_media_host_pid!=-1);
#else
        (void)self;
        pid_t pid=fork();assert(pid>=0);
        if (!pid) _exit(exit_code);
        state.n64.n64_runtime_media_host_pid=pid;
#endif
        for (unsigned i=0;i<200 && state.n64.n64_runtime_media_host_pid;i++) {
            poll_n64_room_host_process(&state);SDL_Delay(10);
        }
        assert(!state.n64.n64_runtime_media_host_pid);
        assert(!state.n64.n64_runtime_media_launched_session_id[0]);
        strcpy(state.n64.n64_runtime_media_session_id,"next-unused-session");
        assert(remove_retired_n64_transfer_files(&state));
    }
    puts("Normal/abnormal child reap clears ownership; next-session cleanup accepted PASS");
}

int main(int argc,char **argv)
{
    if (argc==3 && !strcmp(argv[1],"--exit")) return atoi(argv[2]);
    if(argc==3 && !strcmp(argv[1],"--receive")) {
        int fd=atoi(argv[2]); TransferSavPair pair;
        assert(!transfer_sav_receive(transfer_sav_pipe_read,&fd,"client_memory_test",&pair));
        transfer_sav_close(fd);
        assert(pair.lengths[0]==TRANSFER_SAV_MAX && pair.lengths[1]==TRANSFER_SAV_MAX);
        assert(pair.revisions[0]==7 && pair.revisions[1]==9);
        assert(pair.saves[0].data[32767]==0x17 && pair.saves[1].data[32767]==0x29);
        transfer_sav_pair_clear(&pair);
        return 0;
    }
    retired_files_test();reap_test(argv[0]);
    LoginState login={0}; IntegralRoomContext state={.login=&login};
    strcpy(state.n64.n64_runtime_media_session_id,"client_memory_test");
    char rom_path[160]; snprintf(rom_path,sizeof(rom_path),"runtime/memory-test-%lu.gbc",(unsigned long)getpid());
    unsigned char header[0x150]={0};header[0x147]=0x10;header[0x149]=4;
    FILE *rom=fopen(rom_path,"wb");assert(rom);
    assert(fwrite(header,1,sizeof(header),rom)==sizeof(header));assert(!fclose(rom));
    const char *roms[2]={rom_path,rom_path};
    for (invalid_save_slot=1;invalid_save_slot<=2;invalid_save_slot++) {
        assert(!prepare_n64_transfer_saves(&state,roms));
        char message[100];snprintf(message,sizeof(message),"SLOT%d: RUN IN LOCAL, EXIT NORMALLY, THEN RETRY.",invalid_save_slot);
        assert(!strcmp(login.status,message));
        assert(!state.n64.n64_runtime_media_host_pid);
        assert(state.n64.preflight_failed);
    }
    for (int failure=0; failure<2; failure++) {
        AppScreen screen=SCREEN_N64_ROOM;state.screen=&screen;
        state.common.room_number=65;
        strcpy(state.n64.n64_runtime_media_session_id,"client_memory_test");
        strcpy(state.n64.lifecycle_room_code,"12345");
        reject_result=failure?-1:0;
        state.n64.preflight_failed=true;
        state.common.room_ready_self=state.common.room_ready_peer=true;
        reject_n64_preflight(&state);
        assert(screen==SCREEN_N64_ROOM && !state.n64.terminal_pending);
        assert(!state.common.room_ready_self && !state.common.room_ready_peer);
        for (Uint32 now=5000;now<60000;now+=5000) {
            assert(poll_n64_runtime_media_transport(&state,now));
            assert(!state.n64.n64_runtime_media_host_pid);
        }
        SDL_KeyboardEvent key={0};key.keysym.sym=SDLK_TAB;
        state.common.room_selected=0;handle_n64_room_key(&state,&key);
        assert(state.common.room_selected==1);
        assert(reject_calls==failure+1);
    }
    /* HTTP absence/invalid payload blocks; transport/5xx/429 remains retryable. */
    strcpy(state.n64.n64_runtime_media_session_id,"client_memory_test");
    for (int i=0;i<5;i++) {
        const int results[]={404,-2,-1,503,429};download_result=results[i];
        assert(!prepare_n64_transfer_saves(&state,roms));
        assert(state.n64.preflight_failed==(i<2));
    }
    download_result=0;state.n64.preflight_failed=false;
    {
        AppScreen screen=SCREEN_N64_ROOM;state.screen=&screen;
        reset_n64_runtime_media_connection(&state);
        strcpy(state.n64.n64_runtime_media_session_id,"client_memory_test");
        media_terminal=false;reconnect_n64_transport(&state,1000);
        assert(state.n64.n64_runtime_media_reconnect_deadline && !state.n64.preflight_failed);
        assert(!strcmp(state.n64.n64_runtime_media_session_id,"client_memory_test"));
        media_terminal=true;reconnect_n64_transport(&state,2000);
        assert(screen==SCREEN_N64_ROOM && state.n64.preflight_failed);
        assert(!state.n64.n64_runtime_media_reconnect_deadline);
        assert(request_n64_runtime_media_session(&state));
        assert(!state.n64.n64_runtime_media_session_id[0]);
        strcpy(state.n64.n64_runtime_media_session_id,"client_memory_test");
        state.n64.preflight_failed=false;
    }
    invalid_save_slot=0;
    N64TransferSend *send=prepare_n64_transfer_saves(&state,roms);assert(send);
    assert(!remove(rom_path));
    int fd[2];assert(!transfer_sav_pipe(fd));send->fd=fd[1];
    char number[32];snprintf(number,sizeof(number),"%d",fd[0]);
#ifdef _WIN32
    const char *args[]={argv[0],"--receive",number,NULL};
    intptr_t child=integral_windows_spawnv(_P_NOWAIT,argv[0],args);assert(child!=-1);
#else
    pid_t child=fork();assert(child>=0);
    if(!child) {transfer_sav_close(fd[1]);execl(argv[0],argv[0],"--receive",number,(char*)NULL);_exit(127);}
#endif
    transfer_sav_close(fd[0]);
    SDL_Thread *sender=SDL_CreateThread(send_n64_transfer_saves,"memory-test",send);assert(sender);
    int result;SDL_WaitThread(sender,&result);assert(!result);
#ifdef _WIN32
    assert(WaitForSingleObject((HANDLE)child,10000)==WAIT_OBJECT_0);
    DWORD status;assert(GetExitCodeProcess((HANDLE)child,&status));CloseHandle((HANDLE)child);assert(status==0);
#else
    int status;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && !WEXITSTATUS(status));
#endif
    puts("Client download-to-memory, sender thread, inherited Runtime receiver PASS");
    return 0;
}

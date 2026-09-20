/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_save_sync.h"
#include "client_save_outbox.h"
#include "client_file_io.h"
#include "http_client.h"
#include <assert.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#include <process.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* Only the API is mocked. Files, hashes, atomic replacement, outbox and both
 * native OS monitor implementations are real. Each monitor case gets its own
 * process/cwd; no ROM, real SAV, credential or real server is used. */
static const unsigned char original[]={1,2,3,4}, changed[]={5,6,7,8};
static unsigned uploads,replays,stops,completes,cancels,heartbeats,removed,recoveries;
static unsigned expected_count=1;
static bool upload_fail,download_fail,stop_fail,complete_fail,monitor_case;
static const char *mode="";
static char request_seen[192];
static unsigned uncertain_calls, committed_revision;
static bool lose_response;
static char first_request[192];
static int uncertain_put(int revision, const unsigned char *data, size_t size,
                         const char *request, int *next, char *error, size_t ec)
{
    assert(size == 4);
    uncertain_calls++;
    if (!committed_revision) {
        assert(revision == 7 && !memcmp(data, changed, 4));
        snprintf(first_request, sizeof(first_request), "%s", request);
        committed_revision = 8;
    }
    if (revision == 7) {
        assert(!strcmp(request, first_request) && !memcmp(data, changed, 4));
        *next = 8; /* server's idempotent replay */
    } else {
        assert(revision == 8 && memcmp(data, changed, 4) && strcmp(request, first_request));
        assert(committed_revision == 8);
        committed_revision = *next = 9;
    }
    if (lose_response) {
        snprintf(error, ec, "response lost after commit");
        return -1;
    }
    return 0;
}
#ifdef _WIN32
static volatile LONG finished;
static void finish(void) { InterlockedExchange(&finished,1); }
#else
static void finish(void) {}
#endif
static void pause_ms(unsigned ms)
{
#ifdef _WIN32
    Sleep(ms);
#else
    usleep(ms*1000u);
#endif
}
static void identity(const char *server,const char *token)
{ assert(!strcmp(server,"test-server") && !strcmp(token,"test-token")); }
static void fence(const char *server,const char *token,const char *game,long long value)
{ identity(server,token);assert(!strcmp(game,"game-test") && value==41); }
void client_save_log(const char *event,const char *format,...)
{
    (void)format;
    if(!monitor_case)return;
    if(!strcmp(event,"session_save_removed") && ++removed==expected_count)finish();
    if(!strcmp(event,"mobile_result_rejected"))finish();
    if(!strcmp(event,"mobile_complete_ok"))finish();
    if(!strcmp(event,"game_stop_withheld_for_recovery"))finish();
    if(!strcmp(event,"local_game_stop_failed"))finish();
    /* Failed uploads preserve working SAVs, so no session_save_removed follows. */
    if(!strcmp(event,"local_game_stop_ok") && !strcmp(mode,"monitor-upload-fail"))finish();
    if(!strcmp(event,"save_recovery_record_written") && ++recoveries==expected_count &&
       (!strcmp(mode,"monitor-expired") || !strcmp(mode,"monitor-lost")))finish();
    if(!strcmp(event,"mobile_complete_retry") && completes==3)finish();
}
int integral_api_download_save(const char *server,const char *token,const char *id,
    unsigned char *out,size_t cap,size_t *size,int *revision,char *error,size_t ec)
{
    identity(server,token);assert(id[0] && cap>=sizeof(original));
    if(download_fail){snprintf(error,ec,"download failed");return -1;}
    memcpy(out,original,sizeof(original));*size=sizeof(original);*revision=7;return 0;
}
int integral_api_upload_save_fenced(const char *server,const char *token,const char *id,
    int revision,const unsigned char *data,size_t size,const char *game,long long value,
    const char *request,int *next,char *error,size_t ec)
{
    fence(server,token,game,value);
    if (!strcmp(mode, "inflight")) return uncertain_put(revision, data, size, request, next, error, ec);
    assert(!strncmp(id,"save",4) && revision==7);
    unsigned char expected[sizeof(changed)];memcpy(expected,changed,sizeof(expected));
    expected[0]+=(unsigned char)atoi(id+4);
    assert(size==sizeof(expected) && !memcmp(data,expected,size));
    if(monitor_case && (!strcmp(mode,"monitor-mobile") || !strcmp(mode,"monitor-complete-fail"))) {
        assert(local_file_exists("child-ended")); /* no Mobile upload during execution */
    }
    assert(!strncmp(request,"save-save",9));uploads++;
    snprintf(request_seen,sizeof(request_seen),"%s",request);
    if(upload_fail){snprintf(error,ec,"revision conflict");return -1;}
    *next=revision+1;return 0;
}
int integral_api_upload_save_with_request_id(const char *server,const char *token,const char *id,
    int revision,const unsigned char *data,size_t size,const char *request,
    int *next,char *error,size_t ec)
{
    identity(server,token);
    if (!strcmp(mode, "inflight")) return uncertain_put(revision, data, size, request, next, error, ec);
    assert(!strcmp(id,"save0") && revision==7);
    assert(size==sizeof(changed) && !memcmp(data,changed,size));
    assert(!strcmp(request,request_seen));replays++;
    if(upload_fail){snprintf(error,ec,"revision conflict");return -1;}
    *next=8;return 0;
}
int integral_api_stop_local_game(const char *server,const char *token,const char *game,
    long long value,char *error,size_t ec)
{
    fence(server,token,game,value);stops++;
    if(stop_fail){snprintf(error,ec,"stop failed");return -1;}
    return 0;
}
int integral_api_complete_mobile_session(const char *server,const char *token,const char *mobile,
    const char *game,long long value,char *error,size_t ec)
{
    fence(server,token,game,value);assert(strstr(mobile,"mobile_") && uploads==1);
    completes++;
    if(complete_fail){snprintf(error,ec,"complete failed");return -1;}
    return 0;
}
int integral_api_cancel_mobile_session(const char *server,const char *token,const char *mobile,
    const char *game,long long value,const char *reason,char *error,size_t ec)
{
    fence(server,token,game,value);assert(mobile[0] && reason[0] && !uploads);
    (void)error;(void)ec;cancels++;return 0;
}
int integral_api_heartbeat_game(const char *server,const char *token,const char *game,
    long long value,char *error,size_t ec)
{
    fence(server,token,game,value);heartbeats++;
    snprintf(error,ec,"%s",!strcmp(mode,"monitor-expired")?"expired":"network unreachable");
    return -1;
}
int integral_api_heartbeat_mobile_session(const char *server,const char *token,const char *mobile,
    const char *game,long long value,char *error,size_t ec)
{ assert(mobile[0]);return integral_api_heartbeat_game(server,token,game,value,error,ec); }

static void receipt(const LocalSyncSlot *s,bool clean)
{
    char hash[65],body[512];assert(sha256_file_hex(s->save_path,hash,sizeof(hash))==0);
    int n=snprintf(body,sizeof(body),"schema_version=1\nclean_exit=%u\nbattery_flush=1\nsav_size=4\nsav_sha256=%s\n",clean?1:0,hash);
    assert(n>0 && (size_t)n<sizeof(body));
    assert(atomic_replace_binary_file(s->mobile_result_path,(unsigned char *)body,(size_t)n)==0);
}
static void init_slot(LocalSyncSlot *s,unsigned index,bool mobile)
{
    memset(s,0,sizeof(*s));snprintf(s->save_id,sizeof(s->save_id),"save%u",index);
    s->revision=7;strcpy(s->last_hash,"old");
    assert(ensure_private_runtime_directory("runtime")==0);
    if(mobile) {
        strcpy(s->mobile_session_id,"mobile_0123456789abcdef0123456789abcdef");
        snprintf(s->mobile_runtime_dir,sizeof(s->mobile_runtime_dir),"runtime/gb-mobile/%s",s->mobile_session_id);
        assert(ensure_private_runtime_directory("runtime/gb-mobile")==0);
        assert(ensure_private_runtime_directory(s->mobile_runtime_dir)==0);
        snprintf(s->save_path,sizeof(s->save_path),"%s/working.sav",s->mobile_runtime_dir);
        snprintf(s->mobile_result_path,sizeof(s->mobile_result_path),"%s/runtime-result.txt",s->mobile_runtime_dir);
        s->mobile_guard=true;s->authoritative_size=4;
    } else {
        snprintf(s->save_path,sizeof(s->save_path),"runtime/slot%u.sav",index);
    }
    unsigned char bytes[sizeof(changed)];memcpy(bytes,changed,sizeof(bytes));bytes[0]+=(unsigned char)index;
    assert(atomic_replace_binary_file(s->save_path,bytes,sizeof(bytes))==0);
    if(mobile)receipt(s,true);
}
static unsigned pending_records(char *first,size_t cap)
{
    unsigned count=0;DIR *d=opendir(INTEGRAL_SAVE_OUTBOX_DIR);struct dirent *e;
    if(!d)return 0;
    while((e=readdir(d))) {
        size_t n=strlen(e->d_name);
        if(n>8 && (!strcmp(e->d_name+n-8,".pending") || !strcmp(e->d_name+n-9,".inflight"))) {
            if(!count && first)snprintf(first,cap,"%s/%s",INTEGRAL_SAVE_OUTBOX_DIR,e->d_name);
            count++;
        }
    }
    closedir(d);return count;
}
static void test_storage(void)
{
    LocalSyncSlot s;init_slot(&s,0,false);IntegralConfigRomSlot slot={0};strcpy(slot.save_id,"save0");
    char status[160];download_fail=true;
    assert(integral_save_download("test-server","test-token",&slot,s.save_path,&s,status,sizeof(status))==-1);
    unsigned char data[8];size_t size;assert(read_binary_file(s.save_path,data,sizeof(data),&size)==0 && !memcmp(data,changed,4));
    download_fail=false;
    assert(integral_save_download("test-server","test-token",&slot,s.save_path,&s,status,sizeof(status))==0);
    assert(s.revision==7 && strlen(s.last_hash)==64);
    assert(upload_changed_save("test-server","test-token","game-test",41,&s,false) && !uploads);
    assert(atomic_replace_binary_file(s.save_path,changed,4)==0);
    upload_fail=true;
    assert(!upload_changed_save("test-server","test-token","game-test",41,&s,false));
    assert(s.revision==7 && pending_records(NULL,0)==1);
    assert(upload_changed_save("test-server","test-token","game-test",41,&s,true));
    char record[1024],saved[1024];assert(pending_records(record,sizeof(record))==1);
    unsigned char *bytes;size_t n;assert(read_binary_file_alloc(record,&bytes,&n,8192)==0);
    char line[4096];memcpy(line,bytes,sizeof(line));free(bytes);
    assert(extract_tsv_field(line,"path",saved,sizeof(saved)));
    assert(!strstr(line,"test-token")); /* credential must not be persisted */
    unsigned pending=0;
    assert(save_upload_outbox_pending("test-server", "save0", "", NULL, 0));
    assert(!save_upload_outbox_pending("other-server", "save0", "", NULL, 0));
    assert(!save_upload_outbox_pending("test-server", "other-save", "", NULL, 0));
    assert(!save_upload_outbox_pending("test-server", "save0", "another-account", NULL, 0));
    char reason_text[160];
    assert(save_upload_outbox_pending("test-server", "save0", "", reason_text, sizeof(reason_text)));
    assert(strstr(reason_text, "revision conflict"));
    assert(replay_save_upload_outbox("other-server","test-token","save0",&pending, "")==0 && pending==0 && !replays);
    assert(replay_save_upload_outbox("test-server","test-token","other-save",&pending, "")==0 && pending==0 && !replays);
    assert(replay_save_upload_outbox("test-server","test-token","save0",&pending, "")==0 && pending==1 && replays==1);
    assert(local_file_exists(saved) && local_file_exists(record));
    upload_fail=false;
    assert(replay_save_upload_outbox("test-server","test-token","save0",&pending, "")==1 && !pending && replays==2);
    assert(!save_upload_outbox_pending("test-server", "save0", "", NULL, 0));
    assert(!local_file_exists(saved) && !local_file_exists(record));
    assert(!replay_save_upload_outbox("test-server","test-token","save0",&pending, "") && !pending && replays==2);
    assert(!upload_changed_save("test-server","test-token","game-test",41,&s,true));
    assert(!s.preserve_save_path); /* missing source cannot be recorded as recovered */
    init_slot(&s,0,true);char reason[160];
    assert(mobile_runtime_result_allows_commit(&s,reason,sizeof(reason)));
    receipt(&s,false);assert(!mobile_runtime_result_allows_commit(&s,reason,sizeof(reason)));
    receipt(&s,true);assert(atomic_replace_binary_file(s.save_path,original,4)==0);
    assert(!mobile_runtime_result_allows_commit(&s,reason,sizeof(reason)));
    assert(remove(s.mobile_result_path)==0);
    assert(!mobile_runtime_result_allows_commit(&s,reason,sizeof(reason)));
    assert(!cleanup_mobile_runtime_directory("runtime/gb-mobile/../outside"));
    puts("SAV storage: download, unchanged/changed hash, revision/fence, failed upload, replay/idempotency, receipt PASS");
}

static void test_inflight(void)
{
    /* Lost 200 acknowledgment, later SAV, and loss of all in-memory state.
     * No ROM or real account is used; the fake authority enforces revision. */
    for (unsigned restart = 0; restart < 2; ++restart) {
        LocalSyncSlot slot;
        init_slot(&slot, 0, false);
        strcpy(slot.account, "owner");
        uncertain_calls = committed_revision = 0;
        lose_response = true;
        assert(!upload_changed_save("test-server", "test-token", "game-test", 41, &slot, false));
        assert(committed_revision == 8 && slot.revision == 7);
        const unsigned char newer[] = {9,10,11,12};
        assert(atomic_replace_binary_file(slot.save_path, newer, sizeof(newer)) == 0);
        assert(!upload_changed_save("test-server", "test-token", "game-test", 41, &slot, false));
        assert(committed_revision == 8); /* the new bytes were NOT sent */
        char reason[160];
        assert(save_upload_outbox_pending("test-server", "save0", "owner", reason, sizeof(reason)));
        assert(strstr(reason, "response lost after commit"));
        assert(!save_upload_outbox_pending("test-server", "save0", "other", reason, sizeof(reason)));
        lose_response = false;
        if (restart) {
            memset(&slot, 0, sizeof(slot));
            unsigned pending = 99;
            assert(replay_save_upload_outbox("test-server", "test-token", "save0", &pending, "owner") == 1);
            assert(pending == 0);
        } else {
            assert(upload_changed_save("test-server", "test-token", "game-test", 41, &slot, true));
            assert(slot.revision == 9 && !slot.preserve_save_path);
        }
        assert(committed_revision == 9 && uncertain_calls == 4);
        assert(!save_upload_outbox_pending("test-server", "save0", "owner", reason, sizeof(reason)));
    }
    puts("SAV in-flight: lost ACK, identical retries, latest follows, restart, account isolation PASS");
}

static void run_monitor(const char *self)
{
    monitor_case=true;
    bool mobile=strstr(mode,"mobile") || !strcmp(mode,"monitor-reject") || !strcmp(mode,"monitor-complete-fail");
    bool heartbeat=!strcmp(mode,"monitor-expired") || !strcmp(mode,"monitor-lost");
    expected_count=mobile?1:2;
    LocalSyncSlot slots[2];for(unsigned i=0;i<expected_count;i++)init_slot(&slots[i],i,mobile);
    if(!strcmp(mode,"monitor-reject"))receipt(&slots[0],false);
    if(heartbeat)for(unsigned i=0;i<expected_count;i++)assert(sha256_file_hex(slots[i].save_path,slots[i].last_hash,sizeof(slots[i].last_hash))==0);
    upload_fail=!strcmp(mode,"monitor-upload-fail") || !strcmp(mode,"monitor-withheld");
    complete_fail=!strcmp(mode,"monitor-complete-fail");stop_fail=!strcmp(mode,"monitor-stop-fail");
    if(!strcmp(mode,"monitor-withheld"))assert(atomic_replace_binary_file(INTEGRAL_SAVE_OUTBOX_DIR,original,4)==0);
    char paths[2][INTEGRAL_CONFIG_PATH_MAX],mobile_dir[INTEGRAL_CONFIG_PATH_MAX];
    for(unsigned i=0;i<expected_count;i++)strcpy(paths[i],slots[i].save_path);
    strcpy(mobile_dir,slots[0].mobile_runtime_dir);
#ifdef _WIN32
    intptr_t child=_spawnl(_P_NOWAIT,self,self,"--child",heartbeat?"120":"2",NULL);assert(child!=-1);
    start_save_sync_thread(child,"test-server","test-token","game-test",41,slots,expected_count,true);
    memset(slots,0,sizeof(slots)); /* monitor must already own copies */
    bool observed=false;
    for(unsigned i=0;i<1400;i++) {
        if(InterlockedCompareExchange(&finished,0,0)){observed=true;break;}
        pause_ms(50);
    }
    assert(observed);
    if(mobile && !complete_fail && strcmp(mode,"monitor-reject")) {
        for(unsigned i=0;i<100 && local_file_exists(paths[0]);i++)pause_ms(10);
    }
#else
    (void)self;
    pid_t child=fork();assert(child>=0);
    if(!child){sleep(heartbeat?120:2);assert(write_binary_file("child-ended",original,sizeof(original))==0);_exit(0);}
    monitor_save_sync_process(child,"test-server","test-token","game-test",41,slots,expected_count,true);
#endif
    if(heartbeat) {
        assert(!uploads && !stops && !completes && !cancels);
        assert(heartbeats==(!strcmp(mode,"monitor-expired")?1u:3u));
        assert(pending_records(NULL,0)==expected_count);
        for(unsigned i=0;i<expected_count;i++)assert(local_file_exists(paths[i]));
    } else if(!strcmp(mode,"monitor-reject")) {
        assert(!uploads && !completes && cancels==1 && pending_records(NULL,0)==1);
        assert(local_file_exists(paths[0]));
    } else if(complete_fail) {
        assert(uploads==1 && completes==3 && !stops && !cancels && local_file_exists(paths[0]));
    } else if(!strcmp(mode,"monitor-withheld")) {
        assert(!stops && !uploads && local_file_exists(paths[0]));
    } else if(stop_fail) {
        assert(stops==1 && local_file_exists(paths[0]) && local_file_exists(paths[1]));
    } else {
        assert(mobile ? completes==1 && !stops && uploads==1 : stops==1 && uploads>=expected_count);
        for(unsigned i=0;i<expected_count;i++)assert(local_file_exists(paths[i]) == upload_fail);
        if(mobile) {
            for(unsigned i=0;i<100 && local_file_exists(mobile_dir);i++)pause_ms(10);
            assert(!local_file_exists(mobile_dir));
        }
        if(upload_fail)assert(pending_records(NULL,0)==expected_count);
    }
    printf("SAV native monitor: %s PASS\n",mode);
}
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--child")){pause_ms((unsigned)atoi(argv[2])*1000u);assert(write_binary_file("child-ended",original,sizeof(original))==0);return 0;}
    assert(argc==2);mode=argv[1];
    if(!strcmp(mode,"storage"))test_storage();
    else if(!strcmp(mode,"inflight"))test_inflight();
    else {
#ifdef _WIN32
        char self[32768];DWORD n=GetModuleFileNameA(NULL,self,sizeof(self));assert(n>0 && n<sizeof(self));
        run_monitor(self);
#else
        run_monitor(argv[0]);
#endif
    }
    return 0;
}

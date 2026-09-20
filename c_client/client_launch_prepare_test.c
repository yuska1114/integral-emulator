/* SPDX-FileCopyrightText: 2026 yuska (GitHub: @yuska1114) */
/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "client_launch_gb_mobile.h"
#include "client_launch_n64_local.h"
#include "client_file_io.h"
#include "http_client.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

/* No real server, ROM or SAV: exercise orchestration against explicit boundaries.
 * Only N64's dummy child/monitor marker use the OS, inside a disposable cwd. */
static const char *self;
static char status[160], sequence[256];
static unsigned stopped, cancelled, cleaned, launched, freed, transfers, creates, dirs;
static int failure, create_error;
static unsigned expected_exit = 42;
static bool reuse_request;
enum { FAIL_RECOVER=1, FAIL_CREATE, FAIL_INVALID_ID, FAIL_DIRECTORY, FAIL_DOWNLOAD, FAIL_MANIFEST, FAIL_SIZE,
       FAIL_KEYMAP, FAIL_PATHS, FAIL_TRANSFER, FAIL_WRITE_REQUEST, FAIL_SAVE_PATH, FAIL_SESSION_PATH, FAIL_SESSION_DIRECTORY };
static void step(char c) { size_t n=strlen(sequence); assert(n+1<sizeof(sequence)); sequence[n]=c; sequence[n+1]=0; }
static void reset(void)
{
    stopped=cancelled=cleaned=launched=freed=transfers=creates=dirs=0;
    failure=create_error=0; reuse_request=false;
    strcpy(status,"before"); sequence[0]=0;
}
static void identity(const char *server,const char *token)
{ assert(!strcmp(server,"server") && !strcmp(token,"token")); }
int ensure_private_runtime_directory(const char *p)
{ (void)p; dirs++; return failure==FAIL_DIRECTORY || (failure==FAIL_SESSION_DIRECTORY && dirs>2) ? -1 : 0; }
bool runtime_session_id_is_path_safe(const char *s)
{ return strcmp(s,"../invalid")!=0; }
bool format_runtime_session_path(char *out,size_t cap,const char *root,const char *id,const char *rel)
{
    snprintf(out,cap,"%s/%s%s%s",root,id,rel?"/":"",rel?rel:"");
    return failure!=FAIL_SESSION_PATH;
}
void make_safe_outbox_token(const char *s,char *out,size_t cap) { snprintf(out,cap,"%s",s); }
void make_safe_n64_runtime_save_name(const char *s,char *out,size_t cap) { snprintf(out,cap,"%s",s); }
int atomic_replace_binary_file(const char *p,const unsigned char *data,size_t size)
{
    assert(strstr(p,"gb-mobile-create/")); assert(size && !strncmp((const char *)data,"mobile-create:",14));
    step('w'); return failure==FAIL_WRITE_REQUEST ? -1 : 0;
}
int read_binary_file_alloc(const char *p,unsigned char **out,size_t *size,size_t limit)
{
    *out=NULL; *size=0;
    if (strstr(p,"gb-mobile-create/")) {
        if (!reuse_request) return -1;
        *size=5; *out=malloc(5); assert(*out); memcpy(*out,"retry",5); step('r'); return 0;
    }
    assert(strstr(p,"working.sav") && limit==INTEGRAL_MAX_SAVE_BYTES);
    if(failure==FAIL_SIZE) return -1;
    *size=32; *out=calloc(1,*size); assert(*out); step('s'); return 0;
}
static bool recover(void *c,const char *id)
{ assert(c==status && id[0]); step('o'); return failure!=FAIL_RECOVER; }
static int download(void *c,const IntegralConfigRomSlot *slot,const char *p,LocalSyncSlot *sync)
{
    assert(c==status); step('d');
    if(failure==FAIL_DOWNLOAD) return -1;
    snprintf(sync->save_id,sizeof(sync->save_id),"%s",slot->save_id);
    snprintf(sync->save_path,sizeof(sync->save_path),"%s",failure==FAIL_SAVE_PATH?"wrong":p);
    strcpy(sync->last_hash,"hash"); sync->revision=9; return 0;
}
static bool rtc(void *c,char *out,size_t cap)
{ assert(c==status); step('t'); snprintf(out,cap,"-123"); return true; }
static void window_size(void *c,unsigned *w,unsigned *h)
{ assert(c==status); step('v'); *w=360; *h=480; }
static bool cleanup(const char *p)
{ assert(strstr(p,"runtime/gb-mobile/") && cancelled==1); cleaned++; step('x'); return true; }
static void redirect_output(void)
{ FILE *f=fopen("redirect.marker","wb"); assert(f); assert(fclose(f)==0); }
static void log_event(void *c,const char *event,const char *detail)
{
    assert(c==status && event && detail);
    if(!strcmp(event,"n64_runtime_started")) {
        assert(strstr(status,"STARTED") && strstr(detail,"transfer_slots=")); step('l');
    }
}
int integral_api_start_mobile_session_contract(const char *server,const char *token,
    const char *save,const char *rom,const char *request_id,const char *scenario,
    char *mobile,size_t mc,char *game,size_t gc,long long *fence,
    IntegralMobileRuntimeContract *contract,char *error,size_t ec)
{
    identity(server,token); assert(!strcmp(save,"save0") && !strcmp(rom,"rom0") && !strcmp(scenario,"scenario"));
    assert(reuse_request ? !strcmp(request_id,"retry") : !strncmp(request_id,"mobile-create:",14));
    creates++; step('c'); memset(contract,0,sizeof(*contract)); snprintf(error,ec,"test");
    snprintf(mobile,mc,"%s",failure==FAIL_INVALID_ID?"../invalid":"mobile");
    snprintf(game,gc,"game"); *fence=41;
    return failure==FAIL_CREATE ? (create_error ? create_error : -1) : 0;
}
int integral_api_cancel_mobile_session(const char *server,const char *token,const char *mobile,
    const char *game,long long fence,const char *reason,char *error,size_t ec)
{
    identity(server,token); assert(mobile[0] && !strcmp(game,"game") && fence==41 && reason[0]);
    (void)error; (void)ec; cancelled++; step('c'); return 0;
}
void integral_mobile_runtime_contract_free(IntegralMobileRuntimeContract *c)
{ (void)c; freed++; step('f'); }
int integral_mobile_runtime_contract_write_manifest(const IntegralMobileRuntimeContract *c,
    const char *dir,const char *path,char *error,size_t ec)
{
    (void)c;(void)error;(void)ec; assert(strstr(path,dir)); step('m'); return failure==FAIL_MANIFEST?-1:0;
}
void integral_gb_mobile_start(const IntegralGbMobileLaunch *l,const IntegralGbMobileSession *s,
    char *out,size_t cap,bool (*clean)(const char *),void (*redirect)(void),
    void (*log)(void *,const char *,const char *),void *context)
{
    assert(context==status && clean==cleanup && redirect==redirect_output && log==log_event);
    identity(s->server,s->token);
    assert(s->fencing_token==41 && !strcmp(s->game_session_id,"game"));
    assert(!strcmp(s->mobile_session_id,"mobile") && !strcmp(s->scenario_id,"scenario"));
    assert(!strcmp(l->runtime,self) && !strcmp(l->rom,"same.gbc") && !strcmp(l->rtc_offset,"-123"));
    assert(!strcmp(l->window_width,"360") && !strcmp(l->window_height,"480"));
    assert(!strcmp(l->keys->slot1,"Z,X") && strstr(l->adapter_config,"adapter.bin"));
    assert(s->sync_slot->mobile_guard && s->sync_slot->authoritative_size==32);
    assert(s->sync_slot->revision==9 && !strcmp(s->sync_slot->last_hash,"hash"));
    assert(!strcmp(s->sync_slot->save_path,l->save) && !strcmp(s->sync_slot->save_id,"save0"));
    assert(!strcmp(s->sync_slot->mobile_result_path,l->result));
    assert(freed==1 && !cancelled && !cleaned); launched++; step('a');
    snprintf(out,cap,"MOBILE MODE STARTED");
}
int integral_api_start_game_with_saves(const char *server,const char *token,const char *mode,
    const char *const *ids,unsigned count,char *game,size_t cap,long long *fence,char *error,size_t ec)
{
    identity(server,token); assert(!strcmp(mode,INTEGRAL_EXECUTION_MODE_N64_RUNTIME_CLIENT));
    assert(count==transfers+1 && !strcmp(ids[0],"save0"));
    for(unsigned i=1;i<count;i++) { char id[16];snprintf(id,sizeof(id),"save%u",i);assert(!strcmp(ids[i],id)); }
    creates++; step('c'); snprintf(game,cap,"%s",failure==FAIL_INVALID_ID?"../invalid":"game");
    *fence=41; snprintf(error,ec,"test"); return failure==FAIL_CREATE?-1:0;
}
int integral_api_stop_local_game(const char *server,const char *token,const char *game,
    long long fence,char *error,size_t ec)
{
    identity(server,token); assert(game[0] && fence==41); (void)error;(void)ec;
    stopped++; step('q'); return 0;
}
static bool paths(IntegralN64LocalPaths *p)
{
    memset(p,0,sizeof(*p)); snprintf(p->frontend,sizeof(p->frontend),"%s",self);
    strcpy(p->core,"core");strcpy(p->video,"video");strcpy(p->audio,"audio");
    strcpy(p->input,"input");strcpy(p->rsp,"rsp");strcpy(p->data,"data");
    return failure!=FAIL_PATHS;
}
static int prepare_save(void *c,const IntegralConfigRomSlot *slot,const char *dir,LocalSyncSlot *s)
{ assert(strstr(dir,"n64-save")); return download(c,slot,dir,s); }
static int prepare_transfer(void *c,unsigned i,const char *dir,IntegralConfigRomSlot *slot,LocalSyncSlot *s)
{
    assert(strstr(dir,"transfer") && i<4);
    if(failure==FAIL_TRANSFER) return -1;
    char path[1024]; snprintf(path,sizeof(path),"%s/slot%u.sav",dir,i+1);
    return download(c,slot,path,s);
}
static bool keymap(const char *keys,char *out,size_t cap)
{ assert(!strcmp(keys,"keys"));step('k');snprintf(out,cap,"map");return failure!=FAIL_KEYMAP; }
static void handoff(const char *server,const char *token,const char *game,long long fence,
    const LocalSyncSlot *s,unsigned count,bool active)
{
    identity(server,token);assert(!strcmp(game,"game") && fence==41 && active && count==transfers+1);
    for(unsigned i=0;i<count;i++) {
        char id[16];snprintf(id,sizeof(id),"save%u",i);
        assert(!strcmp(s[i].save_id,id) && s[i].revision==9 && !strcmp(s[i].last_hash,"hash"));
        assert(!s[i].mobile_guard);
        for(unsigned j=0;j<i;j++) assert(strcmp(s[i].save_path,s[j].save_path));
    }
}
static void marker(void) { FILE *f=fopen("monitor.marker","wb");assert(f);assert(fclose(f)==0); }
#ifdef _WIN32
void start_save_sync_thread(intptr_t pid,const char *server,const char *token,const char *game,
    long long fence,const LocalSyncSlot *s,unsigned count,bool active)
{
    handoff(server,token,game,fence,s,count,active);
    assert(!strcmp(status,"before"));
    assert(WaitForSingleObject((HANDLE)pid,10000)==WAIT_OBJECT_0);
    DWORD code;assert(GetExitCodeProcess((HANDLE)pid,&code) && code==expected_exit);
    assert(CloseHandle((HANDLE)pid));marker();
}
#else
void monitor_save_sync_process(IntegralChildProcess pid,const char *server,const char *token,
    const char *game,long long fence,LocalSyncSlot *s,unsigned count,bool active)
{
    handoff(server,token,game,fence,s,count,active);
    int result; assert(waitpid(pid,&result,0)==pid && WIFEXITED(result) && WEXITSTATUS(result)==(int)expected_exit);
    FILE *f=fopen("redirect.marker","rb");assert(f);fclose(f);marker();
}
#endif
static void check_arguments(void)
{
    IntegralN64LocalLaunch l={"frontend","日本語 rom.z64","core","config","data","screenshots",
        "save dir","save-name","video","audio","input","rsp","transfer","map","utils", {NULL,NULL,NULL}};
    const char *expected[]={"frontend","--rom","日本語 rom.z64","--core","core","--config-dir","config",
        "--data-dir","data","--screenshot-dir","screenshots","--save-dir","save dir","--save-name",
        "save-name","--video","video","--audio","audio","--input","input","--rsp","rsp",
        "--transfer-storage","transfer","--controller1","keyboard","--controller-map1","map","--interactive","--hotkeys","utils",
        "--controller2","auto","--controller3","auto","--controller4","auto",NULL};
    const char *argv[INTEGRAL_N64_LOCAL_ARGV_CAPACITY+1];
    argv[INTEGRAL_N64_LOCAL_ARGV_CAPACITY]="canary";integral_n64_local_arguments(&l,argv);
    for(unsigned i=0;i<sizeof(expected)/sizeof(*expected);i++) assert(expected[i]?argv[i] && !strcmp(argv[i],expected[i]):argv[i]==NULL);
    l.extra_controller_maps[0]="map2";l.extra_controller_maps[1]="map3";l.extra_controller_maps[2]="map4";
    integral_n64_local_arguments(&l,argv);
    assert(!strcmp(argv[34],"--controller-map2") && !strcmp(argv[35],"map2"));
    assert(!strcmp(argv[38],"--controller-map3") && !strcmp(argv[39],"map3"));
    assert(!strcmp(argv[42],"--controller-map4") && !strcmp(argv[43],"map4") && argv[44]==NULL);
    assert(!strcmp(argv[INTEGRAL_N64_LOCAL_ARGV_CAPACITY],"canary"));
    char long_path[INTEGRAL_CONFIG_PATH_MAX];memset(long_path,'x',sizeof(long_path)-1);long_path[sizeof(long_path)-1]=0;
    l.rom=long_path;integral_n64_local_arguments(&l,argv);assert(argv[2]==long_path);
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--rom")) {assert(argc==38 && !strcmp(argv[29],"--interactive") && !strcmp(argv[30],"--hotkeys") && !strcmp(argv[36],"--controller4"));return 42;}
    (void)argv;
#ifdef _WIN32
    char executable[32768];DWORD n=GetModuleFileNameA(NULL,executable,sizeof(executable));assert(n>0 && n<sizeof(executable));self=executable;
#else
    self=argv[0];
#endif
    check_arguments();
    IntegralConfigRomSlot slots[5]={0};IntegralConfigKeys keys={0};strcpy(keys.slot1,"Z,X");
    for(unsigned i=0;i<5;i++) {snprintf(slots[i].save_id,sizeof(slots[i].save_id),"save%u",i);strcpy(slots[i].rom_id,"rom0");strcpy(slots[i].rom_path,"same.gbc");}
    IntegralGbMobileRequest mobile={.slot=&slots[0],.scenario_id="scenario",.runtime=self,.server="server",.token="token",
        .keys=&keys,.status=status,.status_size=sizeof(status),.context=status,.recover=recover,.download=download,
        .rtc=rtc,.window_size=window_size,.cleanup=cleanup,.redirect_output=redirect_output,.log=log_event};
    for(int f=0;f<=FAIL_SESSION_DIRECTORY;f++) {
        if(f==FAIL_KEYMAP||f==FAIL_PATHS||f==FAIL_TRANSFER)continue;
        reset();failure=f;integral_gb_mobile_run(&mobile);
        if(f==0) {assert(launched==1 && !strcmp(sequence,"owcdmsfvta"));}
        else {
            assert(!launched);
            bool acquired=f!=FAIL_RECOVER && f!=FAIL_CREATE && f!=FAIL_DIRECTORY && f!=FAIL_WRITE_REQUEST;
            assert(cancelled==(unsigned)acquired && freed==(unsigned)acquired);
            assert(cleaned==(unsigned)(f==FAIL_DOWNLOAD || f==FAIL_MANIFEST || f==FAIL_SIZE || f==FAIL_SAVE_PATH || f==FAIL_SESSION_DIRECTORY));
        }
    }
    reset();reuse_request=true;integral_gb_mobile_run(&mobile);assert(launched && !strcmp(sequence,"orcdmsfvta"));
    for(int code=-3;code<=-2;code++) {
        reset();failure=FAIL_CREATE;create_error=code;integral_gb_mobile_run(&mobile);assert(!cancelled && !freed);
        assert(strstr(status,code==-3?"ANOTHER LOGIN":"ABORTED"));
    }
    reset();mobile.runtime="./missing-runtime";integral_gb_mobile_run(&mobile);assert(!creates && !sequence[0]);
    puts("Mobile preparation: ordering, idempotency reuse, failures, guard/RTC/window PASS");
    IntegralN64LocalRequest n64={.slot=&slots[0],.server="server",.token="token",.keys="keys",.hotkeys="utils",
        .status=status,.status_size=sizeof(status),.context=status,.paths=paths,.recover=recover,
        .prepare_save=prepare_save,.prepare_transfer=prepare_transfer,.keymap=keymap,
        .redirect_output=redirect_output,.log=log_event};
    for(unsigned count=0;count<=4;count++) {
        reset();transfers=count;
        for(unsigned i=0;i<4;i++) n64.transfer[i]=i<count?&slots[i+1]:NULL;
        integral_n64_local_run(&n64);
#ifndef _WIN32
        int result;assert(waitpid(-1,&result,0)>0 && WIFEXITED(result) && WEXITSTATUS(result)==0);
#endif
        assert(!stopped && creates==1 && strstr(status,"STARTED"));
        FILE *f=fopen("monitor.marker","rb");assert(f);fclose(f);assert(remove("monitor.marker")==0);
    }
    const int failures[]={FAIL_RECOVER,FAIL_CREATE,FAIL_INVALID_ID,FAIL_DIRECTORY,FAIL_DOWNLOAD,FAIL_KEYMAP,FAIL_PATHS,FAIL_TRANSFER,FAIL_SESSION_PATH};
    for(unsigned i=0;i<sizeof(failures)/sizeof(*failures);i++) {
        reset();transfers=4;failure=failures[i];integral_n64_local_run(&n64);
        assert(stopped==(unsigned)(failure!=FAIL_RECOVER && failure!=FAIL_CREATE && failure!=FAIL_PATHS));
        assert(!strstr(status,"STARTED"));
    }
    reset();transfers=4;self="./missing-n64-runtime";expected_exit=127;
    integral_n64_local_run(&n64);
#ifdef _WIN32
    assert(stopped==1 && !strcmp(status,"N64_RUNTIME START FAILED"));
#else
    int result;assert(waitpid(-1,&result,0)>0 && WIFEXITED(result) && WEXITSTATUS(result)==0);
    assert(!stopped && strstr(status,"STARTED")); /* Existing asynchronous exec failure. */
    FILE *missing_marker=fopen("monitor.marker","rb");assert(missing_marker);fclose(missing_marker);
#endif
    reset();n64.slot=NULL;integral_n64_local_run(&n64);assert(!creates && !strcmp(status,"N64 ROM REQUIRED"));
    reset();n64.slot=&slots[0];slots[0].save_id[0]=0;integral_n64_local_run(&n64);assert(!creates && strstr(status,"REGISTER"));
    puts("N64 LOCAL: argv, preparation, zero/four Transfer Pak slots, process handoff, failures PASS");
    return 0;
}

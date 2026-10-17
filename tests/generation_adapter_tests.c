/* CC BY-NC-SA 4.0; see LICENSE.md.
 * Actual native/player adapters; mocked stock calls and QNX OS/Screen API.
 * ARM32 table bytes are exercised, but this is not an ARM ABI/loader test.
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stddef.h>
#include "cluster_h264_shm.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define M2_ADAPTER_SYNTAX_ONLY 1
typedef int pid_t;
typedef long ssize_t;
typedef long long off_t;
typedef void *screen_window_t;
typedef void *screen_buffer_t;
typedef CRITICAL_SECTION pthread_mutex_t;
typedef int pthread_mutexattr_t;
#define PTHREAD_MUTEX_RECURSIVE 1
#define O_CREAT 1
#define O_RDWR 2
#define O_RDONLY 0
#define O_NONBLOCK 4
#define O_EXCL 8
#define F_WRLCK 1
#define F_SETLK 2
#define F_SETFD 3
#define FD_CLOEXEC 1
#define PROT_READ 1
#define PROT_WRITE 2
#define MAP_SHARED 1
#define MAP_FAILED ((void*)-1)
struct flock { short l_type,l_whence; };
struct stat { off_t st_size; };
/* Names are isolated from CRT aliases. These OS mocks intentionally fail. */
#define open mock_open
#define close mock_close
#define read mock_read
static int mock_open(const char *p,int flags,...) { (void)p;(void)flags;return -1; }
static int mock_close(int fd) { (void)fd;return 0; }
static ssize_t mock_read(int fd,void *p,size_t n) { (void)fd;(void)p;(void)n;return -1; }
static int fcntl(int fd,int cmd,...) { (void)fd;(void)cmd;return -1; }
static int shm_unlink(const char *p) { (void)p;return -1; }
static int shm_open(const char *p,int flags,int mode) { (void)p;(void)flags;(void)mode;return -1; }
static int ftruncate(int fd,size_t n) { (void)fd;(void)n;return -1; }
static int fstat(int fd,struct stat *st) { (void)fd;(void)st;return -1; }
static void *mmap(void *p,size_t n,int prot,int flags,int fd,int off) { (void)p;(void)n;(void)prot;(void)flags;(void)fd;(void)off;return MAP_FAILED; }
static int munmap(void *p,size_t n) { (void)p;(void)n;return 0; }
static pid_t getpid(void) { return 1234; }
static int usleep(unsigned us) { (void)us; Sleep(0); return 0; }
static int kill(pid_t pid,int sig) { (void)pid;(void)sig; errno=ESRCH; return -1; }
static HANDLE callback_waiting;
static DWORD primary_thread;
static int pthread_mutex_lock(pthread_mutex_t *m) {
    if(callback_waiting && GetCurrentThreadId()!=primary_thread) SetEvent(callback_waiting);
    EnterCriticalSection(m);return 0;
}
static int pthread_mutex_unlock(pthread_mutex_t *m) { LeaveCriticalSection(m);return 0; }
static void log_line(const char *fmt,...) { (void)fmt; }
static cluster_h264_shm_t *g_cluster_shm;
static void *g_router_ptr;
static int g_cluster_enabled=1,g_cluster_ch14_opened,g_pending_projected_ticks,g_pending_start_ticks,g_hmi_focus_projected;
static uint8_t *g_original_sdresp,*g_modified_sdresp;
static size_t g_original_sdresp_len,g_modified_sdresp_len;
static int native_prepares,native_shutdowns,native_closes,native_registers,sends,posts;
static void *sent_router;
static uint8_t sent_channel;
static uint8_t sent_bytes[64];
static uint32_t sent_length;
static int32_t init_result=1;
static int32_t mock_init(void *router,void *manager) {
    (void)manager; memset(router,0,0x504); *(uint32_t*)((uint8_t*)router+0x4fc)=1; return init_result;
}
static void mock_shutdown(void *r) { ++native_shutdowns; *(uint32_t*)((uint8_t*)r+0x4fc)=0; }
static void mock_prepare(void *r) { (void)r; ++native_prepares; }
static void mock_close_channel(void *r,uint8_t c) { ((uint8_t*)r)[c]=255; ++native_closes; }
static int32_t mock_register(void *r,void *ep) {
    uint8_t service=((uint8_t*)ep)[12]; uint32_t *cell=(uint32_t*)((uint8_t*)r+0x100+service*4);
    ++native_registers; if(*cell) return 0; *cell=(uint32_t)(uintptr_t)ep; return 1;
}
static int32_t mock_queue(void *r,uint8_t c,void *bytes,uint32_t n) {
    ++sends; sent_router=r; sent_channel=c; sent_length=n;
    CHECK(n<=sizeof(sent_bytes)); memcpy(sent_bytes,bytes,n); return 0x12345678;
}
#define RTLD_NEXT ((void*)-1)
static void *dlsym(void *where,const char *name) {
    (void)where;
    if(!strcmp(name,"_ZN13MessageRouter4initEP14ChannelManager")) return (void*)mock_init;
    if(!strcmp(name,"_ZN13MessageRouter8shutdownEv")) return (void*)mock_shutdown;
    if(!strcmp(name,"_ZN11GalReceiver15prepareShutdownEv")) return (void*)mock_prepare;
    if(!strcmp(name,"_ZN13MessageRouter12closeChannelEh")) return (void*)mock_close_channel;
    if(!strcmp(name,"_ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase")) return (void*)mock_register;
    if(!strcmp(name,"_ZN13MessageRouter13queueOutgoingEhPvj")) return (void*)mock_queue;
    return NULL;
}
#include "cluster_receiver_adapter.h"
static int32_t (*real_queue_outgoing)(void*,uint8_t,void*,uint32_t)=mock_queue;
static void rate_log_record(uint8_t channel,const void *p,uint32_t n) { (void)channel;(void)p;(void)n; }
/* Generated directly from gal_cluster.c at configure time, not a test rewrite. */
#include "generation_send.inc"
static int screen_post_window(screen_window_t w,screen_buffer_t b,int n,int *rects,int f) {
    (void)w;(void)b;(void)n;(void)rects;(void)f; ++posts;return 0;
}
#include "cluster_player_generation.h"
static void *start_data(uint8_t session,uint8_t config) {
    static uint32_t shared[2]; static uint8_t *arena; uint32_t *inner;
    if(!arena) arena=VirtualAlloc((void*)0x21000000,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    CHECK(arena && (uintptr_t)arena<=UINT32_MAX); inner=(uint32_t*)arena;
    shared[1]=(uint32_t)(uintptr_t)inner; inner[0]=(uint32_t)(uintptr_t)(arena+128);
    inner[1]=32;inner[2]=0;inner[3]=6;
    arena[128]=0x80;arena[129]=1;arena[130]=8;arena[131]=session;arena[132]=16;arena[133]=config;
    return shared;
}
static DWORD WINAPI late_start(void *endpoint) {
    m2_endpoint *ep=endpoint;
    m2_dummy_route(ep,ep->channel,0x8001,start_data(44,0)); return 0;
}
int main(void) {
    uint8_t router[0x504],stock[64]={0}; uint32_t boot[4]={1,2,3,4},reader_id[4]={5,6,7,8};
    m2_receiver *old,*current; uint32_t stream; int sent_before;
    g_cluster_shm=calloc(1,sizeof(*g_cluster_shm)); CHECK(g_cluster_shm);
    m2_init(g_cluster_shm,boot,1234); InitializeCriticalSection(&g_m2_mutex); g_m2_mutex_ready=1; g_m2_enabled=1;
    CHECK(_ZN13MessageRouter4initEP14ChannelManager(router,(void*)1)==1);
    old=g_m2_owners.current; CHECK(old && old->video.native[3]==14);
    CHECK(m2_dummy_may_open(&old->video)==1); m2_dummy_opened(&old->video,42); m2_dummy_opened(&old->input,43);
    CHECK(sends==1 && sent_router==router && sent_channel==42 && sent_length==12);
    m2_dummy_route(&old->input,43,0x8002,NULL); CHECK(sends==2 && sent_channel==43 && sent_length==4);
    {
        static const uint8_t cfg[]={0,1,0,0,0,1,0x67,5,0,0,1,0x68,6};
        static const uint8_t frame[]={0,0,0,0,0,0,0,0,0,0,0,0,1,0x65,9};
        uint8_t *arena=VirtualAlloc((void*)0x20000000,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        uint32_t shared[2]={0,0},*inner; CHECK(arena && (uintptr_t)arena<=UINT32_MAX);
        inner=(uint32_t*)arena; shared[1]=(uint32_t)(uintptr_t)inner;
        inner[0]=(uint32_t)(uintptr_t)(arena+128); inner[1]=32; inner[2]=0; inner[3]=sizeof(cfg); memcpy(arena+128,cfg,sizeof(cfg));
        m2_dummy_route(&old->video,42,1,shared); CHECK(g_cluster_shm->codec_ready && g_cluster_shm->codec_length==sizeof(cfg)-2);
        m2_dummy_route(&old->video,42,0x8001,start_data(42,0)); CHECK(g_cluster_shm->codec_ready); // early config survives first Start
        inner[3]=sizeof(frame); memcpy(arena+128,frame,sizeof(frame)); m2_dummy_route(&old->video,42,0,shared);
        CHECK(g_cluster_shm->published==1 && (g_cluster_shm->packets[0].flags&M2_IDR));
        CHECK(sends==3 && sent_channel==42 && sent_length==6 && sent_bytes[1]==4 && sent_bytes[3]==42 && sent_bytes[5]==1);
        inner[2]=30; inner[3]=2; m2_dummy_route(&old->video,42,0,shared); CHECK(g_cluster_shm->published==1); // invalid bounds
        VirtualFree(arena,0,MEM_RELEASE);
    }
    CHECK(m2_claim(g_cluster_shm,&g_m2_player_reader,reader_id)==M2_PACKET);
    g_m2_player_ring=g_cluster_shm; CHECK(m2_player_post(NULL,NULL,0,NULL,0)==-1 && posts==0);
    m2_store(&g_m2_can_post,1); CHECK(m2_player_post(NULL,NULL,0,NULL,0)==0 && posts==1);
    stream=old->stream;
    m2_dummy_route(&old->video,42,0x8001,start_data(42,0)); CHECK(old->stream==stream); // duplicate live Start is idempotent
    m2_dummy_route(&old->video,42,0x8002,NULL); CHECK(!m2_load(&g_cluster_shm->live)); CHECK(sends==3);
    CHECK(m2_player_post(NULL,NULL,0,NULL,0)==-1 && posts==1);
    m2_dummy_route(&old->video,42,0x8001,start_data(42,0)); CHECK(old->stream>stream);
    CHECK(m2_player_post(NULL,NULL,0,NULL,0)==-1 && posts==1); // old decoder cannot adopt new session
    {
        HANDLE thread;
        primary_thread=GetCurrentThreadId(); callback_waiting=CreateEventA(NULL,TRUE,FALSE,NULL); CHECK(callback_waiting);
        pthread_mutex_lock(&g_m2_mutex);
        thread=CreateThread(NULL,0,late_start,&old->video,0,NULL); CHECK(thread);
        CHECK(WaitForSingleObject(callback_waiting,1000)==WAIT_OBJECT_0);
        CHECK(m2_stream_start(&g_m2_owners,old,g_cluster_shm)==M2_PACKET); stream=old->stream;
        pthread_mutex_unlock(&g_m2_mutex);
        CHECK(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0);
        CHECK(old->stream==stream && old->started==0); // callback admitted before reset cannot revive it
        CloseHandle(thread); CloseHandle(callback_waiting); callback_waiting=NULL;
    }
    CHECK(_ZN13MessageRouter4initEP14ChannelManager(router,(void*)1)==1); current=g_m2_owners.current;
    CHECK(current!=old && old->retired && current->generation>old->generation);
    m2_dummy_opened(&current->video,77); sent_before=sends;
    m2_dummy_route(&old->input,43,0x8002,NULL); CHECK(sends==sent_before);
    CHECK(m2_dummy_closed(&old->video)==0); CHECK(m2_load(&g_cluster_shm->live));
    {
        uint8_t *bytes=malloc(2); bytes[0]=1;bytes[1]=2;
        CHECK(hook_queue_outgoing(router,14,bytes,2)==0x12345678 && sent_channel==77);
    }
    _ZN13MessageRouter12closeChannelEh(router,77); CHECK(native_closes==1 && !m2_load(&g_cluster_shm->live));
    m2_dummy_opened(&current->video,77); _ZN11GalReceiver15prepareShutdownEv(router);
    CHECK(native_prepares==1 && current->retired && !m2_load(&g_cluster_shm->live));
    _ZN13MessageRouter8shutdownEv(router); CHECK(native_shutdowns==1);
    CHECK(_ZN13MessageRouter4initEP14ChannelManager(router,(void*)1)==1);
    stock[12]=14;
    CHECK(_ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase(router,stock)==1 && native_registers==1);
    CHECK(g_m2_owners.current->retired && *m2_slot(router,21)==0); // stock registration takes priority
    init_result=0; CHECK(_ZN13MessageRouter4initEP14ChannelManager(router,(void*)1)==0);
    CHECK(m2_create_mapping()==0); CHECK(m2_player_claim(g_cluster_shm)==M2_INVALID);
    CHECK(m2_player_public_matches()==0); m2_player_signal(0); CHECK(g_m2_player_quit);
    DeleteCriticalSection(&g_m2_mutex); free(g_cluster_shm);
    puts("actual receiver/send/player adapters passed against mocked stock/OS/Screen effects"); return 0;
}

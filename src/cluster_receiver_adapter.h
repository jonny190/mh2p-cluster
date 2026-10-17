/* Local P2873 native adapter. CC BY-NC-SA 4.0; see LICENSE.md.
 * Included after g_router_ptr in gal_cluster.c. Uses audited ARM32 table
 * offsets only. Loader interception hits still require target verification.
 */
#include "cluster_receiver_owner.h"
#if UINTPTR_MAX != UINT32_MAX && !defined(M2_ADAPTER_SYNTAX_ONLY)
#error P2873 receiver adapter requires ARM32 pointer layout
#endif
static m2_owners g_m2_owners;
static pthread_mutex_t g_m2_mutex;
static int g_m2_enabled=0, g_m2_mutex_ready=0;
static int32_t m2_native_send(m2_endpoint *ep,uint8_t *data,uint32_t bytes);
static void *g_m2_vtable[16];
static int g_m2_mapping_lock=-1;
static int m2_create_mapping(void) {
    struct flock lock;
    uint32_t nonce[4]; int randomfd,sfd;
    g_m2_mapping_lock=open("/tmp/mib2p_cluster_writer.lock",O_CREAT|O_RDWR,0600);
    memset(&lock,0,sizeof(lock)); lock.l_type=F_WRLCK; lock.l_whence=SEEK_SET;
    if(g_m2_mapping_lock<0 || fcntl(g_m2_mapping_lock,F_SETLK,&lock)) return 0;
    fcntl(g_m2_mapping_lock,F_SETFD,FD_CLOEXEC);
    randomfd=open("/dev/urandom",O_RDONLY|O_NONBLOCK);
    if(randomfd<0) randomfd=open("/dev/random",O_RDONLY|O_NONBLOCK);
    if(randomfd<0) return 0;
    if(read(randomfd,nonce,sizeof(nonce))!=(ssize_t)sizeof(nonce)) { close(randomfd); return 0; }
    close(randomfd);
    /* Never reset an inode which an old player still has mapped. The writer
     * lock prevents a second live GAL from unlinking another writer's name.
     */
    if(shm_unlink(CLUSTER_SHM_NAME) && errno!=ENOENT) return 0;
    sfd=shm_open(CLUSTER_SHM_NAME,O_RDWR|O_CREAT|O_EXCL,0666);
    if(sfd<0) return 0;
    if(ftruncate(sfd,sizeof(cluster_h264_shm_t))) { close(sfd); return 0; }
    g_cluster_shm=(cluster_h264_shm_t*)mmap(NULL,sizeof(cluster_h264_shm_t),PROT_READ|PROT_WRITE,MAP_SHARED,sfd,0);
    close(sfd);
    if(g_cluster_shm==MAP_FAILED) { g_cluster_shm=NULL; return 0; }
    m2_init(g_cluster_shm,nonce,(uint32_t)getpid());
    return 1;
}

static uint32_t *m2_slot(void *router,uint32_t service) { return (uint32_t*)((uint8_t*)router+0x100+service*4); }
static m2_receiver *m2_for_router(void *router) {
    m2_receiver *o=g_m2_owners.current;
    if(!g_m2_enabled || !m2_owner_live(&g_m2_owners,o) || o->router!=router) return NULL;
    if(*m2_slot(router,M2_VIDEO_SERVICE)!=(uint32_t)(uintptr_t)o->video.native ||
       *m2_slot(router,M2_INPUT_SERVICE)!=(uint32_t)(uintptr_t)o->input.native ||
       !*(uint32_t*)((uint8_t*)router+0x4fc)) {
        m2_retire(&g_m2_owners,o,g_cluster_shm); g_router_ptr=NULL; g_cluster_ch14_opened=0;
        log_line("[generation] service ownership lost; receiver=%u retired\n",o->generation); return NULL;
    }
    return o;
}
static void m2_retire_router(void *router) {
    m2_receiver *o=g_m2_owners.current;
    if(o && o->router==router) {
        m2_retire(&g_m2_owners,o,g_cluster_shm); g_router_ptr=NULL; g_cluster_ch14_opened=0;
        g_pending_projected_ticks=0; g_pending_start_ticks=0;
        free(g_original_sdresp); free(g_modified_sdresp);
        g_original_sdresp=g_modified_sdresp=NULL;
        g_original_sdresp_len=g_modified_sdresp_len=0;
        log_line("[generation] receiver=%u retired; raw endpoints retained as tombstones\n",o->generation);
    }
}
static int m2_dummy_stub(void) { return 0; }
static int m2_dummy_may_open(void *self) {
    m2_endpoint *ep=(m2_endpoint*)self; int result;
    pthread_mutex_lock(&g_m2_mutex);
    result=m2_for_router(ep->owner->router)==ep->owner;
    pthread_mutex_unlock(&g_m2_mutex); return result;
}
static int m2_dummy_closed(void *self) {
    m2_endpoint *ep=(m2_endpoint*)self; int previous;
    pthread_mutex_lock(&g_m2_mutex);
    previous=m2_endpoint_close(&g_m2_owners,ep,g_cluster_shm);
    if(ep->service==M2_VIDEO_SERVICE && g_m2_owners.current==ep->owner) g_cluster_ch14_opened=0;
    log_line("[generation] close receiver=%u service=%u channel=%u previous_gate=%d\n",
        ep->owner->generation,ep->service,ep->channel,previous);
    pthread_mutex_unlock(&g_m2_mutex); return previous;
}
static void m2_dummy_opened(void *self,uint8_t channel) {
    m2_endpoint *ep=(m2_endpoint*)self;
    pthread_mutex_lock(&g_m2_mutex);
    if(m2_for_router(ep->owner->router)==ep->owner &&
       m2_endpoint_open(&g_m2_owners,ep,channel,g_cluster_shm)==M2_PACKET) {
        log_line("[generation] open receiver=%u stream=%u service=%u channel=%u\n",
            ep->owner->generation,ep->owner->stream,ep->service,channel);
        if(ep->service==M2_VIDEO_SERVICE) {
            uint8_t *setup=(uint8_t*)malloc(12);
            static const uint8_t bytes[12]={0x80,0x03,0x08,0x02,0x10,0x08,0x18,0x00,0x18,0x01,0x18,0x02};
            g_cluster_ch14_opened=1;
            if(setup) { memcpy(setup,bytes,12); m2_native_send(ep,setup,12); }
        }
    }
    pthread_mutex_unlock(&g_m2_mutex);
}
static int m2_payload(void *data,uint16_t type,uint8_t **payload,uint32_t *length) {
    uint32_t *shared=(uint32_t*)data,*inner;
    if(!shared || !shared[1]) return 0;
    inner=(uint32_t*)(uintptr_t)shared[1];
    if(!inner[0] || inner[3]<inner[2] || inner[3]>inner[1] || inner[3]>UINT32_MAX-inner[0] ||
       inner[3]-inner[2]<2 || inner[3]-inner[2]>M2_PACKET_MAX+10) return 0;
    *payload=(uint8_t*)(uintptr_t)inner[0]+inner[2]; *length=inner[3]-inner[2];
    if((((uint16_t)(*payload)[0]<<8)|(*payload)[1])!=type) return 0;
    *payload+=2; *length-=2; return 1;
}
static int m2_dummy_route(void *self,uint32_t channel,uint16_t type,void *data) {
    m2_endpoint *ep=(m2_endpoint*)self; m2_receiver *o=ep->owner;
    uint32_t captured=m2_load(&ep->callback_stream);
    uint8_t *payload; uint32_t length;
    pthread_mutex_lock(&g_m2_mutex);
    if(m2_for_router(o->router)!=o || !m2_endpoint_live(&g_m2_owners,ep,channel)) goto done;
    if(ep->service==M2_VIDEO_SERVICE && captured!=o->stream) goto done;
    if(ep->service==M2_INPUT_SERVICE) {
        if(type==0x8002) { /* preserve upstream input handshake; NOT video Stop */
            uint8_t *reply=(uint8_t*)malloc(4);
            if(reply) { reply[0]=0x80; reply[1]=0x06; reply[2]=8; reply[3]=0; m2_native_send(ep,reply,4); }
        }
        goto done;
    }
    if(type==0x8002) { m2_stop(g_cluster_shm); o->started=0; goto done; }
    if(type==0x8001) {
        uint32_t session=0,configuration=0;
        if(!m2_payload(data,type,&payload,&length) || !m2_start_values(payload,length,&session,&configuration)) {
            m2_stop(g_cluster_shm); goto done;
        }
        if(o->started && m2_load(&g_cluster_shm->live) && o->session==session && o->configuration==configuration) goto done;
        /* First Start preserves configuration received between open and Start.
         * Subsequent playback Start requires a fresh stream/codec/keyframe.
         */
        if(o->started || !m2_load(&g_cluster_shm->live)) {
            if(m2_stream_start(&g_m2_owners,o,g_cluster_shm)!=M2_PACKET) goto done;
        }
        o->session=session; o->configuration=configuration; o->started=1; goto done;
    }
    if((type!=0 && type!=1) || !data) goto done;
    if(type==0 && !o->started) goto done;
    if(!m2_payload(data,type,&payload,&length)) goto done;
    if(!type) { if(length<=8) goto done; payload+=8; length-=8; }
    {
        int result=M2_BUSY,attempt;
        for(attempt=0;attempt<20;++attempt) {
            result=m2_publish(g_cluster_shm,&o->stamp,payload,length,type==1?M2_CODEC:M2_FRAME);
            if(result!=M2_BUSY) break;
            usleep(100); /* bounded copy contention; no vendor work holds the ring lock */
        }
        if(result==M2_BUSY || result==M2_INVALID) m2_stop(g_cluster_shm);
        if(type==0 && (result==M2_PACKET || result==M2_EMPTY)) {
            uint8_t *ack=(uint8_t*)malloc(14);
            if(ack) { uint32_t size=m2_ack_bytes(o->session,1,ack); m2_native_send(ep,ack,size); }
        }
        if(result<0 || result==M2_BUSY)
            log_line("[generation] video rejected receiver=%u stream=%u result=%d\n",o->generation,o->stream,result);
    }
done:
    pthread_mutex_unlock(&g_m2_mutex); return 0;
}
static void m2_install(void *router) {
    m2_receiver *o; unsigned i;
    if(!g_m2_enabled || !g_cluster_enabled || !g_cluster_shm) return;
    if(g_m2_owners.current) m2_retire_router(g_m2_owners.current->router);
    o=m2_owner_new(&g_m2_owners,router,!*m2_slot(router,14) && !*m2_slot(router,21),g_cluster_shm);
    if(!o) { g_router_ptr=NULL; g_cluster_ch14_opened=0; return; }
    if(g_m2_owners.count==1) {
        for(i=0;i<16;++i) g_m2_vtable[i]=(void*)m2_dummy_stub;
        g_m2_vtable[2]=(void*)m2_dummy_closed; g_m2_vtable[3]=(void*)m2_dummy_may_open;
        g_m2_vtable[4]=(void*)m2_dummy_opened; g_m2_vtable[5]=(void*)m2_dummy_route;
    }
    o->video.native[0]=o->input.native[0]=(uint32_t)(uintptr_t)g_m2_vtable;
    *m2_slot(router,14)=(uint32_t)(uintptr_t)o->video.native;
    *m2_slot(router,21)=(uint32_t)(uintptr_t)o->input.native;
    g_router_ptr=router; g_cluster_ch14_opened=0; g_hmi_focus_projected=0;
    log_line("[generation] installed receiver=%u router=%p; services14/21 vacant\n",o->generation,router);
}
int32_t _ZN13MessageRouter4initEP14ChannelManager(void *self,void *manager) {
    static int32_t (*original)(void*,void*)=NULL; int32_t result;
    if(!original) original=(int32_t(*)(void*,void*))dlsym(RTLD_NEXT,"_ZN13MessageRouter4initEP14ChannelManager");
    if(!original) return 0;
    if(g_m2_mutex_ready) pthread_mutex_lock(&g_m2_mutex);
    m2_retire_router(self); result=original(self,manager); if(result==1) m2_install(self);
    if(g_m2_mutex_ready) pthread_mutex_unlock(&g_m2_mutex);
    return result;
}
void _ZN13MessageRouter8shutdownEv(void *self) {
    static void (*original)(void*)=NULL;
    if(!original) original=(void(*)(void*))dlsym(RTLD_NEXT,"_ZN13MessageRouter8shutdownEv");
    if(g_m2_mutex_ready) pthread_mutex_lock(&g_m2_mutex);
    m2_retire_router(self); if(original) original(self);
    if(g_m2_mutex_ready) pthread_mutex_unlock(&g_m2_mutex);
}
void _ZN11GalReceiver15prepareShutdownEv(void *self) {
    static void (*original)(void*)=NULL;
    if(!original) original=(void(*)(void*))dlsym(RTLD_NEXT,"_ZN11GalReceiver15prepareShutdownEv");
    if(g_m2_mutex_ready) pthread_mutex_lock(&g_m2_mutex);
    m2_retire_router(self); if(original) original(self);
    if(g_m2_mutex_ready) pthread_mutex_unlock(&g_m2_mutex);
}
void _ZN13MessageRouter12closeChannelEh(void *self,uint8_t channel) {
    static void (*original)(void*,uint8_t)=NULL; m2_receiver *o;
    if(!original) original=(void(*)(void*,uint8_t))dlsym(RTLD_NEXT,"_ZN13MessageRouter12closeChannelEh");
    if(g_m2_mutex_ready) pthread_mutex_lock(&g_m2_mutex);
    o=m2_for_router(self);
    if(o && m2_endpoint_live(&g_m2_owners,&o->video,channel)) m2_endpoint_close(&g_m2_owners,&o->video,g_cluster_shm);
    if(o && m2_endpoint_live(&g_m2_owners,&o->input,channel)) m2_endpoint_close(&g_m2_owners,&o->input,g_cluster_shm);
    if(original) original(self,channel);
    if(g_m2_mutex_ready) pthread_mutex_unlock(&g_m2_mutex);
}
int32_t _ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase(void *self,void *endpoint) {
    static int32_t (*original)(void*,void*)=NULL; int32_t result;
    if(!original) original=(int32_t(*)(void*,void*))dlsym(RTLD_NEXT,"_ZN11GalReceiver15registerServiceEP20ProtocolEndpointBase");
    if(!original) return 0;
    if(g_m2_mutex_ready) pthread_mutex_lock(&g_m2_mutex);
    if(g_m2_enabled && endpoint) {
        m2_receiver *o=m2_for_router(self);
        uint8_t service=((uint8_t*)endpoint)[0x0c];
        if(o && (service==M2_VIDEO_SERVICE || service==M2_INPUT_SERVICE) &&
           endpoint!=o->video.native && endpoint!=o->input.native) {
            /* A legitimate stock registration always wins. Detach only our
             * exact raw cells; retain tombstones for any borrowed callbacks.
             */
            m2_retire_router(self);
            if(*m2_slot(self,14)==(uint32_t)(uintptr_t)o->video.native) *m2_slot(self,14)=0;
            if(*m2_slot(self,21)==(uint32_t)(uintptr_t)o->input.native) *m2_slot(self,21)=0;
        }
    }
    result=original(self,endpoint); (void)m2_for_router(self);
    if(g_m2_mutex_ready) pthread_mutex_unlock(&g_m2_mutex);
    return result;
}

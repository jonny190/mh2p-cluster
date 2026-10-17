/* Local P2873 receiver admission. CC BY-NC-SA 4.0; see LICENSE.md.
 * Adapter serializes calls. Retired endpoints remain bounded tombstones:
 * stock raw pointers and unproven comm callbacks are never freed or reused.
 */
#ifndef CLUSTER_RECEIVER_OWNER_H
#define CLUSTER_RECEIVER_OWNER_H
#include "cluster_h264_shm.h"
#define M2_OWNER_LIMIT 256u
#define M2_VIDEO_SERVICE 14u
#define M2_INPUT_SERVICE 21u
typedef struct m2_receiver m2_receiver;
typedef struct { uint32_t native[16]; m2_receiver *owner; uint32_t service, channel, opened; volatile uint32_t callback_stream; } m2_endpoint;
struct m2_receiver {
    void *router;
    uint32_t generation, stream, retired, started, session, configuration;
    m2_stamp stamp;
    m2_endpoint video,input;
};
typedef struct { uint32_t count,generation,stream; m2_receiver *current; m2_receiver owners[M2_OWNER_LIMIT]; } m2_owners;
M2_INLINE void m2_retire(m2_owners *pool,m2_receiver *owner,cluster_h264_shm_t *ring) {
    if(!owner) return;
    owner->retired=1; owner->video.opened=owner->input.opened=0;
    ((uint8_t*)owner->video.native)[4]=0; ((uint8_t*)owner->input.native)[4]=0;
    if(pool->current==owner && ring) m2_stop(ring);
}
M2_INLINE m2_receiver *m2_owner_new(m2_owners *pool,void *router,int vacant,cluster_h264_shm_t *ring) {
    m2_receiver *owner;
    if(pool->current) m2_retire(pool,pool->current,ring);
    pool->current=NULL;
    if(!vacant || pool->count==M2_OWNER_LIMIT || pool->generation==UINT32_MAX) return NULL;
    owner=&pool->owners[pool->count++]; memset(owner,0,sizeof(*owner));
    owner->router=router; owner->generation=++pool->generation;
    owner->video.owner=owner->input.owner=owner;
    owner->video.service=M2_VIDEO_SERVICE; owner->input.service=M2_INPUT_SERVICE;
    owner->video.native[3]=M2_VIDEO_SERVICE; owner->input.native[3]=M2_INPUT_SERVICE;
    pool->current=owner; return owner;
}
M2_INLINE int m2_owner_live(const m2_owners *pool,const m2_receiver *owner) {
    return owner && pool->current==owner && !owner->retired;
}
M2_INLINE int m2_stream_start(m2_owners *pool,m2_receiver *owner,cluster_h264_shm_t *ring) {
    int result;
    if(!m2_owner_live(pool,owner) || !ring) return M2_STALE;
    if(pool->stream==UINT32_MAX) { m2_retire(pool,owner,ring); return M2_STALE; }
    owner->stream=++pool->stream;
    m2_stop(ring);
    result=m2_begin(ring,owner->generation,owner->stream);
    if(result!=M2_PACKET) return result;
    owner->stamp=ring->stamp; owner->started=0;
    m2_store(&owner->video.callback_stream,owner->stream); return M2_PACKET;
}
M2_INLINE int m2_endpoint_open(m2_owners *pool,m2_endpoint *ep,uint32_t channel,cluster_h264_shm_t *ring) {
    if(!m2_owner_live(pool,ep->owner) || !channel || channel>254) return M2_STALE;
    if(ep->service==M2_VIDEO_SERVICE && ep->owner->input.opened && ep->owner->input.channel==channel) return M2_INVALID;
    if(ep->service==M2_INPUT_SERVICE && ep->owner->video.opened && ep->owner->video.channel==channel) return M2_INVALID;
    if(ep->opened && ep->channel==channel) return M2_EMPTY;
    if(ep->service==M2_VIDEO_SERVICE && m2_stream_start(pool,ep->owner,ring)!=M2_PACKET) return M2_STALE;
    ep->channel=channel; ep->opened=1; ((uint8_t*)ep->native)[4]=1; return M2_PACKET;
}
M2_INLINE int m2_endpoint_close(m2_owners *pool,m2_endpoint *ep,cluster_h264_shm_t *ring) {
    int previous=((uint8_t*)ep->native)[4];
    ((uint8_t*)ep->native)[4]=0; ep->opened=0;
    if(ep->service==M2_VIDEO_SERVICE && m2_owner_live(pool,ep->owner) && ring) m2_stop(ring);
    return previous;
}
M2_INLINE int m2_endpoint_live(const m2_owners *pool,const m2_endpoint *ep,uint32_t channel) {
    return ep && m2_owner_live(pool,ep->owner) && ep->opened && ep->channel==channel;
}
/* Exact saved P2873 Start constants: session_id=field1, configuration_index=field2.
 * Bounded protobuf reader; unknown varint/fixed/length fields are skipped.
 */
M2_INLINE int m2_varint(const uint8_t *p,uint32_t n,uint32_t *at,uint64_t *value) {
    unsigned i; uint64_t v=0;
    for(i=0;i<10 && *at<n;++i) {
        uint8_t b=p[(*at)++];
        if(i==9 && b>1) return 0;
        v|=(uint64_t)(b&127)<<(i*7);
        if(!(b&128)) { *value=v; return 1; }
    }
    return 0;
}
M2_INLINE int m2_start_values(const uint8_t *p,uint32_t n,uint32_t *session,uint32_t *configuration) {
    uint32_t at=0,found=0;
    if(n>128) return 0;
    while(at<n) {
        uint64_t tag,value; unsigned wire;
        if(!m2_varint(p,n,&at,&tag) || !(tag>>3) || (tag>>3)>0x1fffffff) return 0;
        wire=(unsigned)(tag&7);
        if(wire==0) {
            if(!m2_varint(p,n,&at,&value)) return 0;
            if((tag>>3)==1 || (tag>>3)==2) {
                if(value>UINT32_MAX) return 0;
                if((tag>>3)==1) { *session=(uint32_t)value; found|=1; }
                else { *configuration=(uint32_t)value; found|=2; }
            }
        } else if(wire==1 || wire==5) {
            uint32_t skip=wire==1?8:4; if(skip>n-at) return 0; at+=skip;
        } else if(wire==2) {
            if(!m2_varint(p,n,&at,&value) || value>n-at) return 0; at+=(uint32_t)value;
        } else return 0;
    }
    return found==3;
}
M2_INLINE uint32_t m2_ack_bytes(uint32_t session,uint32_t count,uint8_t out[14]) {
    uint32_t values[2]={session,count},at=2,i;
    out[0]=0x80; out[1]=4;
    for(i=0;i<2;++i) {
        uint32_t value=values[i]; out[at++]=(uint8_t)((i+1)<<3);
        do { uint8_t b=(uint8_t)(value&127); value>>=7; out[at++]=b|(value?128:0); } while(value);
    }
    return at;
}
#endif

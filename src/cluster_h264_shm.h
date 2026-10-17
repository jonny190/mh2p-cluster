/* Copyright (c) 2026 fifthBro. Local P2873 adaptation.
 * CC BY-NC-SA 4.0; see LICENSE.md. Incompatible with the upstream raw ring.
 * Compound state/payload accesses hold a 32-bit atomic try-lock. No vendor
 * call holds this lock; a crashed writer cannot block readers indefinitely.
 */
#ifndef CLUSTER_H264_SHM_H
#define CLUSTER_H264_SHM_H

#include <stdint.h>
#include <string.h>
#if defined(_MSC_VER)
#include <intrin.h>
#define M2_INLINE static __inline
M2_INLINE uint32_t m2_load(volatile uint32_t *p) { return (uint32_t)_InterlockedCompareExchange((volatile long *)p,0,0); }
M2_INLINE void m2_store(volatile uint32_t *p,uint32_t v) { _InterlockedExchange((volatile long *)p,(long)v); }
M2_INLINE int m2_cas(volatile uint32_t *p,uint32_t old,uint32_t v) { return (uint32_t)_InterlockedCompareExchange((volatile long *)p,(long)v,(long)old)==old; }
#else
#define M2_INLINE static inline
M2_INLINE uint32_t m2_load(volatile uint32_t *p) { return __sync_val_compare_and_swap(p,0,0); }
M2_INLINE void m2_store(volatile uint32_t *p,uint32_t v) { __sync_lock_test_and_set(p,v); __sync_synchronize(); }
M2_INLINE int m2_cas(volatile uint32_t *p,uint32_t old,uint32_t v) { return __sync_bool_compare_and_swap(p,old,v); }
#endif
#define CLUSTER_SHM_NAME "/mib2p_cluster_h264_v2"
#define CLUSTER_SHM_MAGIC 0x3243484du
#define CLUSTER_SHM_VERSION 2u
#define M2_SLOTS 8u
#define M2_PACKET_MAX (512u * 1024u)
#define M2_CODEC_MAX (64u * 1024u)
#define M2_EMPTY 0
#define M2_PACKET 1
#define M2_BUSY 2
#define M2_STALE -1
#define M2_INVALID -2
#define M2_FULL -3
#define M2_CODEC_CHANGED -4
#define M2_CODEC 1u
#define M2_FRAME 2u
#define M2_IDR 4u
typedef struct { uint32_t boot[4], receiver, stream; } m2_stamp;
typedef struct { uint32_t sequence, length, flags; uint8_t bytes[M2_PACKET_MAX]; } m2_packet;
typedef struct {
    volatile uint32_t magic, lock, live;
    uint32_t version, bytes, writer_pid;
    m2_stamp stamp;
    uint32_t published, consumed, reader[4], reader_pid, codec_length, codec_ready, keyframe;
    uint8_t codec[M2_CODEC_MAX];
    m2_packet packets[M2_SLOTS];
} cluster_h264_shm_t;
typedef struct { m2_stamp stamp; uint32_t identity[4], codec_sent, keyframe; } m2_reader;
M2_INLINE int m2_lock(cluster_h264_shm_t *s) { return m2_cas(&s->lock,0,1); }
M2_INLINE void m2_unlock(cluster_h264_shm_t *s) { m2_store(&s->lock,0); }
M2_INLINE int m2_equal(const m2_stamp *a,const m2_stamp *b) { return !memcmp(a,b,sizeof(*a)); }
M2_INLINE int m2_valid(cluster_h264_shm_t *s) {
    return m2_load(&s->magic)==CLUSTER_SHM_MAGIC && s->version==CLUSTER_SHM_VERSION && s->bytes==sizeof(*s);
}
M2_INLINE void m2_init(cluster_h264_shm_t *s,const uint32_t nonce[4],uint32_t pid) {
    memset(s,0,sizeof(*s)); s->version=CLUSTER_SHM_VERSION; s->bytes=(uint32_t)sizeof(*s);
    memcpy(s->stamp.boot,nonce,16); s->writer_pid=pid; m2_store(&s->magic,CLUSTER_SHM_MAGIC);
}
M2_INLINE void m2_stop(cluster_h264_shm_t *s) { m2_store(&s->live,0); }
M2_INLINE int m2_begin(cluster_h264_shm_t *s,uint32_t receiver,uint32_t stream) {
    uint32_t i;
    if (!receiver || !stream || !m2_valid(s)) return M2_INVALID;
    if (!m2_lock(s)) return M2_BUSY;
    if (receiver < s->stamp.receiver || (receiver==s->stamp.receiver && stream<=s->stamp.stream)) {
        m2_unlock(s); return M2_STALE;
    }
    m2_stop(s);
    s->stamp.receiver=receiver; s->stamp.stream=stream; s->published=s->consumed=0;
    s->codec_length=s->codec_ready=s->keyframe=s->reader_pid=0; memset(s->reader,0,16);
    for(i=0;i<M2_SLOTS;++i) s->packets[i].sequence=0;
    m2_store(&s->live,1); m2_unlock(s); return M2_PACKET;
}
M2_INLINE unsigned m2_nals(const uint8_t *p,uint32_t size) {
    uint32_t i; unsigned found=0;
    for(i=0;i+3<size;++i) {
        uint32_t at;
        if(p[i] || p[i+1]) continue;
        if(p[i+2]==1) at=i+3;
        else if(i+4<size && p[i+2]==0 && p[i+3]==1) at=i+4;
        else continue;
        if((p[at]&31)==7) found|=1;
        if((p[at]&31)==8) found|=2;
        if((p[at]&31)==5) found|=4;
    }
    return found;
}
M2_INLINE int m2_publish(cluster_h264_shm_t *s,const m2_stamp *stamp,const uint8_t *p,uint32_t n,uint32_t kind) {
    m2_packet *packet; unsigned nals; int result=M2_PACKET;
    if(!p || !n || n>M2_PACKET_MAX || (kind!=M2_CODEC && kind!=M2_FRAME)) return M2_INVALID;
    if(!m2_lock(s)) return M2_BUSY;
    if(!m2_load(&s->live) || !m2_equal(stamp,&s->stamp)) { m2_unlock(s); return M2_STALE; }
    if(kind==M2_CODEC) {
        if(s->codec_ready) {
            result=(n==s->codec_length && !memcmp(p,s->codec,n)) ? M2_PACKET : M2_CODEC_CHANGED;
            if(result==M2_CODEC_CHANGED) m2_stop(s);
        } else if(n>M2_CODEC_MAX-s->codec_length) { m2_stop(s); result=M2_INVALID; }
        else {
            memcpy(s->codec+s->codec_length,p,n); s->codec_length+=n;
            s->codec_ready=(m2_nals(s->codec,s->codec_length)&3)==3;
        }
        m2_unlock(s); return result;
    }
    nals=m2_nals(p,n);
    if(!s->codec_ready || (!s->keyframe && !(nals&4))) { m2_unlock(s); return M2_EMPTY; }
    if(s->published==UINT32_MAX || s->published-s->consumed>=M2_SLOTS) {
        m2_stop(s); m2_unlock(s); return M2_FULL;
    }
    packet=&s->packets[s->published%M2_SLOTS];
    packet->length=n; packet->flags=M2_FRAME|((nals&4)?M2_IDR:0); memcpy(packet->bytes,p,n);
    packet->sequence=++s->published; s->keyframe=1;
    m2_unlock(s); return result;
}
M2_INLINE int m2_claim_pid(cluster_h264_shm_t *s,m2_reader *r,const uint32_t identity[4],uint32_t pid) {
    static const uint32_t zero[4]={0,0,0,0};
    if(!memcmp(identity,zero,16) || !m2_valid(s)) return M2_INVALID;
    if(!m2_lock(s)) return M2_BUSY;
    if(!m2_load(&s->live)) { m2_unlock(s); return M2_EMPTY; }
    if(memcmp(s->reader,zero,16)) { m2_unlock(s); return M2_BUSY; }
    memset(r,0,sizeof(*r)); r->stamp=s->stamp; memcpy(r->identity,identity,16); memcpy(s->reader,identity,16); s->reader_pid=pid;
    m2_unlock(s); return M2_PACKET;
}
M2_INLINE int m2_claim(cluster_h264_shm_t *s,m2_reader *r,const uint32_t identity[4]) { return m2_claim_pid(s,r,identity,0); }
M2_INLINE int m2_matches(cluster_h264_shm_t *s,const m2_reader *r) {
    int valid;
    if(!m2_valid(s) || !m2_load(&s->live)) return M2_STALE;
    if(!m2_lock(s)) return M2_BUSY;
    valid=m2_load(&s->live) && m2_equal(&s->stamp,&r->stamp) && !memcmp(s->reader,r->identity,16);
    m2_unlock(s); return valid?M2_PACKET:M2_STALE;
}
M2_INLINE int m2_read(cluster_h264_shm_t *s,m2_reader *r,uint8_t *out,uint32_t capacity,uint32_t *length,uint32_t *flags) {
    m2_packet *packet;
    *length=*flags=0;
    if(!m2_lock(s)) return M2_BUSY;
    if(!m2_load(&s->live) || !m2_equal(&s->stamp,&r->stamp) || memcmp(s->reader,r->identity,16)) {
        m2_unlock(s); return M2_STALE;
    }
    if(!s->codec_ready || !s->keyframe) { m2_unlock(s); return M2_EMPTY; }
    if(!r->codec_sent) {
        if(!s->codec_length || s->codec_length>M2_CODEC_MAX || s->codec_length>capacity) { m2_unlock(s); return M2_INVALID; }
        memcpy(out,s->codec,s->codec_length); *length=s->codec_length; *flags=M2_CODEC; r->codec_sent=1;
        m2_unlock(s); return M2_PACKET;
    }
    while(s->consumed<s->published) {
        packet=&s->packets[s->consumed%M2_SLOTS];
        if(packet->sequence!=s->consumed+1 || !packet->length || packet->length>M2_PACKET_MAX || packet->length>capacity) {
            m2_stop(s); m2_unlock(s); return M2_INVALID;
        }
        ++s->consumed;
        if(!r->keyframe && !(packet->flags&M2_IDR)) continue;
        memcpy(out,packet->bytes,packet->length); *length=packet->length; *flags=packet->flags; r->keyframe=1;
        m2_unlock(s); return M2_PACKET;
    }
    m2_unlock(s); return M2_EMPTY;
}
M2_INLINE int m2_release(cluster_h264_shm_t *s,const m2_reader *r) {
    if(!m2_lock(s)) return M2_BUSY;
    if(!m2_equal(&s->stamp,&r->stamp) || memcmp(s->reader,r->identity,16)) { m2_unlock(s); return M2_STALE; }
    memset(s->reader,0,16); s->reader_pid=0; m2_unlock(s); return M2_PACKET;
}

#endif

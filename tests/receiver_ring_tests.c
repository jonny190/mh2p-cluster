/* CC BY-NC-SA 4.0; see LICENSE.md. Production receiver/ring code tests. */
#include <stdio.h>
#include <stdlib.h>
#include "cluster_receiver_owner.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static const uint8_t codec[]={0,0,0,1,0x67,5,0,0,1,0x68,6};
static const uint8_t idr[]={0,0,1,0x65,9,8,7};
static const uint8_t delta[]={0,0,1,0x41,6,5,4};
int main(void) {
    uint32_t boot[4]={1,2,3,4}, identity[4]={5,6,7,8}, length,flags,i;
    uint8_t data[100]; m2_reader reader,other; m2_stamp old;
    cluster_h264_shm_t *s=calloc(1,sizeof(*s)); m2_owners *pool=calloc(1,sizeof(*pool));
    m2_receiver *a,*b; CHECK(s && pool); m2_init(s,boot,1234); CHECK(m2_valid(s));
    {
        uint32_t session=0,configuration=0;
        const uint8_t start[]={8,42,16,0,24,99},missing[]={8,42},truncated[]={8,128};
        CHECK(m2_start_values(start,sizeof(start),&session,&configuration) && session==42 && configuration==0);
        CHECK(!m2_start_values(missing,sizeof(missing),&session,&configuration));
        CHECK(!m2_start_values(truncated,sizeof(truncated),&session,&configuration));
    }
    a=m2_owner_new(pool,(void*)1,1,s); CHECK(a && a->generation==1);
    CHECK(a->video.native[3]==14 && a->input.native[3]==21);
    CHECK(m2_endpoint_open(pool,&a->video,42,s)==M2_PACKET); old=a->stamp;
    CHECK(m2_endpoint_open(pool,&a->video,42,s)==M2_EMPTY); CHECK(m2_equal(&old,&a->stamp));
    CHECK(m2_endpoint_open(pool,&a->input,43,s)==M2_PACKET);
    CHECK(m2_endpoint_live(pool,&a->video,42)); CHECK(!m2_endpoint_live(pool,&a->video,14));
    CHECK(m2_claim(s,&reader,identity)==M2_PACKET); CHECK(m2_claim(s,&other,boot)==M2_BUSY);
    CHECK(m2_publish(s,&old,delta,sizeof(delta),M2_FRAME)==M2_EMPTY);
    CHECK(m2_publish(s,&old,codec,sizeof(codec),M2_CODEC)==M2_PACKET);
    CHECK(m2_read(s,&reader,data,sizeof(data),&length,&flags)==M2_EMPTY); // no keyframe
    CHECK(m2_publish(s,&old,idr,sizeof(idr),M2_FRAME)==M2_PACKET);
    CHECK(m2_read(s,&reader,data,sizeof(data),&length,&flags)==M2_PACKET && flags==M2_CODEC && length==sizeof(codec));
    CHECK(!memcmp(data,codec,length));
    CHECK(m2_read(s,&reader,data,sizeof(data),&length,&flags)==M2_PACKET && (flags&M2_IDR));
    CHECK(!memcmp(data,idr,length));
    CHECK(m2_endpoint_close(pool,&a->input,s)==1); CHECK(m2_matches(s,&reader)==M2_PACKET);
    CHECK(m2_endpoint_close(pool,&a->input,s)==0); // exact previous gate
    CHECK(m2_endpoint_close(pool,&a->video,s)==1); CHECK(m2_matches(s,&reader)==M2_STALE);
    CHECK(m2_read(s,&reader,data,sizeof(data),&length,&flags)==M2_STALE); // stop without frames
    CHECK(m2_endpoint_open(pool,&a->video,42,s)==M2_PACKET); CHECK(a->stamp.stream>old.stream);
    CHECK(m2_publish(s,&old,idr,sizeof(idr),M2_FRAME)==M2_STALE);
    CHECK(m2_release(s,&reader)==M2_STALE); CHECK(m2_claim(s,&other,boot)==M2_PACKET);
    CHECK(m2_begin(s,old.receiver,old.stream)==M2_STALE); CHECK(m2_matches(s,&other)==M2_PACKET);
    b=m2_owner_new(pool,(void*)1,1,s); // same address, different receiver lifetime
    CHECK(b && b!=a && b->generation==2 && a->retired);
    CHECK(m2_endpoint_open(pool,&a->video,42,s)==M2_STALE);
    CHECK(m2_endpoint_open(pool,&b->video,99,s)==M2_PACKET);
    CHECK(m2_endpoint_close(pool,&a->video,s)==0 && m2_load(&s->live)==1);
    CHECK(m2_claim(s,&reader,identity)==M2_PACKET);
    CHECK(m2_publish(s,&b->stamp,codec,sizeof(codec),M2_CODEC)==M2_PACKET);
    CHECK(m2_publish(s,&b->stamp,idr,sizeof(idr),M2_FRAME)==M2_PACKET);
    for(i=1;i<M2_SLOTS;++i) CHECK(m2_publish(s,&b->stamp,delta,sizeof(delta),M2_FRAME)==M2_PACKET);
    CHECK(m2_publish(s,&b->stamp,delta,sizeof(delta),M2_FRAME)==M2_FULL);
    CHECK(m2_matches(s,&reader)==M2_STALE); // overflow retires, no corrupt prediction chain
    CHECK(m2_stream_start(pool,b,s)==M2_PACKET); CHECK(m2_claim(s,&reader,identity)==M2_PACKET);
    CHECK(m2_publish(s,&b->stamp,codec,sizeof(codec),M2_CODEC)==M2_PACKET);
    CHECK(m2_publish(s,&b->stamp,codec,sizeof(codec),M2_CODEC)==M2_PACKET); // identical repeat
    CHECK(m2_publish(s,&b->stamp,idr,sizeof(idr),M2_CODEC)==M2_CODEC_CHANGED);
    CHECK(m2_stream_start(pool,b,s)==M2_PACKET); CHECK(m2_lock(s));
    CHECK(m2_publish(s,&b->stamp,idr,sizeof(idr),M2_FRAME)==M2_BUSY);
    m2_stop(s); CHECK(m2_load(&s->live)==0); m2_unlock(s); // closure never waits on a crashed copy lock
    CHECK(m2_owner_new(pool,(void*)2,0,s)==NULL); CHECK(b->retired); // preserve occupied native slots
    while(pool->count<M2_OWNER_LIMIT) CHECK(m2_owner_new(pool,(void*)3,1,s));
    CHECK(m2_owner_new(pool,(void*)3,1,s)==NULL); CHECK(pool->count==M2_OWNER_LIMIT);
    s->version=1; CHECK(!m2_valid(s));
    free(s); free(pool); puts("receiver ownership, dynamic channel, codec/keyframe, stop, reuse and bounded ring tests passed"); return 0;
}

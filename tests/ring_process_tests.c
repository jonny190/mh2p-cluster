/* CC BY-NC-SA 4.0; see LICENSE.md.
 * Two real Windows processes share the production v2 transport in an actual
 * file mapping. H.264 bytes are synthetic; no vendor decoder/Screen is used.
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "cluster_h264_shm.h"
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
#define GENERATIONS 100u
#define FRAMES 20u
#define PAYLOAD 32768u
typedef struct { volatile uint32_t phase,received,errors; cluster_h264_shm_t ring; } shared_test;
static int wait_value(volatile uint32_t *value,uint32_t target) {
    ULONGLONG deadline=GetTickCount64()+5000;
    while(m2_load(value)!=target) { if(GetTickCount64()>deadline) return 0; Sleep(0); }
    return 1;
}
static const uint8_t codec[]={0,0,0,1,0x67,1,0,0,1,0x68,2};
static void payload(uint8_t *p,uint32_t gen,uint32_t frame) {
    uint32_t i;
    memset(p,0xab,PAYLOAD); p[0]=p[1]=0; p[2]=1; p[3]=frame?0x41:0x65;
    memcpy(p+4,&gen,4); memcpy(p+8,&frame,4);
    for(i=12;i<PAYLOAD;++i) p[i]=(uint8_t)((gen+frame+i)%251+1);
}
int main(int argc,char **argv) {
    HANDLE file,map; shared_test *s; int child=argc>2;
    CHECK(argc>=2);
    file=CreateFileA(argv[1],GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,
        child?OPEN_EXISTING:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL); CHECK(file!=INVALID_HANDLE_VALUE);
    map=CreateFileMappingA(file,NULL,PAGE_READWRITE,0,(DWORD)sizeof(*s),NULL); CHECK(map);
    s=MapViewOfFile(map,FILE_MAP_READ|FILE_MAP_WRITE,0,0,sizeof(*s)); CHECK(s);
    if(child) {
        m2_reader reader,previous; uint8_t bytes[PAYLOAD],expected[PAYLOAD]; uint32_t gen,frame,n,flags;
        for(gen=1;gen<=GENERATIONS;++gen) {
            uint32_t identity[4]={gen,2,3,4}; CHECK(wait_value(&s->phase,1));
            if(gen>1) CHECK(m2_matches(&s->ring,&previous)==M2_STALE && m2_release(&s->ring,&previous)==M2_STALE);
            CHECK(m2_claim_pid(&s->ring,&reader,identity,(uint32_t)GetCurrentProcessId())==M2_PACKET);
            m2_store(&s->received,0); m2_store(&s->phase,2);
            frame=0;
            ULONGLONG progress_deadline=GetTickCount64()+5000;
            while(frame<FRAMES) {
                CHECK(GetTickCount64()<progress_deadline);
                int result=m2_read(&s->ring,&reader,bytes,sizeof(bytes),&n,&flags);
                CHECK(result>=0);
                if(result!=M2_PACKET) { Sleep(0); continue; }
                if(flags==M2_CODEC) { CHECK(n==sizeof(codec) && !memcmp(bytes,codec,n)); continue; }
                payload(expected,gen,frame); CHECK(n==PAYLOAD && !memcmp(bytes,expected,n));
                ++frame; m2_store(&s->received,frame); progress_deadline=GetTickCount64()+5000;
            }
            CHECK(wait_value(&s->phase,3));
            CHECK(m2_matches(&s->ring,&reader)==M2_STALE);
            CHECK(m2_read(&s->ring,&reader,bytes,sizeof(bytes),&n,&flags)==M2_STALE);
            previous=reader; m2_store(&s->phase,4);
        }
    } else {
        PROCESS_INFORMATION process; STARTUPINFOA startup; char exe[MAX_PATH],command[1024];
        uint32_t nonce[4]={1,2,3,4},gen,frame; uint8_t bytes[PAYLOAD];
        memset(s,0,sizeof(*s)); m2_init(&s->ring,nonce,(uint32_t)GetCurrentProcessId());
        memset(&startup,0,sizeof(startup)); startup.cb=sizeof(startup);
        CHECK(GetModuleFileNameA(NULL,exe,sizeof(exe))); snprintf(command,sizeof(command),"\"%s\" \"%s\" child",exe,argv[1]);
        CHECK(CreateProcessA(NULL,command,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&startup,&process));
        for(gen=1;gen<=GENERATIONS;++gen) {
            CHECK(m2_begin(&s->ring,gen,gen)==M2_PACKET); m2_store(&s->phase,1);
            CHECK(wait_value(&s->phase,2));
            {
                int result;
                do { result=m2_publish(&s->ring,&s->ring.stamp,codec,sizeof(codec),M2_CODEC); if(result==M2_BUSY) Sleep(0); } while(result==M2_BUSY);
                CHECK(result==M2_PACKET);
            }
            for(frame=0;frame<FRAMES;++frame) {
                int result; payload(bytes,gen,frame);
                do { result=m2_publish(&s->ring,&s->ring.stamp,bytes,sizeof(bytes),M2_FRAME); if(result==M2_BUSY) Sleep(0); } while(result==M2_BUSY);
                CHECK(result==M2_PACKET); CHECK(wait_value(&s->received,frame+1));
            }
            m2_stop(&s->ring); m2_store(&s->phase,3); CHECK(wait_value(&s->phase,4));
        }
        CHECK(WaitForSingleObject(process.hProcess,5000)==WAIT_OBJECT_0);
        { DWORD code; CHECK(GetExitCodeProcess(process.hProcess,&code) && code==0); }
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
        puts("100 generations / 2000 checked packets passed across two real shared-memory processes");
    }
    UnmapViewOfFile(s); CloseHandle(map); CloseHandle(file); return 0;
}

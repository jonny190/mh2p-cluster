/* Local P2873 player adapter. CC BY-NC-SA 4.0; see LICENSE.md.
 * Immutable decoder binding: this process never adopts a replacement stream.
 */
static cluster_h264_shm_t *g_m2_player_ring;
static m2_reader g_m2_player_reader;
static volatile sig_atomic_t g_m2_player_quit;
static volatile uint32_t g_m2_can_post;
static void m2_player_signal(int sig) { (void)sig; g_m2_player_quit=1; m2_store(&g_m2_can_post,0); }
static int m2_player_public_matches(void) {
    int fd,result=0;
    struct stat st;
    cluster_h264_shm_t *latest;
    if(kill((pid_t)g_m2_player_ring->writer_pid,0) && errno!=EPERM) return 0;
    fd=shm_open(CLUSTER_SHM_NAME,O_RDWR,0); if(fd<0) return 0;
    if(fstat(fd,&st) || st.st_size!=(off_t)sizeof(*latest)) { close(fd); return 0; }
    latest=(cluster_h264_shm_t*)mmap(NULL,sizeof(*latest),PROT_READ|PROT_WRITE,MAP_SHARED,fd,0); close(fd);
    if(latest!=MAP_FAILED) {
        result=m2_valid(latest) && !memcmp(latest->stamp.boot,g_m2_player_reader.stamp.boot,16);
        munmap(latest,sizeof(*latest));
    }
    return result;
}
static int m2_player_claim(cluster_h264_shm_t *ring) {
    uint32_t identity[4]; int fd,result;
    fd=open("/dev/urandom",O_RDONLY|O_NONBLOCK);
    if(fd<0) fd=open("/dev/random",O_RDONLY|O_NONBLOCK);
    if(fd<0) return M2_INVALID;
    result=read(fd,identity,16)==16; close(fd); if(!result) return M2_INVALID;
    result=m2_claim_pid(ring,&g_m2_player_reader,identity,(uint32_t)getpid());
    if(result==M2_PACKET) g_m2_player_ring=ring;
    return result;
}
/* PID absence is only a memory-consumer proof, never Nvidia fence proof.
 * Recheck the exact nonce/stamp after the OS check so an old observation
 * cannot release a newer consumer. PID reuse conservatively blocks reclaim.
 */
static void m2_player_reap_reader(cluster_h264_shm_t *ring) {
    m2_reader prior; uint32_t pid;
    if(!m2_lock(ring)) return;
    prior.stamp=ring->stamp; memcpy(prior.identity,ring->reader,16); pid=ring->reader_pid;
    m2_unlock(ring);
    if(pid && kill((pid_t)pid,0)<0 && errno==ESRCH) (void)m2_release(ring,&prior);
}
static int m2_player_post(screen_window_t window,screen_buffer_t buffer,int count,int *rectangles,int flags) {
    if(!g_m2_player_ring || !m2_load(&g_m2_can_post) ||
       m2_matches(g_m2_player_ring,&g_m2_player_reader)!=M2_PACKET) return -1;
    return screen_post_window(window,buffer,count,rectangles,flags);
}

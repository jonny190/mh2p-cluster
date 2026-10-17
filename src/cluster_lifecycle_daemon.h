/* Local P2873 lifecycle adaptation. CC BY-NC-SA 4.0; see LICENSE.md.
 * Included by cluster.c after daemon_start_mirror. This replaces the legacy
 * lossy control file ONLY in rebuilt players, never in packaged releases.
 */
#include "cluster_lifecycle.h"
#define LC_CHILD "/tmp/mib2p_cluster_child"

static volatile sig_atomic_t lc_quit = 0;
static void lc_signal(int sig) { (void)sig; lc_quit = 1; }
static uint64_t lc_now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) return 0;
    return (uint64_t)t.tv_sec * 1000ULL + (uint64_t)t.tv_nsec / 1000000ULL;
}
static int lc_publish(const lc_owner *s) {
    char line[LC_LINE_SIZE];
    FILE *f;
    int ok;
    if (!lc_format(s, line, sizeof(line))) return 0;
    f = fopen(LC_STATUS ".new", "w"); if (!f) return 0;
    ok = fputs(line, f) >= 0;
    if (fclose(f)) ok = 0;
    if (ok) ok = !rename(LC_STATUS ".new", LC_STATUS);
    return ok;
}
static int lc_record_child(pid_t pid) {
    FILE *f = fopen(LC_CHILD ".new", "w");
    int ok;
    if (!f) return 0;
    ok = pid > 0 ? fprintf(f, "%ld\n", (long)pid) > 0 : fputs("PENDING\n", f) >= 0;
    if (fclose(f)) ok = 0;
    return ok && !rename(LC_CHILD ".new", LC_CHILD);
}
/* Exit is the process ownership barrier. A timeout is ERROR, never STOPPED.
 * SIGKILL is restricted to our exact unreaped child PID, not a name search.
 */
static int lc_stop_child(pid_t *pid) {
    int phase, status;
    if (*pid <= 0) return 1;
    for (phase = 0; phase < 2; ++phase) {
        uint64_t start = lc_now();
        kill(*pid, phase ? SIGKILL : SIGTERM);
        int attempt;
        for (attempt = 0; attempt < 100 && lc_now() - start < 1000ULL; ++attempt) {
            pid_t r = waitpid(*pid, &status, WNOHANG);
            if (r == *pid || (r < 0 && errno == ECHILD)) { *pid = -1; return 1; }
            if (r < 0 && errno != EINTR) return 0;
            usleep(10000);
        }
    }
    return 0;
}
static int daemon_run(int argc, char **argv) {
    lc_owner state;
    char daemon[33], defaults[256] = "", request[LC_LINE_SIZE];
    unsigned char nonce[16];
    pid_t child = -1;
    int i, fd;
    FILE *f;
    /* A second daemon must not truncate status or claim another child's PID.
     * fcntl locks are released by the kernel on exit (including SIGKILL).
     */
    struct flock lock;
    int lockfd = open("/tmp/mib2p_cluster_daemon.lock", O_CREAT | O_RDWR, 0600);
    memset(&lock, 0, sizeof(lock)); lock.l_type = F_WRLCK; lock.l_whence = SEEK_SET;
    if (lockfd < 0 || fcntl(lockfd, F_SETLK, &lock)) return 1;
    fcntl(lockfd, F_SETFD, FD_CLOEXEC);
    /* After SIGKILL the former child can outlive its daemon. A replacement
     * daemon cannot waitpid that process, and must not claim a release barrier
     * just because its own child variable starts empty. Do not signal a saved
     * PID (it may have been reused); refuse startup until it no longer exists.
     */
    f = fopen(LC_CHILD, "r");
    if (f) {
        long previous;
        int valid = fscanf(f, "%ld", &previous) == 1 && previous > 0 && previous <= 0x7fffffffL;
        fclose(f);
        if (!valid || kill((pid_t)previous, 0) == 0 || errno != ESRCH) {
            fprintf(stderr, "[lifecycle] prior child release unproved; refusing daemon replacement\n");
            close(lockfd); return 1;
        }
        if (unlink(LC_CHILD)) { close(lockfd); return 1; }
    }
    if (lc_now() == 0) { close(lockfd); return 1; }
    fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0 || read(fd, nonce, sizeof(nonce)) != (ssize_t)sizeof(nonce)) {
        if (fd >= 0) close(fd);
        fprintf(stderr, "[lifecycle] cannot obtain fresh daemon identity; refusing takeover\n");
        close(lockfd); return 1;
    }
    close(fd);
    for (i = 0; i < 16; ++i) sprintf(daemon + i * 2, "%02x", nonce[i]);
    lc_init(&state, daemon);
    for (i = 1; i < argc; ++i) {
        size_t used;
        if (!strcmp(argv[i], "daemon") || !strncmp(argv[i], "poll=", 5)) continue;
        used = strlen(defaults);
        if (used + strlen(argv[i]) + 2 >= sizeof(defaults)) { close(lockfd); return 1; }
        if (used) strcat(defaults, " ");
        strcat(defaults, argv[i]);
    }
    signal(SIGTERM, lc_signal); signal(SIGINT, lc_signal);
    if (!lc_publish(&state)) { close(lockfd); return 1; }
    chmod(LC_STATUS, 0644);
    printf("[lifecycle] daemon=%s; protocol v1; lease=%llu ms; OEM owns display at idle\n",
        daemon, (unsigned long long)LC_LEASE_MS);
    while (!lc_quit) {
        uint64_t now = lc_now();
        int accepted = 0;
        lc_request r;
        if (now == 0) break;
        if (child > 0) {
            int status;
            pid_t reaped = waitpid(child, &status, WNOHANG);
            if (reaped == child || (reaped < 0 && errno == ECHILD)) {
                child = -1; state.busy = 0; state.retired = 1;
                if (unlink(LC_CHILD) && errno != ENOENT) break;
                strcpy(state.state, "STOPPED");
                printf("[lifecycle] child exited; session retired; OEM restoration required\n");
                if (!lc_publish(&state)) break;
            }
        }
        if (lc_expired(&state, now)) {
            state.retired = 1;
            state.busy = !lc_stop_child(&child);
            if (!state.busy && unlink(LC_CHILD) && errno != ENOENT) break;
            strcpy(state.state, state.busy ? "ERROR" : "STOPPED");
            printf("[lifecycle] lease expired; child=%d; state=%s\n", (int)child, state.state);
            if (!lc_publish(&state)) break;
        }
        /* Immutable whole-file snapshots: no read/truncate race. A truncated
         * or oversized record is ignored and cannot renew the lease.
         */
        f = fopen(LC_REQUEST, "r");
        if (f) {
            size_t n = fread(request, 1, sizeof(request) - 1, f);
            int extra = fgetc(f);
            fclose(f); request[n] = 0;
            if (extra == EOF && strlen(request) == n && lc_parse(request, &r)) accepted = lc_accept(&state, &r, now);
        }
        if (accepted) {
            printf("[lifecycle] owner=%s generation=%llu sequence=%llu action=%s\n",
                r.owner, (unsigned long long)r.generation, (unsigned long long)r.sequence, r.action);
            if (!strcmp(r.action, "STOP")) {
                state.busy = !lc_stop_child(&child);
                if (!state.busy && unlink(LC_CHILD) && errno != ENOENT) break;
                strcpy(state.state, state.busy ? "ERROR" : "STOPPED");
                /* No idle/boot splash: STOP relinquishes displayable 33. */
            } else if (!strcmp(r.action, "PREPARE")) {
                if (!state.busy) {
                    char args[512];
                    int n = snprintf(args, sizeof(args), "capture=h264 skip_boot_splash=1 lifecycle_parent=%d %s %s prepare",
                        (int)getpid(), r.args, defaults);
                    /* Reserve before fork. A SIGKILL between fork and recording
                     * the PID leaves PENDING, which a new daemon must refuse.
                     * Never advertise a false exit barrier for an orphan.
                     */
                    if (n <= 0 || n >= (int)sizeof(args) || !lc_record_child(0)) {
                        state.retired = 1; strcpy(state.state, "ERROR");
                        if (!lc_publish(&state)) break;
                        continue;
                    }
                    child = daemon_start_mirror(argv[0], args);
                    if (child > 0) {
                        if (!lc_record_child(child)) {
                            state.busy = !lc_stop_child(&child);
                            state.retired = 1; strcpy(state.state, "ERROR");
                            if (!lc_publish(&state)) break;
                            continue;
                        }
                    } else unlink(LC_CHILD); /* fork failed: no owned child */
                    state.busy = child > 0;
                    state.retired = !state.busy;
                    strcpy(state.state, state.busy ? "PREPARED" : "ERROR");
                }
            } else if (!strcmp(r.action, "RENDER")) {
                if (child > 0 && !strcmp(state.state, "PREPARED")) {
                    if (!kill(child, SIGUSR1)) strcpy(state.state, "RUNNING");
                    else strcpy(state.state, "ERROR");
                }
            }
            if (!lc_publish(&state)) break;
        }
        usleep(100000); /* bounded poll independent of video arrival */
    }
    state.retired = 1; state.busy = !lc_stop_child(&child);
    if (!state.busy) unlink(LC_CHILD);
    strcpy(state.state, state.busy ? "ERROR" : "STOPPED"); lc_publish(&state);
    close(lockfd);
    return state.busy ? 1 : 0;
}

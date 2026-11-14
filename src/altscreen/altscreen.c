/*
 * altscreen - CarPlay instrument-cluster video ("altScreen") on Audi MH2p.
 * Built by src/altscreen/build.sh, shipped as Update/altscreen/altscreen.so and
 * installed by the ModKit installer with enable_carplay_altscreen.txt.
 * Copyright (c) 2026 CrEaK (https://github.com/CrEaK). GPL-3.0-or-later.
 * Protocol knowledge and the overall approach follow luka-dev/mib2q-carplay-rgi
 * (hook/altscreen, GPL-3.0); this file is a separate, smaller implementation
 * for libairplay 320.17.1 (MH2p_ER_AUG35_P2711).
 *
 * LD_PRELOAD into dio_manager. Gets the iPhone to send the second CarPlay
 * video stream (type 111, "altScreen", the instrument cluster scene) and
 * hands it to the mh2p-cluster daemon, which decodes it onto the cockpit.
 *
 * On by default once installed; every hook passes straight through while
 * cluster/altscreen_off or /tmp/altscreen_off exists when the iPhone
 * connects, or when libairplay is not the one build this was made for.
 *
 * What it does when on:
 *   control SETUP response  += enabledFeatures [altScreen, viewAreas]
 *   /info                   += extendedFeatures altScreen, viewAreas and a
 *                              displays / displayPanels list [main, cluster]
 *   media SETUP with 111    -> own TCP listener, decrypt (ChaCha20-Poly1305
 *                              over wireless, AES-CTR otherwise), AVCC to
 *                              Annex-B into the daemon's ring
 *                              /cluster_h264_shm (and /tmp/alt111.h264 while
 *                              /tmp/altscreen_rec exists)
 *   after the stream starts -> showUI (Maps cluster scene) and keyframes
 *                              addressed to the cluster display uuid
 *   while the stream runs   -> libairplay's idle keep-alive timeout (30 s,
 *                              select() interposed) is held off, because the
 *                              phone's keep-alives travel inside stream 111
 *   day / night changes     -> the head unit's setNightMode is repeated for the
 *                              cluster display uuid
 *
 * The cockpit side is the installed cluster mod: when CarPlay owns the main
 * screen its Java part writes "start mode=<mirror.mode> ..." to
 * /tmp/cluster_ctl and hides the Audi map. With mirror.mode set to
 * "fill capture=h264" the daemon starts its H.264 decoder on this ring
 * instead of the screen mirror.
 *
 * libairplay 320.17.1 facts used here (from its disassembly):
 *   AirPlayReceiverSessionSetup @0x370f0: r0 session (kept in sl), r1 request,
 *     r2 response out-pointer. Stream types above 110 are logged and skipped.
 *   type 110 branch, key: with a pairing session at *(session+996) (wireless
 *     CarPlay) it derives a 32-byte ChaCha20-Poly1305 key @0x34d46 with
 *     PairingSessionDeriveKey(pair, "DataStream-Salt%llu" of the connection
 *     id, "DataStream-Output-Encryption-Key", 32). Without one it falls back
 *     to AirPlay_DeriveAESKeySHA512ForScreen(session+504, 16,
 *     streamConnectionID, key, iv) @0x38594 (AES-CTR).
 *   ProcessFrames with ChaCha (@0x3cd4e): AAD = the 128-byte header as
 *     received, ciphertext = body minus its last 16 bytes, tag = those 16
 *     bytes, nonce = 8-byte little-endian counter from 0, one step per
 *     encrypted (opcode 0) frame.
 *     ServerSocketOpen(*(u8 *)(session+133), SOCK_STREAM, IPPROTO_TCP, -6030,
 *     &port, -180224, &sock) @0x373e4; SocketSetQoS(sock, 12) right after.
 *   SocketAccept(listen, timeoutSecs, &fd, addr) @0x7dc60.
 *   AES_CTR context is 280 bytes (fields up to +276).
 *   AirPlayReceiverSessionForceKeyFrame builds {type} and calls
 *     AirPlayReceiverSessionSendCommand(session, cmd, completion, ctx).
 *   CFRetain -> CFLRetain returns NULL for anything that is not a CFL object.
 *   The CF* names are thin wrappers over CFL* with CoreFoundation signatures.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define OFF_FLAG      "/mnt/app/eso/bin/apps/cluster/altscreen_off"
#define OFF_FLAG_TMP  "/tmp/altscreen_off"
#define LOG_PATH      "/tmp/altscreen.log"
#define LOG_LIMIT     (1024 * 1024)
#define REC_FLAG      "/tmp/altscreen_rec"
#define VIDEO_PATH    "/tmp/alt111.h264"
#define VIDEO_LIMIT   (24 * 1024 * 1024)

#define OFF_AESKEY    504
#define OFF_FAMILY    133
#define OFF_PAIRING   996
#define ALT_TYPE      111
#define ALT_PORT      (-6031)          /* negative = preferred port, any if taken */
#define RCVBUF        (-180224)        /* same as stock type 110 */

/* Audi A6 C8 virtual cockpit: 12.3", 1920x720 (the cluster daemon finds the
 * surface as 1920x722). 1920x720 at 312 mm diagonal = 292 x 110 mm. */
#define CL_W          1920
#define CL_H          720
#define CONF_TMP      "/tmp/altscreen.conf"
#define CONF_PATH     "/mnt/app/eso/bin/apps/cluster/altscreen.conf"
#define VIEW_PATH     "/tmp/altscreen_view"
#define ZOOM_PATH     "/tmp/altscreen_cmd"
#define CL_UUID       "A6C80111-C1A5-4E20-B0C0-1057E12A0D10"
#define CL_STREAM     "A6C80111-C1A5-4E20-B0C0-1057E12A0D11"
#define CL_URL        "maps:/car/instrumentcluster?showETA=yes&maneuverLayout=rightAligned&showSpeedLimit="

#define ENC_UTF8      0x08000100

/* ---------------------------------------------------------------- logging */

static pthread_mutex_t g_log_mtx = PTHREAD_MUTEX_INITIALIZER;
static long g_logged;

static void plog(const char *fmt, ...) {
    FILE *f;
    va_list ap;
    struct timespec ts;
    pthread_mutex_lock(&g_log_mtx);
    if (g_logged < LOG_LIMIT && (f = fopen(LOG_PATH, "a")) != NULL) {
        clock_gettime(CLOCK_MONOTONIC, &ts);
        g_logged += fprintf(f, "[%5ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
        va_start(ap, fmt);
        g_logged += vfprintf(f, fmt, ap);
        va_end(ap);
        g_logged += fprintf(f, "\n");
        fclose(f);
    }
    pthread_mutex_unlock(&g_log_mtx);
}

/* The hook relies on struct offsets and call conventions of exactly one
 * libairplay build (320.17.1 of MH2p_ER_AUG35_P2711, 1024092 bytes). The
 * distances between five of its exports (none of them interposed here) are
 * a cheap fingerprint of that build; anything else keeps the hook passive. */
static int g_build_ok = -1;
static pthread_once_t g_build_once = PTHREAD_ONCE_INIT;

static long sym_delta(const char *a, const char *b) {
    void *pa = dlsym(RTLD_DEFAULT, a), *pb = dlsym(RTLD_DEFAULT, b);
    if (!pa || !pb) return 0x7fffffffL;
    return (long)((uintptr_t)pa & ~(uintptr_t)1) - (long)((uintptr_t)pb & ~(uintptr_t)1);
}

static void check_build(void) {
    static const char *base = "AirPlay_DeriveAESKeySHA512ForScreen";
    long d1 = sym_delta("PairingSessionDeriveKey", base);
    long d2 = sym_delta("chacha20_poly1305_decrypt_all_64x64", base);
    long d3 = sym_delta("ServerSocketOpen", base);
    long d4 = sym_delta("AirPlayReceiverSessionScreen_SetChaChaSecurityInfo", base);
    g_build_ok = (d1 == 0x43628L && d2 == 0x2c264L && d3 == 0x40888L && d4 == -0x27b4L);
    plog("libairplay build %s (deltas %lx %lx %lx %lx)",
         g_build_ok ? "320.17.1 / P2711 recognised" : "UNKNOWN - altScreen stays off", d1, d2, d3, d4);
}

/* On unless switched off: cluster/altscreen_off (permanent) or
 * /tmp/altscreen_off (until the next restart), and only on the known build. */
/* Survives the restart that empties /tmp: one line per session event in the
 * ModKit log folder, capped at 64 KB (older lines move to .old). */
#define SESSION_LOG   "/mnt/ota/modkit/Logs/altscreen-sessions.log"

static void slog(const char *fmt, ...) {
    struct stat st;
    struct timespec ts;
    va_list ap;
    FILE *f;
    if (stat(SESSION_LOG, &st) == 0 && st.st_size > 64 * 1024)
        rename(SESSION_LOG, SESSION_LOG ".old");
    f = fopen(SESSION_LOG, "a");
    if (!f) return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(f, "%6ld s  ", (long)ts.tv_sec);
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

/* Present while decoded frames reach the cluster daemon. The mod's
 * KombiMapGuard shows the video displayable only then, so a stream that never
 * arrives leaves Audi's map in the cockpit instead of a black panel. */
#define VIDEO_FLAG    "/tmp/altscreen_video"

static void video_flag(int on) {
    if (on) {
        FILE *f = fopen(VIDEO_FLAG, "w");
        if (f) fclose(f);
    } else {
        unlink(VIDEO_FLAG);
    }
}

static int is_on(void) {
    pthread_once(&g_build_once, check_build);
    return g_build_ok == 1 && access(OFF_FLAG, F_OK) != 0 && access(OFF_FLAG_TMP, F_OK) != 0;
}

/* --------------------------------------------- CFLite dump (as in stage 1) */

typedef int  (*fn_getcount_t)(const void *, long *);
typedef int  (*fn_apply_t)(const void *, void (*)(const void *, const void *, void *), void *);
typedef int  (*fn_arrget_t)(const void *, long, const void **);
typedef int  (*fn_strptr_t)(const void *, const char **, size_t *);
typedef int  (*fn_dataptr_t)(const void *, const void **, size_t *);
typedef long (*fn_numtype_t)(const void *);
typedef int  (*fn_numval_t)(const void *, long, void *);

static struct {
    int ok;
    fn_getcount_t dict_count, arr_count;
    fn_apply_t dict_apply;
    fn_arrget_t arr_get;
    fn_strptr_t str_ptr;
    fn_dataptr_t data_ptr;
    fn_numtype_t num_type;
    fn_numval_t num_val;
    const void *b_true, *b_false;
} cfl;

static void cfl_resolve(void) {
    if (cfl.ok) return;
    cfl.dict_count = (fn_getcount_t)dlsym(RTLD_DEFAULT, "CFLDictionaryGetCount");
    cfl.arr_count  = (fn_getcount_t)dlsym(RTLD_DEFAULT, "CFLArrayGetCount");
    cfl.dict_apply = (fn_apply_t)dlsym(RTLD_DEFAULT, "CFLDictionaryApplyFunction");
    cfl.arr_get    = (fn_arrget_t)dlsym(RTLD_DEFAULT, "CFLArrayGetValueAtIndex");
    cfl.str_ptr    = (fn_strptr_t)dlsym(RTLD_DEFAULT, "CFLStringGetCStringPtr");
    cfl.data_ptr   = (fn_dataptr_t)dlsym(RTLD_DEFAULT, "CFLDataGetDataPtr");
    cfl.num_type   = (fn_numtype_t)dlsym(RTLD_DEFAULT, "CFLNumberGetType");
    cfl.num_val    = (fn_numval_t)dlsym(RTLD_DEFAULT, "CFLNumberGetValue");
    cfl.b_true     = dlsym(RTLD_DEFAULT, "kCFLBooleanTrue");
    cfl.b_false    = dlsym(RTLD_DEFAULT, "kCFLBooleanFalse");
    cfl.ok = cfl.dict_count && cfl.arr_count && cfl.dict_apply && cfl.arr_get &&
             cfl.str_ptr && cfl.data_ptr && cfl.num_type && cfl.num_val;
}

typedef struct { char *buf; size_t len, cap; } sbuf_t;

static void sb_add(sbuf_t *b, const char *fmt, ...) {
    va_list ap;
    int n;
    if (b->len + 1 >= b->cap) return;
    va_start(ap, fmt);
    n = vsnprintf(b->buf + b->len, b->cap - b->len, fmt, ap);
    va_end(ap);
    if (n > 0) b->len += ((size_t)n < b->cap - b->len) ? (size_t)n : b->cap - b->len - 1;
}

static void dump_obj(sbuf_t *b, const void *o, int depth);

typedef struct { sbuf_t *b; int depth; } apply_ctx_t;

static void dump_pair(const void *k, const void *v, void *c) {
    apply_ctx_t *a = (apply_ctx_t *)c;
    sb_add(a->b, "\n%*s", a->depth * 2, "");
    dump_obj(a->b, k, a->depth);
    sb_add(a->b, " = ");
    dump_obj(a->b, v, a->depth);
}

static void dump_obj(sbuf_t *b, const void *o, int depth) {
    const unsigned char *h = (const unsigned char *)o;
    if (!o) { sb_add(b, "NULL"); return; }
    if (depth > 12) { sb_add(b, "..."); return; }
    if (o == cfl.b_true || (cfl.b_true && o == *(const void * const *)cfl.b_true)) { sb_add(b, "true"); return; }
    if (o == cfl.b_false || (cfl.b_false && o == *(const void * const *)cfl.b_false)) { sb_add(b, "false"); return; }
    if (h[0] != 'V') { sb_add(b, "<?%p>", o); return; }
    switch (h[1]) {
    case 5: {
        long n = 0;
        apply_ctx_t a;
        cfl.dict_count(o, &n);
        sb_add(b, "{ /* %ld */", n);
        a.b = b; a.depth = depth + 1;
        cfl.dict_apply(o, dump_pair, &a);
        sb_add(b, "\n%*s}", depth * 2, "");
        break; }
    case 1: {
        long n = 0, i;
        cfl.arr_count(o, &n);
        sb_add(b, "[ /* %ld */", n);
        for (i = 0; i < n; i++) {
            const void *v = NULL;
            cfl.arr_get(o, i, &v);
            sb_add(b, "\n%*s", (depth + 1) * 2, "");
            dump_obj(b, v, depth + 1);
        }
        sb_add(b, "\n%*s]", depth * 2, "");
        break; }
    case 7: {
        const char *s = NULL;
        size_t n = 0;
        if (cfl.str_ptr(o, &s, &n) == 0 && s) sb_add(b, "\"%.*s\"", (int)(n > 300 ? 300 : n), s);
        else sb_add(b, "<str?>");
        break; }
    case 6: {
        long t = cfl.num_type(o);
        if (t == 5 || t == 6 || t == 12 || t == 13) {
            double d = 0;
            cfl.num_val(o, 13, &d);
            sb_add(b, "%g", d);
        } else {
            long long v = 0;
            cfl.num_val(o, 4, &v);
            sb_add(b, "%lld", v);
        }
        break; }
    case 3: {
        const void *p = NULL;
        size_t n = 0, i;
        cfl.data_ptr(o, &p, &n);
        sb_add(b, "<data %u:", (unsigned)n);
        for (i = 0; p && i < n && i < 32; i++) sb_add(b, "%02x", ((const unsigned char *)p)[i]);
        sb_add(b, n > 32 ? "...>" : ">");
        break; }
    case 2: sb_add(b, "bool(%u)", (unsigned)h[2]); break;
    default: sb_add(b, "<type %u>", (unsigned)h[1]); break;
    }
}

static pthread_mutex_t g_dump_mtx = PTHREAD_MUTEX_INITIALIZER;

static void plog_obj(const char *label, const void *obj) {
    sbuf_t b;
    static char buf[64 * 1024];   /* shared, guarded by g_dump_mtx */
    if (!obj) { plog("%s: NULL", label); return; }
    cfl_resolve();
    if (!cfl.ok) { plog("%s: %p (CFL accessors not found)", label, obj); return; }
    pthread_mutex_lock(&g_dump_mtx);
    b.buf = buf; b.len = 0; b.cap = sizeof(buf); buf[0] = 0;
    dump_obj(&b, obj, 0);
    plog("%s: %s", label, buf);
    pthread_mutex_unlock(&g_dump_mtx);
}

/* ------------------------------------------ libairplay functions (dlsym) */

static struct {
    void *(*str)(void *, const char *, unsigned);              /* CFStringCreateWithCString */
    void *(*arr_new)(void *, long, const void *);              /* CFArrayCreateMutable */
    void  (*arr_add)(void *, const void *);                    /* CFArrayAppendValue */
    long  (*arr_count)(const void *);                          /* CFArrayGetCount */
    void *(*arr_at)(const void *, long);                       /* CFArrayGetValueAtIndex */
    void *(*dict_new)(void *, long, const void *, const void *); /* CFDictionaryCreateMutable */
    void *(*dict_copy)(void *, long, const void *);            /* CFDictionaryCreateMutableCopy */
    void *(*dict_get)(const void *, const void *);             /* CFDictionaryGetValue */
    void  (*dict_set)(void *, const void *, const void *);     /* CFDictionarySetValue */
    int   (*dict_seti)(void *, const void *, int64_t);         /* CFDictionarySetInt64 */
    int64_t (*dict_geti)(const void *, const void *, int *);   /* CFDictionaryGetInt64 */
    unsigned long (*type_of)(const void *);                    /* CFGetTypeID */
    unsigned long (*arr_type)(void);                           /* CFArrayGetTypeID */
    unsigned long (*dict_type)(void);                          /* CFDictionaryGetTypeID */
    unsigned long (*str_type)(void);                           /* CFStringGetTypeID */
    int   (*str_cstr)(const void *, char *, long, unsigned);   /* CFStringGetCString */
    void *(*retain)(void *);                                   /* CFRetain */
    void  (*release)(void *);                                  /* CFRelease */
    const void *arr_cb, *key_cb, *val_cb;
    int   (*aes_init)(void *, const uint8_t *, const uint8_t *);
    int   (*aes_update)(void *, const void *, size_t, void *);
    void  (*aes_final)(void *);
    void  (*derive)(const void *, size_t, uint64_t, uint8_t *, uint8_t *);
    int   (*pair_derive)(void *, const void *, size_t, const void *, size_t, size_t, uint8_t *);
    /* (key, nonce, aad, aadLen, src, len, dst, tag), as called in
     * PairingSessionFindPeer @0x81f48 */
    int   (*chacha_open)(const uint8_t *, const uint8_t *, const void *, size_t,
                         const void *, size_t, void *, const uint8_t *);
    int   (*sock_open)(int, int, int, int, int *, int, int *);
    void  (*sock_qos)(int, int);
    int   (*sock_accept)(int, int, int *, void *);
    int   (*send_cmd)(void *, void *, void *, void *);
} ap;

static pthread_once_t g_ap_once = PTHREAD_ONCE_INIT;
static int g_ap_ok;

#define SYM(field, name) do { \
        *(void **)&ap.field = dlsym(RTLD_DEFAULT, name); \
        if (!ap.field) { plog("missing symbol %s", name); ok = 0; } \
    } while (0)

static void ap_resolve(void) {
    int ok = 1;
    SYM(str, "CFStringCreateWithCString");
    SYM(arr_new, "CFArrayCreateMutable");
    SYM(arr_add, "CFArrayAppendValue");
    SYM(arr_count, "CFArrayGetCount");
    SYM(arr_at, "CFArrayGetValueAtIndex");
    SYM(dict_new, "CFDictionaryCreateMutable");
    SYM(dict_copy, "CFDictionaryCreateMutableCopy");
    SYM(dict_get, "CFDictionaryGetValue");
    SYM(dict_set, "CFDictionarySetValue");
    SYM(dict_seti, "CFDictionarySetInt64");
    SYM(dict_geti, "CFDictionaryGetInt64");
    SYM(type_of, "CFGetTypeID");
    SYM(arr_type, "CFArrayGetTypeID");
    SYM(dict_type, "CFDictionaryGetTypeID");
    SYM(str_type, "CFStringGetTypeID");
    SYM(str_cstr, "CFStringGetCString");
    SYM(retain, "CFRetain");
    SYM(release, "CFRelease");
    SYM(arr_cb, "kCFLArrayCallBacksCFLTypes");
    SYM(key_cb, "kCFLDictionaryKeyCallBacksCFLTypes");
    SYM(val_cb, "kCFLDictionaryValueCallBacksCFLTypes");
    SYM(aes_init, "AES_CTR_Init");
    SYM(aes_update, "AES_CTR_Update");
    SYM(aes_final, "AES_CTR_Final");
    SYM(derive, "AirPlay_DeriveAESKeySHA512ForScreen");
    SYM(pair_derive, "PairingSessionDeriveKey");
    SYM(chacha_open, "chacha20_poly1305_decrypt_all_64x64");
    SYM(sock_open, "ServerSocketOpen");
    SYM(sock_qos, "SocketSetQoS");
    SYM(sock_accept, "SocketAccept");
    SYM(send_cmd, "AirPlayReceiverSessionSendCommand");
    g_ap_ok = ok;
    plog("libairplay symbols %s", ok ? "complete" : "INCOMPLETE - altScreen stays off");
}

static int ap_ready(void) {
    pthread_once(&g_ap_once, ap_resolve);
    return g_ap_ok;
}

/* ------------------------------------------------------ CF build helpers */

static void rel(void *o) { if (o) ap.release(o); }

static void *cstr(const char *s) { return ap.str(NULL, s, ENC_UTF8); }

static int set_val(void *d, const char *key, void *val) {   /* val borrowed */
    void *k = cstr(key);
    if (!k || !val) { rel(k); return -1; }
    ap.dict_set(d, k, val);
    rel(k);
    return 0;
}

static int set_str(void *d, const char *key, const char *val) {
    void *v = cstr(val);
    int rc = set_val(d, key, v);
    rel(v);
    return rc;
}

static int set_int(void *d, const char *key, int64_t val) {
    void *k = cstr(key);
    int rc = k ? ap.dict_seti(d, k, val) : -1;
    rel(k);
    return rc;
}

static void *get_val(const void *d, const char *key) {      /* borrowed */
    void *k = cstr(key);
    void *v = (k && d) ? ap.dict_get(d, k) : NULL;
    rel(k);
    return v;
}

static int64_t get_int(const void *d, const char *key, int *err) {
    void *k = cstr(key);
    int64_t v = 0;
    *err = -1;
    if (k) v = ap.dict_geti(d, k, err);
    rel(k);
    return v;
}

static int is_type(const void *o, unsigned long (*type)(void)) {
    return o && ap.type_of(o) == type();
}

static int arr_add_str(void *arr, const char *s) {
    void *v = cstr(s);
    if (!v) return -1;
    ap.arr_add(arr, v);
    rel(v);
    return 0;
}

static int arr_has_str(const void *arr, const char *s) {
    long i, n = ap.arr_count(arr);
    char tmp[64];
    for (i = 0; i < n; i++) {
        const void *e = ap.arr_at(arr, i);
        if (is_type(e, ap.str_type) && ap.str_cstr(e, tmp, sizeof(tmp), ENC_UTF8) &&
            strcmp(tmp, s) == 0)
            return 1;
    }
    return 0;
}

/* new array = old entries (if any) + altScreen + viewAreas, no duplicates */
static void *features_plus_alt(const void *old) {
    void *arr = ap.arr_new(NULL, 0, ap.arr_cb);
    long i, n;
    if (!arr) return NULL;
    if (is_type(old, ap.arr_type)) {
        n = ap.arr_count(old);
        for (i = 0; i < n; i++) ap.arr_add(arr, ap.arr_at(old, i));
    }
    if ((!arr_has_str(arr, "altScreen") && arr_add_str(arr, "altScreen") != 0) ||
        (!arr_has_str(arr, "viewAreas") && arr_add_str(arr, "viewAreas") != 0)) {
        rel(arr);
        return NULL;
    }
    return arr;
}

static void *rect(int64_t x, int64_t y, int64_t w, int64_t h) {
    void *d = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    if (!d) return NULL;
    if (set_int(d, "originXPixels", x) || set_int(d, "originYPixels", y) ||
        set_int(d, "widthPixels", w) || set_int(d, "heightPixels", h)) {
        rel(d);
        return NULL;
    }
    return d;
}

/* view area = full visible frame + a safe rectangle for the important UI */
static void *view_area(int64_t sx, int64_t sy, int64_t sw, int64_t sh) {
    void *va = rect(0, 0, CL_W, CL_H);
    void *safe = rect(sx, sy, sw, sh);
    if (!va || !safe || set_val(va, "safeArea", safe)) { rel(va); va = NULL; }
    rel(safe);
    return va;
}

/* initialVideoStreams = [{originX/Y 0, uuid, zIndex 0}]; CarKit rejects a
 * display without it (luka-dev, iOS 26 CarKit). uuidObj is borrowed. */
static void *stream_list(void *uuidObj) {
    void *arr = ap.arr_new(NULL, 1, ap.arr_cb);
    void *d = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    void *res = NULL;
    if (arr && d && !set_int(d, "originXPixels", 0) && !set_int(d, "originYPixels", 0) &&
        !set_val(d, "uuid", uuidObj) && !set_int(d, "zIndex", 0)) {
        ap.arr_add(arr, d);
        res = arr;
        arr = NULL;
    }
    rel(arr);
    rel(d);
    return res;
}

static int add_common(void *d, int night) {
    void *empty = ap.arr_new(NULL, 0, ap.arr_cb);
    int rc = (!empty || set_int(d, "cornerMasks", 0) || set_int(d, "nightMode", night) ||
              set_int(d, "limitedUI", 0) || set_val(d, "limitedUIElements", empty)) ? -1 : 0;
    rel(empty);
    return rc;
}

/* Cluster geometry, overridable without a rebuild: /tmp/altscreen.conf, else
 * cluster/altscreen.conf, lines "key=a,b,..." (phys=W,H in mm;
 * large=x,y,w,h and small=x,y,w,h safe areas; zoom_invert=0/1, default 1;
 * zoom_restore=0/1, default 1, and zoom_max=N, default 10, see ZOOM_SAVE;
 * speedlimit=0/1, default 1: the phone is asked to draw its speed limit sign
 * on the map, showSpeedLimit in the Maps cluster URL).
 *
 * Safe rectangles from the calibration pattern (cluster capture=test,
 * photographed in both cockpit views, 0.725 photo px per stream px):
 * - large map view (small gauges): Audi's top info bar covers x 595..1320
 *   down to y 145, its bottom bar starts below y 575, the consumption box
 *   reaches x 455 above y 380 and the gauges start at x 475 / 1465.
 * - classic view (big gauges): the cockpit shows only the centre of this
 *   window, x 616..1304 and y 150..572, at the same scale.
 * Physical size: the panel is 292 x 110 mm (12.3", 0.152 mm/px), and with
 * that Google Maps showed its ETA only with the 540 px tall first guess, not
 * in 380..400 px. The default claims 0.2 mm/px (384 x 144 mm, close to
 * luka-dev's 0.203 on MHI2Q), so the same pixels count as more room for the
 * card layout; the UI gets 0.76x smaller. */
static struct {
    int phys_w, phys_h;
    int area[2][4];                     /* 0 = large map view, 1 = big gauges */
    int zoom_invert;
    int zoom_restore, zoom_max;
    int speedlimit;
} g_conf;

static void parse_ints(const char *v, int *out, int n) {
    int i;
    for (i = 0; i < n && *v; i++) {
        out[i] = atoi(v);
        while (*v && *v != ',') v++;
        if (*v == ',') v++;
    }
}

static void load_conf(void) {
    char line[160];
    FILE *f;
    g_conf.phys_w = 384; g_conf.phys_h = 144;
    g_conf.area[0][0] = 500; g_conf.area[0][1] = 155; g_conf.area[0][2] = 940; g_conf.area[0][3] = 410;
    g_conf.area[1][0] = 624; g_conf.area[1][1] = 158; g_conf.area[1][2] = 672; g_conf.area[1][3] = 400;
    g_conf.zoom_invert = 1;             /* A6 C8: positive roller steps zoom out */
    g_conf.zoom_restore = 1;
    g_conf.zoom_max = 10;
    g_conf.speedlimit = 1;              /* iOS parses it; no sign seen on the A6 test car yet */
    f = fopen(CONF_TMP, "r");
    if (!f) f = fopen(CONF_PATH, "r");
    if (f) {
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            if (!eq || line[0] == '#') continue;
            *eq = 0;
            if (!strcmp(line, "phys")) { int v[2] = { g_conf.phys_w, g_conf.phys_h }; parse_ints(eq + 1, v, 2); g_conf.phys_w = v[0]; g_conf.phys_h = v[1]; }
            else if (!strcmp(line, "large")) parse_ints(eq + 1, g_conf.area[0], 4);
            else if (!strcmp(line, "small")) parse_ints(eq + 1, g_conf.area[1], 4);
            else if (!strcmp(line, "zoom_invert")) g_conf.zoom_invert = atoi(eq + 1);
            else if (!strcmp(line, "zoom_restore")) g_conf.zoom_restore = atoi(eq + 1);
            else if (!strcmp(line, "zoom_max")) g_conf.zoom_max = atoi(eq + 1);
            else if (!strcmp(line, "speedlimit")) g_conf.speedlimit = atoi(eq + 1) ? 1 : 0;
        }
        fclose(f);
    }
    if (g_conf.zoom_max < 1) g_conf.zoom_max = 1;
    if (g_conf.zoom_max > 30) g_conf.zoom_max = 30;
    plog("conf: phys %dx%d mm, large %d,%d %dx%d, small %d,%d %dx%d, zoom_invert %d, "
         "zoom_restore %d, zoom_max %d, speedlimit %d%s",
         g_conf.phys_w, g_conf.phys_h, g_conf.area[0][0], g_conf.area[0][1], g_conf.area[0][2],
         g_conf.area[0][3], g_conf.area[1][0], g_conf.area[1][1], g_conf.area[1][2],
         g_conf.area[1][3], g_conf.zoom_invert, g_conf.zoom_restore, g_conf.zoom_max,
         g_conf.speedlimit, f ? "" : " (defaults)");
}

/* Cockpit view as the mod's IMapClusterServiceWrapper last reported it:
 * 0 = large map (small gauges), 1 = big gauges, -1 = not reported yet. */
static int current_view(void) {
    char v[16] = "";
    FILE *f = fopen(VIEW_PATH, "r");
    if (!f) return -1;
    if (!fgets(v, sizeof(v), f)) v[0] = 0;
    fclose(f);
    if (!strncmp(v, "large", 5)) return 0;
    if (!strncmp(v, "small", 5)) return 1;
    return -1;
}

static void *build_cluster(int night) {
    void *d = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    void *props = ap.arr_new(NULL, 1, ap.arr_cb);
    void *areas = ap.arr_new(NULL, 2, ap.arr_cb);
    void *adj = ap.arr_new(NULL, 0, ap.arr_cb);
    void *full, *center;
    int view;
    load_conf();
    view = current_view();
    if (view < 0) view = 1;             /* big-gauge area lies inside both views */
    full = view_area(g_conf.area[0][0], g_conf.area[0][1], g_conf.area[0][2], g_conf.area[0][3]);
    center = view_area(g_conf.area[1][0], g_conf.area[1][1], g_conf.area[1][2], g_conf.area[1][3]);
    void *sid = cstr(CL_STREAM);
    void *streams = sid ? stream_list(sid) : NULL;
    void *res = NULL;

    if (!d || !props || !areas || !adj || !full || !center || !streams) goto out;
    if (arr_add_str(props, "Instrument Cluster")) goto out;
    ap.arr_add(areas, full);
    ap.arr_add(areas, center);
    if (set_str(d, "name", "Instrument Cluster") || set_int(d, "type", ALT_TYPE) ||
        set_int(d, "maxFPS", 30) ||
        set_int(d, "widthPixels", CL_W) || set_int(d, "heightPixels", CL_H) ||
        set_int(d, "widthPhysical", g_conf.phys_w) || set_int(d, "heightPhysical", g_conf.phys_h) ||
        set_str(d, "uid", CL_UUID) || set_str(d, "uuid", CL_UUID) ||
        set_val(d, "properties", props) || set_val(d, "viewAreas", areas) ||
        set_int(d, "initialViewArea", view) || set_val(d, "adjacentViewAreas", adj) ||
        set_val(d, "initialVideoStreams", streams) || add_common(d, night))
        goto out;
    res = d;
    d = NULL;
out:
    rel(d); rel(props); rel(areas); rel(adj); rel(full); rel(center); rel(sid); rel(streams);
    if (!res) plog("cluster display dictionary incomplete");
    return res;
}

/* Copy of the stock main display with the keys newer CarKit expects: uid and
 * initialVideoStreams carrying the real main uuid, never a made-up one. */
static void *build_main(const void *stock, int night) {
    void *d = ap.dict_copy(NULL, 0, stock);
    void *uuid = d ? get_val(d, "uuid") : NULL;
    void *streams = uuid ? stream_list(uuid) : NULL;
    if (!d || !uuid || !streams || set_val(d, "uid", uuid) ||
        set_val(d, "initialVideoStreams", streams) || add_common(d, night)) {
        plog("main display copy failed (d=%p uuid=%p)", d, uuid);
        rel(d);
        d = NULL;
    }
    rel(streams);
    return d;
}

/* ----------------------------------------------------------- shared state */

static pthread_mutex_t g_mtx = PTHREAD_MUTEX_INITIALIZER;
static void *g_ctrl_session;            /* session whose control SETUP got altScreen */
static int g_session_gen;               /* counts those control SETUPs */
static int g_zoom_gen;                  /* session whose saved zoom was restored */
static int g_night_dio = -1;            /* last night mode dio_manager set, -1 = none yet */
static void *g_night_session;           /* the session it set it for */
static int g_night_advert = -1;         /* night mode the cluster display was advertised with */

typedef struct receiver {
    void *session;                      /* retained (CFRetain) or NULL */
    int listen_fd;
    volatile int stop;
    volatile int streaming;             /* first config or frame received */
    volatile int done;                  /* receive loop left, command thread ends */
    int paired;                         /* 1: ChaCha20-Poly1305, 0: AES-CTR */
    uint8_t key[32], iv[16];            /* AES uses key[0..15] + iv */
    uint64_t conn_id;
} receiver_t;

static receiver_t *g_rx;                /* current receiver, owned by its thread */

/* ------------------------------------------- libairplay idle keep-alive */

/*
 * While no stream libairplay knows is set up (main screen 110, audio), its
 * _IdleStateKeepAliveThread @0x321bc waits in select() with a 30 s timeout on
 * the UDP keep-alive socket *(int *)(session+0x15ec) (port at +0x15f0, both
 * filled by _IdleStateKeepAliveInitialize @0x385de) and its stop pipe
 * (+0x15f4). Every datagram re-arms the wait; a timeout tears the session down
 * ("Keep alive thread timeout (30 ms) expired!", kTimeoutErr; the log says ms,
 * the value is seconds). With stream 111 running the iPhone is not idle and
 * sends its keep-alives inside stream 111 instead (opcode 5,
 * KeepAliveWithBody, about one per second), which libairplay never sees. So
 * with the MMI on the radio and no CarPlay audio the session ended every
 * 30 s and the phone reconnected (A6 C8, 2026-11-10).
 *
 * select() below passes every call straight through except exactly that one
 * (readfds holding the keep-alive socket, no write/except sets, timeout 30.0
 * s): when it times out while stream 111 delivered data in the last 30 s, it
 * waits again until 30 s after that data. A phone that really goes away stops
 * sending on 111 too, and the session ends as before, at most 30 s later.
 */
#define OFF_KA_SOCK   0x15ec
#define OFF_KA_PORT   0x15f0
#ifndef KA_TIMEOUT
#define KA_TIMEOUT    30               /* s, as libairplay waits */
#endif

static volatile int g_ka_fd = -1;       /* keep-alive socket while 111 runs */
static volatile long g_alt_seen;        /* CLOCK_MONOTONIC s of the last 111 data */
static receiver_t *g_ka_owner;          /* under g_mtx */

static long mono_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec;
}

static void ka_attach(receiver_t *rx) {
    /* getsockopt lives in libsocket, which this library does not link:
     * looked up in dio_manager's global scope, skipped if absent */
    typedef int (*fn_getsockopt_t)(int, int, int, void *, socklen_t *);
    fn_getsockopt_t gso = (fn_getsockopt_t)dlsym(RTLD_DEFAULT, "getsockopt");
    const char *s = (const char *)rx->session;
    int fd = *(const int *)(s + OFF_KA_SOCK);
    int port = *(const int *)(s + OFF_KA_PORT);
    int type = -1;
    socklen_t len = sizeof(type);

    if (gso && fd > 2 && gso(fd, SOL_SOCKET, SO_TYPE, &type, &len) != 0)
        type = -2;
    if (fd <= 2 || port <= 0 || port > 65535 || fcntl(fd, F_GETFD) == -1 ||
        (gso && type != SOCK_DGRAM)) {
        plog("keep-alive: no idle socket at session+0x%x (fd %d port %d type %d), not held",
             OFF_KA_SOCK, fd, port, type);
        return;
    }
    pthread_mutex_lock(&g_mtx);
    g_alt_seen = mono_sec();
    g_ka_owner = rx;
    g_ka_fd = fd;
    pthread_mutex_unlock(&g_mtx);
    plog("keep-alive: libairplay idle socket fd %d port %d, held while stream 111 runs", fd, port);
}

static void ka_detach(receiver_t *rx) {
    pthread_mutex_lock(&g_mtx);
    if (g_ka_owner == rx) {
        g_ka_owner = NULL;
        g_ka_fd = -1;
    }
    pthread_mutex_unlock(&g_mtx);
}

/* ------------------------------------------------- ring to cluster daemon */

/* Layout and name of mh2p-cluster's src/cluster_h264_shm.h. The daemon's
 * h264 child (capture=h264) maps it read-only, reads from ring offset 0 when
 * it starts and feeds whatever arrives to the NvMedia parser. Writer order:
 * data, then write_pos, then total_bytes (the reader polls total_bytes). */
#define RING_NAME     "/cluster_h264_shm"
#define RING_MAGIC    0x48323634u
#define RING_SIZE     (2 * 1024 * 1024)

typedef struct {
    volatile uint32_t magic;
    volatile uint32_t write_pos;
    volatile uint32_t total_bytes;
    volatile uint32_t flags;            /* bit 0: stream active */
    uint8_t ring[RING_SIZE];
} ring_t;

static ring_t *g_ring;

static void ring_open(void) {
    void *p;
    int fd;
    if (g_ring) return;
    fd = shm_open(RING_NAME, O_RDWR | O_CREAT, 0666);
    if (fd < 0) { plog("ring: shm_open failed errno=%d", errno); return; }
    if (ftruncate(fd, sizeof(ring_t)) != 0) plog("ring: ftruncate errno=%d", errno);
    p = mmap(NULL, sizeof(ring_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) { plog("ring: mmap failed errno=%d", errno); return; }
    g_ring = (ring_t *)p;
    if (g_ring->magic != RING_MAGIC) {
        g_ring->write_pos = 0;
        g_ring->total_bytes = 0;
        g_ring->flags = 0;
        __sync_synchronize();
        g_ring->magic = RING_MAGIC;
    }
    plog("ring: %s mapped (write_pos=%u total=%u)", RING_NAME, g_ring->write_pos,
         g_ring->total_bytes);
}

static void ring_write(const uint8_t *p, uint32_t n) {
    uint32_t wp, first;
    if (!g_ring || !n) return;
    if (n > RING_SIZE / 2) { plog("ring: access unit of %u bytes dropped", n); return; }
    wp = g_ring->write_pos;
    first = RING_SIZE - wp;
    if (n <= first) {
        memcpy(g_ring->ring + wp, p, n);
    } else {
        memcpy(g_ring->ring + wp, p, first);
        memcpy(g_ring->ring, p + first, n - first);
    }
    __sync_synchronize();
    g_ring->write_pos = (wp + n) % RING_SIZE;
    __sync_synchronize();
    g_ring->total_bytes += n;
}

static void ring_active(int on) {
    if (g_ring) g_ring->flags = on ? 1u : 0u;
}

/* ---------------------------------------------------------------- commands */

#define CTL_PATH      "/tmp/cluster_ctl"
#define TICK_MS       50
#define TICKS(ms)     ((ms) / TICK_MS)

static void cmd_done(int status, void *resp, void *ctx) {
    (void)resp;
    plog("command %s answered: status=%d", ctx ? (const char *)ctx : "?", status);
}

/* {type, params {uuid = cluster, [url], [int key = value]}} */
static int send_cmd_int(void *session, const char *type, const char *url,
                        const char *ikey, int64_t ival, int quiet) {
    void *cmd = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    void *params = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    int rc = -1;
    if (cmd && params && !set_str(params, "uuid", CL_UUID) &&
        (!url || !set_str(params, "url", url)) &&
        (!ikey || !set_int(params, ikey, ival)) &&
        !set_str(cmd, "type", type) && !set_val(cmd, "params", params))
        rc = ap.send_cmd(session, cmd, quiet ? NULL : (void *)cmd_done, (void *)type);
    if (!quiet || rc) plog("command %s%s%s sent: rc=%d", type, url ? " " : "", url ? url : "", rc);
    rel(params);
    rel(cmd);
    return rc;
}

static int send_cmd(void *session, const char *type, const char *url) {
    return send_cmd_int(session, type, url, NULL, 0, 0);
}

/* Shows view area 0 (large map) or 1 (big gauges); the areas are declared in
 * /info, the iPhone moves its cards into the selected safe rectangle. */
static int send_view_area(void *session, int index) {
    void *cmd = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    void *params = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    void *adj = ap.arr_new(NULL, 0, ap.arr_cb);
    int rc = -1;
    if (cmd && params && adj && !set_str(params, "uuid", CL_UUID) &&
        !set_int(params, "viewAreaIndex", index) && !set_int(params, "animationDurationMillis", 0) &&
        !set_val(params, "adjacentViewAreas", adj) &&
        !set_str(cmd, "type", "updateViewArea") && !set_val(cmd, "params", params))
        rc = ap.send_cmd(session, cmd, (void *)cmd_done, (void *)"updateViewArea");
    plog("command updateViewArea %d (%s) sent: rc=%d", index, index ? "big gauges" : "large map", rc);
    rel(adj);
    rel(params);
    rel(cmd);
    return rc;
}

/* Zoom steps from the steering-wheel roller, appended by the mod's
 * IMapClusterServiceWrapper as "zoom <steps>" lines. The HMI owns the file
 * and /tmp is sticky, so this process can neither rename nor empty it: it
 * reads from where it stopped last time (*pos), from 0 again if the file got
 * shorter (recreated). pos < 0 skips what is already there. */
static int take_zoom_steps(long *pos) {
    char line[64];
    int sum = 0;
    struct stat st;
    FILE *f;
    if (stat(ZOOM_PATH, &st) != 0) return 0;
    if (*pos < 0 || (long)st.st_size < *pos) {
        if (*pos < 0) { *pos = (long)st.st_size; return 0; }
        *pos = 0;
    }
    if ((long)st.st_size == *pos) return 0;
    f = fopen(ZOOM_PATH, "r");
    if (!f) return 0;
    if (fseek(f, *pos, SEEK_SET) == 0) {
        while (fgets(line, sizeof(line), f)) {
            size_t n = strlen(line);
            if (n == 0 || line[n - 1] != '\n') break;    /* line still being written */
            *pos += (long)n;
            if (!strncmp(line, "zoom ", 5)) sum += atoi(line + 5);
        }
    }
    fclose(f);
    return sum;
}

/* The map zoom lives on the iPhone and is only ever moved in relative steps;
 * the phone never reports the level back, and every new session starts at the
 * app's default. So the hook counts the steps it sent (in roller direction,
 * before zoom_invert) and keeps the net count across restarts: next to the
 * session log, written 3 s after the last step. The count stops at +-zoom_max,
 * so turning the roller past the app's own limit does not drift it far. When
 * the first stream of the next CarPlay session runs, the same steps are sent
 * again (zoom_restore=0 off); a later stream of the same session continues
 * from the saved count without sending them, so they never add up twice.
 * The replay waits until the app's map is up and spaces the steps like a
 * roller turned by hand: in the car, a burst of steps 50 ms apart, 2 s after
 * video start, left the map more zoomed in than it was saved. */
#define ZOOM_SAVE     "/mnt/ota/modkit/altscreen_zoom.txt"
#define ZOOM_SAVE_TMP ZOOM_SAVE ".tmp"
#define ZOOM_RESTORE_DELAY_MS 6000      /* after the first frames */
#define ZOOM_RESTORE_GAP_MS   600       /* between two replayed steps */

static int zoom_load(void) {
    char line[32];
    int v = 0;
    FILE *f = fopen(ZOOM_SAVE, "r");
    if (!f) return 0;
    if (fgets(line, sizeof(line), f)) v = atoi(line);
    fclose(f);
    if (v > g_conf.zoom_max) v = g_conf.zoom_max;
    if (v < -g_conf.zoom_max) v = -g_conf.zoom_max;
    return v;
}

static void zoom_store(int v) {
    FILE *f = fopen(ZOOM_SAVE_TMP, "w");
    if (!f) { plog("zoom: %s not writable (errno %d)", ZOOM_SAVE_TMP, errno); return; }
    fprintf(f, "%d\n", v);
    if (fclose(f) != 0 || rename(ZOOM_SAVE_TMP, ZOOM_SAVE) != 0) {
        plog("zoom: saving %d failed (errno %d)", v, errno);
        return;
    }
    plog("zoom: net %d step(s) saved", v);
}

static int ctl_stamp(long *mtime, long *size) {
    struct stat st;
    if (stat(CTL_PATH, &st) != 0) return -1;
    *mtime = (long)st.st_mtime;
    *size = (long)st.st_size;
    return 0;
}

/* Runs next to the receiver, owns no state of its own. SendCommand goes
 * through the HTTP client and may block, so it never runs on the receive
 * path. Keyframes: the iPhone sends one only when asked, and the cluster
 * daemon starts a fresh decoder on every Java 'start'. Any change of
 * /tmp/cluster_ctl (Java writes a command, the daemon empties the file after
 * acting on it, polling every 2 s) asks for keyframes 2.5 s and 4.5 s later,
 * when the new decoder is up. A slow periodic keyframe covers the rest. */
static void *cmd_main(void *arg) {
    receiver_t *rx = (receiver_t *)arg;
    long mt = 0, sz = 0, nmt, nsz;
    int have_ctl, tick = 0, kf1 = -1, kf2 = -1, last_kf = 0, ui_sent = 0;
    int view = -1, zoom = 0, steps;
    int net = 0, saved_net = 0, restore = 0, restore_at = -1, zoom_tick = -1, lim;
    long zoom_pos = -1;

    have_ctl = (ctl_stamp(&mt, &sz) == 0);
    plog("command thread: %s %s", CTL_PATH, have_ctl ? "found" : "missing (cluster daemon not running?)");
    take_zoom_steps(&zoom_pos);         /* steps from before this stream are stale */
    while (!rx->stop && !rx->done) {
        usleep(TICK_MS * 1000);
        tick++;
        if (!rx->streaming) continue;
        if (!ui_sent) {
            send_cmd(rx->session, "showUI", g_conf.speedlimit ? CL_URL "yes" : CL_URL "no");
            usleep(200 * 1000);
            send_cmd(rx->session, "forceKeyFrame", NULL);
            ui_sent = 1;
            last_kf = tick;
            {
                int night = -1;
                pthread_mutex_lock(&g_mtx);
                if (g_night_session == rx->session && g_night_dio >= 0 &&
                    g_night_dio != g_night_advert)
                    night = g_night_advert = g_night_dio;
                pthread_mutex_unlock(&g_mtx);
                if (night >= 0) {
                    plog("night mode %d set since /info, sent for the cluster display", night);
                    send_cmd_int(rx->session, "setNightMode", NULL, "nightMode", night, 0);
                }
            }
            if (g_conf.zoom_restore) {
                int first;
                pthread_mutex_lock(&g_mtx);
                first = (g_zoom_gen != g_session_gen);
                g_zoom_gen = g_session_gen;
                pthread_mutex_unlock(&g_mtx);
                saved_net = zoom_load();
                if (!first) {
                    net = saved_net;    /* another 111 stream of the same CarPlay session */
                    plog("zoom: same session, %d saved step(s) not sent again", saved_net);
                } else if (saved_net) {
                    restore = saved_net;
                    restore_at = tick + TICKS(ZOOM_RESTORE_DELAY_MS);   /* once the scene is up */
                }
            }
            continue;
        }
        lim = g_conf.zoom_max > 10 ? g_conf.zoom_max : 10;
        if (restore_at >= 0 && tick >= restore_at) {
            int s = restore > 0 ? 1 : -1;
            if (restore == saved_net) plog("zoom: restoring %d saved step(s)", restore);
            zoom += s;
            if (zoom > lim) zoom = lim;
            if (zoom < -lim) zoom = -lim;
            restore -= s;
            restore_at = restore ? tick + TICKS(ZOOM_RESTORE_GAP_MS) : -1;
            if (!restore) slog("zoom: %d saved step(s) restored", saved_net);
        }
        /* cockpit view -> view area */
        if (tick % TICKS(250) == 0) {
            int v = current_view();
            if (v >= 0 && v != view) {
                view = v;
                send_view_area(rx->session, view);
            }
        }
        /* steering-wheel zoom: one changeMapZoomLevel per step, 50 ms apart
         * (luka-dev: zoomDirection 1 for positive steps, 0 for negative) */
        steps = take_zoom_steps(&zoom_pos);
        if (steps) {
            plog("zoom: %d step(s) from the wheel", steps);
            zoom += steps;
            if (zoom > lim) zoom = lim;
            if (zoom < -lim) zoom = -lim;
        }
        if (zoom) {
            int dir = zoom > 0 ? 1 : 0;
            int s = zoom > 0 ? 1 : -1;
            if (g_conf.zoom_invert) dir = !dir;
            if (send_cmd_int(rx->session, "changeMapZoomLevel", NULL, "zoomDirection", dir, 1) == 0 &&
                net + s <= g_conf.zoom_max && net + s >= -g_conf.zoom_max)
                net += s;
            zoom -= s;
            zoom_tick = tick;
            continue;
        }
        if (g_conf.zoom_restore && restore_at < 0 && zoom_tick >= 0 && net != saved_net &&
            tick - zoom_tick >= TICKS(3000)) {
            zoom_store(net);
            saved_net = net;
        }
        if (tick % TICKS(250) != 0) continue;
        if (ctl_stamp(&nmt, &nsz) == 0) {
            if (!have_ctl || nmt != mt || nsz != sz) {
                plog("daemon command file changed (size %ld), keyframes follow", nsz);
                kf1 = tick + TICKS(2500);
                kf2 = tick + TICKS(4500);
            }
            have_ctl = 1;
            mt = nmt;
            sz = nsz;
        }
        if ((kf1 >= 0 && tick >= kf1) || (kf2 >= 0 && tick >= kf2) || tick - last_kf >= TICKS(10000)) {
            if (kf1 >= 0 && tick >= kf1) kf1 = -1;
            else if (kf2 >= 0 && tick >= kf2) kf2 = -1;
            send_cmd(rx->session, "forceKeyFrame", NULL);
            last_kf = tick;
        }
    }
    if (g_conf.zoom_restore && restore_at < 0 && zoom_tick >= 0 && net != saved_net)
        zoom_store(net);
    plog("command thread finished");
    return NULL;
}

/* ---------------------------------------------------------------- receiver */

static int read_exact(int fd, uint8_t *buf, uint32_t n, volatile int *stop) {
    uint32_t got = 0;
    while (got < n) {
        struct pollfd p;
        ssize_t r;
        int pr;
        if (*stop) return -1;
        p.fd = fd; p.events = POLLIN; p.revents = 0;
        pr = poll(&p, 1, 100);
        if (pr == 0) continue;
        if (pr < 0) { if (errno == EINTR) continue; return -1; }
        r = read(fd, buf + got, n - got);
        if (r > 0) { got += (uint32_t)r; continue; }
        if (r < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        return -1;
    }
    return 0;
}

static const uint8_t START[4] = { 0, 0, 0, 1 };

/* Output: the daemon ring always, a file in /tmp only with REC_FLAG. */
typedef struct {
    FILE *f;
    long written;
    int full;
    uint8_t *buf;                       /* one Annex-B access unit */
    uint32_t len, cap;
    uint8_t cfg[1024];                  /* SPS/PPS of the last config */
    uint32_t cfg_len;
} vout_t;

static int out_reserve(vout_t *o, uint32_t more) {
    uint8_t *nb;
    uint32_t want = o->len + more;
    if (want <= o->cap) return 0;
    want = want * 2 + 1024;
    nb = (uint8_t *)realloc(o->buf, want);
    if (!nb) return -1;
    o->buf = nb;
    o->cap = want;
    return 0;
}

static int out_add(vout_t *o, const uint8_t *p, uint32_t n) {
    if (out_reserve(o, n) != 0) return -1;
    memcpy(o->buf + o->len, p, n);
    o->len += n;
    return 0;
}

static void out_flush(vout_t *o) {
    ring_write(o->buf, o->len);
    if (o->f && !o->full) {
        if (o->written + (long)o->len > VIDEO_LIMIT) {
            o->full = 1;
            plog("video file reached %d MB, recording stopped", VIDEO_LIMIT >> 20);
        } else {
            fwrite(o->buf, 1, o->len, o->f);
            o->written += (long)o->len;
        }
    }
    o->len = 0;
}

/* avcC -> SPS/PPS with start codes into o->cfg. Returns NAL length size,
 * 0 if this is not an avcC record. */
static int parse_avcc_config(vout_t *o, const uint8_t *c, uint32_t n) {
    uint32_t pos = 6, i, k, cnt, len, w = 0;
    int ls;
    if (n < 7 || c[0] != 1) return 0;
    ls = (c[4] & 3) + 1;
    cnt = c[5] & 0x1f;
    for (k = 0; k < 2; k++) {
        for (i = 0; i < cnt; i++) {
            if (pos + 2 > n) return 0;
            len = ((uint32_t)c[pos] << 8) | c[pos + 1];
            pos += 2;
            if (pos + len > n || w + 4 + len > sizeof(o->cfg)) return 0;
            memcpy(o->cfg + w, START, 4);
            memcpy(o->cfg + w + 4, c + pos, len);
            w += 4 + len;
            pos += len;
        }
        if (k == 0) {
            if (pos + 1 > n) return 0;
            cnt = c[pos++];
        }
    }
    o->cfg_len = w;
    return ls;
}

/* Checks that the decrypted body is a clean chain of length-prefixed NAL
 * units (the proof that key and nonce are right). Returns the bit mask of
 * NAL types seen, -1 if the chain does not add up. */
static int scan_access_unit(const uint8_t *b, uint32_t n, int ls) {
    uint32_t pos = 0, len;
    int i, types = 0;
    while (pos < n) {
        if (pos + (uint32_t)ls > n) return -1;
        for (len = 0, i = 0; i < ls; i++) len = (len << 8) | b[pos + i];
        pos += (uint32_t)ls;
        if (len == 0 || pos + len > n) return -1;
        types |= 1 << (b[pos] & 0x1f);
        pos += len;
    }
    return types;
}

/* Annex-B access unit into o->buf; SPS/PPS first when it holds an IDR, so a
 * decoder that starts here has everything it needs. */
static int build_access_unit(vout_t *o, const uint8_t *b, uint32_t n, int ls, int idr) {
    uint32_t pos = 0, len;
    int i;
    o->len = 0;
    if (idr && o->cfg_len && out_add(o, o->cfg, o->cfg_len) != 0) return -1;
    while (pos < n) {
        for (len = 0, i = 0; i < ls; i++) len = (len << 8) | b[pos + i];
        pos += (uint32_t)ls;
        if (out_add(o, START, 4) != 0 || out_add(o, b + pos, len) != 0) return -1;
        pos += len;
    }
    return 0;
}

static void hexline(char *dst, size_t cap, const uint8_t *p, uint32_t n) {
    uint32_t i;
    dst[0] = 0;
    for (i = 0; i < n && (i + 1) * 2 < cap; i++) snprintf(dst + i * 2, cap - i * 2, "%02x", p[i]);
}

static void receive(receiver_t *rx, int fd) {
    uint8_t ctx[512] __attribute__((aligned(16)));    /* AES_CTR context is 280 bytes */
    uint8_t hdr[128];
    uint8_t *body = NULL, *clear = NULL;
    uint32_t cap = 0;
    unsigned long frames = 0, bad = 0, idr = 0, other = 0, authfail = 0;
    uint64_t nonce = 0;
    int ls = 4, video_on = 0;
    char hex[160];
    vout_t out;
    struct timespec t0, t1;

    memset(&out, 0, sizeof(out));
    ring_open();
    ring_active(1);
    if (access(REC_FLAG, F_OK) == 0) {
        out.f = fopen(VIDEO_PATH, "wb");
        plog("recording to %s %s", VIDEO_PATH, out.f ? "on" : "failed");
    }
    if (!rx->paired && ap.aes_init(ctx, rx->key, rx->iv) != 0) {
        plog("AES_CTR_Init failed");
        goto done;
    }
    plog("decrypting with %s", rx->paired ? "ChaCha20-Poly1305 (pairing key)" : "AES-CTR");
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (;;) {
        uint32_t size;
        uint8_t op;
        if (read_exact(fd, hdr, sizeof(hdr), &rx->stop) != 0) {
            plog("stream ended (stop=%d errno=%d)", rx->stop, errno);
            break;
        }
        g_alt_seen = mono_sec();              /* the phone is alive (keep-alive) */
        size = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) |
               ((uint32_t)hdr[3] << 24);
        op = hdr[4];
        if (size > 4u * 1024 * 1024) {
            plog("implausible body size %u (opcode %u), giving up", size, op);
            break;
        }
        if (size > cap) {
            uint8_t *nb = (uint8_t *)realloc(body, size);
            if (!nb) break;
            body = nb;
            nb = (uint8_t *)realloc(clear, size);
            if (!nb) break;
            clear = nb;
            cap = size;
        }
        if (size && read_exact(fd, body, size, &rx->stop) != 0) {
            plog("stream ended inside a body (opcode %u size %u)", op, size);
            break;
        }

        if (op == 1) {                        /* codec config, not encrypted */
            float w = 0, h = 0;
            int l;
            memcpy(&w, hdr + 16, 4);
            memcpy(&h, hdr + 20, 4);
            hexline(hex, sizeof(hex), body, size < 64 ? size : 64);
            if (!size) {
                plog("config without body (screen state, flags 0x%02x)", hdr[6]);
                continue;
            }
            l = parse_avcc_config(&out, body, size);
            if (l) {
                ls = l;
                out.len = 0;
                if (out_add(&out, out.cfg, out.cfg_len) == 0) out_flush(&out);
            }
            plog("CONFIG %dx%d size=%u avcC=%s lengthSize=%d data=%s%s",
                 (int)(w + 0.5f), (int)(h + 0.5f), size, l ? "yes" : "NO", ls, hex,
                 size > 64 ? "..." : "");
            rx->streaming = 1;
        } else if (op == 0) {                 /* encrypted access unit */
            int types = 0, opened = 1;
            uint32_t plain = size;
            uint8_t *au = body;
            frames++;
            if (rx->paired) {
                uint8_t nb[8];
                int i, rc = -1;
                for (i = 0; i < 8; i++) nb[i] = (uint8_t)(nonce >> (8 * i));
                nonce++;
                if (size > 16) {
                    plain = size - 16;
                    rc = ap.chacha_open(rx->key, nb, hdr, sizeof(hdr), body, plain,
                                        clear, body + plain);
                    au = clear;
                }
                if (rc != 0) {
                    opened = 0;
                    authfail++;
                    if (authfail <= 5) plog("frame %lu size=%u authentication failed rc=%d", frames, size, rc);
                }
            } else {
                ap.aes_update(ctx, body, size, body);
            }
            if (opened) types = scan_access_unit(au, plain, ls);
            if (!opened) {
                /* counted above */
            } else if (types < 0) {
                bad++;
                if (bad <= 5) {
                    hexline(hex, sizeof(hex), au, plain < 24 ? plain : 24);
                    plog("frame %lu size=%u does NOT parse as AVCC: %s", frames, size, hex);
                }
            } else {
                int is_idr = (types & (1 << 5)) != 0;
                if (is_idr) idr++;
                if (build_access_unit(&out, au, plain, ls, is_idr) == 0) {
                    out_flush(&out);
                    if (!video_on) {
                        video_on = 1;
                        video_flag(1);
                        slog("111: video running");
                    }
                }
                if (frames <= 5 || (is_idr && idr <= 20))
                    plog("frame %lu size=%u NAL types mask=0x%x%s", frames, size, types,
                         is_idr ? " (IDR)" : "");
            }
            rx->streaming = 1;
            if (frames % 300 == 0) {
                clock_gettime(CLOCK_MONOTONIC, &t1);
                plog("%lu frames, %lu IDR, %lu bad, %lu auth failed, ring total %u, %.1f fps",
                     frames, idr, bad, authfail, g_ring ? g_ring->total_bytes : 0,
                     300.0 / ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9));
                t0 = t1;
                if (out.f) fflush(out.f);
            }
        } else {
            other++;
            if (other <= 5) plog("opcode %u size=%u ignored", op, size);
        }
    }
    if (!rx->paired) ap.aes_final(ctx);
done:
    ring_active(0);
    video_flag(0);
    plog("receiver summary: %lu frames, %lu IDR, %lu bad, %lu auth failed, %lu other",
         frames, idr, bad, authfail, other);
    slog("111: stream ended, %lu frames, %lu bad, %lu auth failed", frames, bad, authfail);
    if (out.f) fclose(out.f);
    free(out.buf);
    free(body);
    free(clear);
}

static void *rx_main(void *arg) {
    receiver_t *rx = (receiver_t *)arg;
    int fd = -1, waited = 0, fl, cmd_ok = 0;
    pthread_t cmd;

    while (!rx->stop && waited < 120) {
        if (ap.sock_accept(rx->listen_fd, 1, &fd, NULL) == 0 && fd >= 0) break;
        fd = -1;
        waited++;
    }
    close(rx->listen_fd);
    if (fd < 0) {
        plog("iPhone did not connect to the 111 port (stop=%d, %d s)", rx->stop, waited);
        slog("111: iPhone did NOT connect within %d s (stop=%d) - firewall rule missing?", waited, rx->stop);
    } else {
        plog("iPhone connected to the 111 stream");
        slog("111: iPhone connected after %d s", waited);
        fl = fcntl(fd, F_GETFL, 0);
        if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        if (rx->session) cmd_ok = (pthread_create(&cmd, NULL, cmd_main, rx) == 0);
        if (!cmd_ok) plog("no command thread (session %p): no showUI, no keyframes", rx->session);
        if (rx->session) ka_attach(rx);
        receive(rx, fd);
        ka_detach(rx);
        close(fd);
        rx->done = 1;
        if (cmd_ok) pthread_join(cmd, NULL);
    }
    pthread_mutex_lock(&g_mtx);
    if (g_rx == rx) g_rx = NULL;
    pthread_mutex_unlock(&g_mtx);
    rel(rx->session);
    memset(rx->key, 0, sizeof(rx->key));
    free(rx);
    plog("receiver thread finished");
    return NULL;
}

static void stop_receiver(void *session, const char *why) {
    pthread_mutex_lock(&g_mtx);
    if (g_rx && (!session || g_rx->session == session || !g_rx->session)) {
        g_rx->stop = 1;
        g_rx = NULL;              /* the thread frees itself */
        plog("receiver stop requested (%s)", why);
    }
    pthread_mutex_unlock(&g_mtx);
}

/* Media SETUP offered type 111: open the port, start the receiver and add
 * {type 111, dataPort} to the response. Returns 0 on success. */
static int alt_setup(void *session, void *desc, void *resp) {
    receiver_t *rx;
    pthread_t t;
    pthread_attr_t at;
    void *rdesc, *streams, *pair;
    int err = 0, port = 0, sock = -1, family;
    int64_t conn = get_int(desc, "streamConnectionID", &err);

    if (err || !conn) {
        plog("111: no streamConnectionID (err %d)", err);
        return -1;
    }
    stop_receiver(session, "new 111 SETUP");
    rx = (receiver_t *)calloc(1, sizeof(*rx));
    if (!rx) return -1;
    rx->conn_id = (uint64_t)conn;
    pair = *(void **)((char *)session + OFF_PAIRING);
    if (pair) {
        static const char info[] = "DataStream-Output-Encryption-Key";
        char salt[48];
        int n = snprintf(salt, sizeof(salt), "DataStream-Salt%llu", (unsigned long long)conn);
        err = ap.pair_derive(pair, salt, (size_t)n, info, sizeof(info) - 1, 32, rx->key);
        plog("111: pairing session %p, ChaCha key derived err=%d", pair, err);
        if (err) { free(rx); return -1; }
        rx->paired = 1;
    } else {
        ap.derive((const uint8_t *)session + OFF_AESKEY, 16, (uint64_t)conn, rx->key, rx->iv);
        plog("111: no pairing session, AES key derived");
    }
    family = *((const uint8_t *)session + OFF_FAMILY);
    err = ap.sock_open(family, 1, 6, ALT_PORT, &port, RCVBUF, &sock);
    plog("111: connID=%lld family=%d ServerSocketOpen err=%d port=%d sock=%d",
         (long long)conn, family, err, port, sock);
    if (err || sock < 0) { free(rx); return -1; }
    ap.sock_qos(sock, 12);
    rx->listen_fd = sock;
    rx->session = ap.retain(session);

    rdesc = ap.dict_new(NULL, 0, ap.key_cb, ap.val_cb);
    if (!rdesc || set_int(rdesc, "type", ALT_TYPE) || set_int(rdesc, "dataPort", port)) {
        rel(rdesc);
        goto fail;
    }
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_mutex_lock(&g_mtx);
    if (pthread_create(&t, &at, rx_main, rx) != 0) {
        pthread_mutex_unlock(&g_mtx);
        pthread_attr_destroy(&at);
        rel(rdesc);
        goto fail;
    }
    g_rx = rx;
    pthread_mutex_unlock(&g_mtx);
    pthread_attr_destroy(&at);

    streams = get_val(resp, "streams");
    if (is_type(streams, ap.arr_type)) {
        ap.arr_add(streams, rdesc);
    } else {
        streams = ap.arr_new(NULL, 1, ap.arr_cb);
        if (streams) { ap.arr_add(streams, rdesc); set_val(resp, "streams", streams); }
        rel(streams);
    }
    rel(rdesc);
    plog("111: receiver listening on port %d", port);
    slog("111: SETUP, listening on port %d", port);
    return 0;
fail:
    rel(rx->session);
    close(sock);
    free(rx);
    plog("111: setup failed");
    return -1;
}

static void *find_stream(const void *req, int64_t type) {
    void *streams = get_val(req, "streams");
    long i, n;
    int err;
    if (!is_type(streams, ap.arr_type)) return NULL;
    n = ap.arr_count(streams);
    for (i = 0; i < n; i++) {
        void *s = ap.arr_at(streams, i);
        if (is_type(s, ap.dict_type) && get_int(s, "type", &err) == type && !err) return s;
    }
    return NULL;
}

/* ------------------------------------------------------------------ hooks */

__attribute__((constructor)) static void altscreen_init(void) {
    video_flag(0);                      /* left over if dio_manager died mid-stream */
    plog("=== altscreen loaded into pid %d (off with %s or %s) ===", (int)getpid(), OFF_FLAG, OFF_FLAG_TMP);
}

typedef void *(*fn_copyinfo_t)(void *, void *, void *, void *);

void *AirPlayCopyServerInfo(void *a0, void *a1, void *a2, void *a3) {
    static fn_copyinfo_t real;
    void *info, *displays, *mainD, *clusterD, *list, *feat;
    int err, night;
    int enabled;

    if (!real) real = (fn_copyinfo_t)dlsym(RTLD_NEXT, "AirPlayCopyServerInfo");
    if (!real) { plog("AirPlayCopyServerInfo: real not found"); return NULL; }
    info = real(a0, a1, a2, a3);

    pthread_mutex_lock(&g_mtx);
    enabled = (a0 && a0 == g_ctrl_session);
    pthread_mutex_unlock(&g_mtx);
    if (!info || !enabled || !ap_ready()) return info;

    /* The stock value is dio_manager's start-up night mode and is never
     * updated; once dio_manager has set the mode for this session, that wins. */
    night = get_int(info, "nightMode", &err) ? 1 : 0;
    pthread_mutex_lock(&g_mtx);
    if (g_night_session == a0 && g_night_dio >= 0 && g_night_dio != night) {
        night = g_night_dio;
        set_int(info, "nightMode", night);
    }
    g_night_advert = night;
    pthread_mutex_unlock(&g_mtx);
    displays = get_val(info, "displays");
    if (!is_type(displays, ap.arr_type) || ap.arr_count(displays) < 1 ||
        !is_type(ap.arr_at(displays, 0), ap.dict_type)) {
        plog("/info: no stock main display, advert skipped");
        return info;
    }
    mainD = build_main(ap.arr_at(displays, 0), night);
    clusterD = build_cluster(night);
    list = ap.arr_new(NULL, 2, ap.arr_cb);
    feat = features_plus_alt(get_val(info, "extendedFeatures"));
    if (mainD && clusterD && list && feat) {
        ap.arr_add(list, mainD);
        ap.arr_add(list, clusterD);
        set_val(info, "extendedFeatures", feat);
        set_val(info, "displays", list);
        set_val(info, "displayPanels", list);
        plog("/info: altScreen advert added (session %p, night %d)", a0, night);
        plog_obj("  info.displays", list);
        plog_obj("  info.extendedFeatures", feat);
    } else {
        plog("/info: advert NOT added (main=%p cluster=%p list=%p feat=%p)",
             mainD, clusterD, list, feat);
    }
    rel(mainD); rel(clusterD); rel(list); rel(feat);
    return info;
}

typedef int (*fn_setup_t)(void *, void *, void **, void *);

int AirPlayReceiverSessionSetup(void *session, void *req, void **outResp, void *a3) {
    static fn_setup_t real;
    void *resp, *desc;
    int ret;

    if (!real) real = (fn_setup_t)dlsym(RTLD_NEXT, "AirPlayReceiverSessionSetup");
    if (!real) { plog("AirPlayReceiverSessionSetup: real not found"); return -6705; }
    ret = real(session, req, outResp, a3);
    resp = outResp ? *outResp : NULL;
    if (!is_on()) return ret;

    plog("SETUP session=%p ret=%d", session, ret);
    plog_obj("  request", req);
    if (ret == 0 && resp && session && req && ap_ready()) {
        if (get_val(req, "features")) {               /* control SETUP */
            void *enabled = features_plus_alt(get_val(resp, "enabledFeatures"));
            if (enabled && !set_val(resp, "enabledFeatures", enabled)) {
                pthread_mutex_lock(&g_mtx);
                g_ctrl_session = session;
                g_session_gen++;
                pthread_mutex_unlock(&g_mtx);
                plog("control SETUP: enabledFeatures altScreen + viewAreas");
                slog("CarPlay session start (altScreen offered)");
            } else {
                plog("control SETUP: enabledFeatures could not be set");
            }
            rel(enabled);
        }
        desc = find_stream(req, ALT_TYPE);
        if (desc) {
            plog("media SETUP offers type 111");
            alt_setup(session, desc, resp);   /* on failure the stock answer stays as is */
        }
    }
    plog_obj("  response", resp);
    return ret;
}

typedef int (*fn_select_t)(int, fd_set *, fd_set *, fd_set *, struct timeval *);

/* See "libairplay idle keep-alive" above. Every other call passes through. */
int select(int nfds, fd_set *rd, fd_set *wr, fd_set *ex, struct timeval *tv) {
    static fn_select_t real;
    unsigned char save[128];
    size_t n;
    struct timeval t;
    long left;
    int fd = g_ka_fd, r, held = 0;

    if (!real) real = (fn_select_t)dlsym(RTLD_NEXT, "select");
    if (!real) { errno = ENOSYS; return -1; }
    if (fd < 0 || !tv || tv->tv_sec != KA_TIMEOUT || tv->tv_usec != 0 || !rd || wr || ex ||
        fd >= nfds || !FD_ISSET(fd, rd))
        return real(nfds, rd, wr, ex, tv);
    n = ((size_t)nfds + 31) / 32 * 4;     /* whole 32-bit words, as select uses */
    if (n > sizeof(save)) return real(nfds, rd, wr, ex, tv);
    memcpy(save, rd, n);
    t = *tv;
    for (;;) {
        r = real(nfds, rd, NULL, NULL, &t);
        if (r != 0) return r;
        left = g_alt_seen + KA_TIMEOUT - mono_sec();
        if (g_ka_fd != fd || left <= 0) {
            if (held) plog("keep-alive: stream 111 silent for %d s too, libairplay's timeout goes ahead", KA_TIMEOUT);
            return 0;
        }
        if (!held++) {
            plog("keep-alive: libairplay idle timeout held off, stream 111 alive (next check in %ld s)", left);
            slog("111: idle keep-alive held by stream 111");
        }
        memcpy(rd, save, n);
        t.tv_sec = left;
        t.tv_usec = 0;
    }
}

/* Day / night. dio_manager calls this (session, night, NULL, NULL) when the
 * car switches; libairplay sends setNightMode {nightMode} without a display
 * uuid. The iPhone applies that to the main screen and to further displays
 * that do not advertise a nightMode of their own. The cluster display does
 * (add_common), so it kept the mode from /info until the next session: main
 * screen in day mode, cockpit map still dark (reported on a Q8, and by
 * luka-dev's MHI2Q hook, which repeats the command with the uuid as well).
 * The same command goes out again for the cluster display; the stock result
 * is returned whatever happens to that one. */
typedef int (*fn_night_t)(void *, int, void *, void *);

int AirPlayReceiverSessionSetNightMode(void *session, int night, void *done, void *ctx) {
    static fn_night_t real;
    int rc, mine, crc = -1;
    if (!real) real = (fn_night_t)dlsym(RTLD_NEXT, "AirPlayReceiverSessionSetNightMode");
    if (!real) { plog("AirPlayReceiverSessionSetNightMode: real not found"); return -6705; }
    rc = real(session, night, done, ctx);
    night = night ? 1 : 0;
    pthread_mutex_lock(&g_mtx);
    g_night_dio = night;
    g_night_session = session;
    mine = (session && session == g_ctrl_session);
    pthread_mutex_unlock(&g_mtx);
    if (mine && ap_ready()) {
        crc = send_cmd_int(session, "setNightMode", NULL, "nightMode", night, 0);
        if (crc == 0) {
            pthread_mutex_lock(&g_mtx);
            g_night_advert = night;
            pthread_mutex_unlock(&g_mtx);
            slog("night mode %d sent for the cluster display", night);
        }
    }
    plog("night mode %d from the head unit (session %p): main rc=%d, cluster display %s rc=%d",
         night, session, rc, mine ? "sent" : "not sent (no altScreen session)", crc);
    return rc;
}

typedef int (*fn_teardown_t)(void *, void *, int, void *);

int AirPlayReceiverSessionTearDown(void *session, void *req, int reason, void *a3) {
    static fn_teardown_t real;
    if (!real) real = (fn_teardown_t)dlsym(RTLD_NEXT, "AirPlayReceiverSessionTearDown");
    if (g_ap_ok) {
        void *streams = req ? get_val(req, "streams") : NULL;
        int full = !is_type(streams, ap.arr_type) || ap.arr_count(streams) == 0;
        if (full || find_stream(req, ALT_TYPE)) {
            plog("TEARDOWN session=%p reason=%d %s", session, reason, full ? "full" : "stream 111");
            stop_receiver(session, full ? "full teardown" : "111 teardown");
        }
        if (full) {
            pthread_mutex_lock(&g_mtx);
            if (g_ctrl_session == session) g_ctrl_session = NULL;
            pthread_mutex_unlock(&g_mtx);
        }
    }
    return real ? real(session, req, reason, a3) : reason;
}

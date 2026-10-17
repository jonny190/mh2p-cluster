/* Local P2873 lifecycle adaptation. CC BY-NC-SA 4.0; see LICENSE.md.
 * Independent of QNX/Screen so the SAME admission code is host-testable.
 * No readiness claim: RUNNING means the process has been started/resumed.
 */
#ifndef CLUSTER_LIFECYCLE_H
#define CLUSTER_LIFECYCLE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define LC_REQUEST "/tmp/mib2p_cluster_request"
#define LC_STATUS "/tmp/mib2p_cluster_status"
#define LC_LEASE_MS 5000ULL
#define LC_LINE_SIZE 512
typedef struct {
    char daemon[33], owner[33], action[9], args[256];
    uint64_t generation, sequence;
} lc_request;
typedef struct {
    char daemon[33], owner[33], state[12];
    uint64_t generation, sequence, seen_ms;
    int retired, busy;
} lc_owner;

static int lc_hex(const char *s) {
    size_t i;
    if (strlen(s) != 32) return 0;
    for (i = 0; i < 32; ++i)
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return 0;
    return 1;
}
static int lc_number(const char *s, uint64_t *out) {
    uint64_t n = 0;
    if (!*s) return 0;
    while (*s) {
        unsigned d = (unsigned)(*s++ - '0');
        if (d > 9 || n > (0x7fffffffffffffffULL - d) / 10) return 0;
        n = n * 10 + d;
    }
    if (!n) return 0;
    *out = n; return 1;
}
static int lc_parse(const char *line, lc_request *r) {
    char copy[LC_LINE_SIZE], *p, *words[7];
    size_t len = strlen(line);
    int i;
    if (!len || len >= sizeof(copy) || line[len-1] != '\n') return 0;
    memcpy(copy, line, len + 1); copy[len-1] = 0;
    for (i = 0; copy[i]; ++i)
        if ((unsigned char)copy[i] < 32 || (unsigned char)copy[i] > 126) return 0;
    p = copy;
    for (i = 0; i < 7; ++i) {
        words[i] = p;
        while (*p && *p != ' ') ++p;
        if (i < 6) { if (*p != ' ') return 0; *p++ = 0; }
    }
    if (*p == ' ') *p++ = 0;
    if (strcmp(words[0], "M2LC") || strcmp(words[1], "1") ||
        !lc_hex(words[2]) || !lc_hex(words[3]) ||
        !lc_number(words[4], &r->generation) || !lc_number(words[5], &r->sequence)) return 0;
    if (strcmp(words[6], "STOP") && strcmp(words[6], "PREPARE") && strcmp(words[6], "RENDER")) return 0;
    if (strlen(p) >= sizeof(r->args)) return 0;
    /* Args only belong to PREPARE; no quoting/shell evaluation. */
    if (strcmp(words[6], "PREPARE") && *p) return 0;
    strcpy(r->daemon, words[2]); strcpy(r->owner, words[3]);
    strcpy(r->action, words[6]); strcpy(r->args, p);
    return 1;
}
static void lc_init(lc_owner *s, const char *daemon) {
    memset(s, 0, sizeof(*s)); strcpy(s->daemon, daemon);
    strcpy(s->owner, "00000000000000000000000000000000");
    strcpy(s->state, "STOPPED"); s->retired = 1;
}
/* 1 = new accepted request; 0 = stale/invalid/replayed, NO lease renewal. */
static int lc_accept(lc_owner *s, const lc_request *r, uint64_t now) {
    if (strcmp(s->daemon, r->daemon)) return 0;
    if (strcmp(s->owner, r->owner)) {
        if (s->busy || strcmp(r->action, "STOP")) return 0;
        strcpy(s->owner, r->owner); s->generation = 0; s->sequence = 0;
    }
    if (r->sequence <= s->sequence || r->generation < s->generation) return 0;
    if (!strcmp(r->action, "STOP")) {
        s->retired = 1;
    } else {
        if (r->generation == s->generation && s->retired) return 0;
        if (r->generation > s->generation) {
            if (s->busy || strcmp(r->action, "PREPARE")) return 0;
            s->retired = 0;
        }
        if (!strcmp(r->action, "RENDER") && !s->busy) return 0;
    }
    s->generation = r->generation; s->sequence = r->sequence; s->seen_ms = now;
    return 1;
}
static int lc_expired(const lc_owner *s, uint64_t now) {
    return s->busy && (now < s->seen_ms || now - s->seen_ms >= LC_LEASE_MS);
}
static int lc_format(const lc_owner *s, char *out, size_t size) {
    int n = snprintf(out, size, "M2LS 1 %s %s %llu %llu %s\n", s->daemon, s->owner,
        (unsigned long long)s->generation, (unsigned long long)s->sequence, s->state);
    return n > 0 && (size_t)n < size;
}
#endif

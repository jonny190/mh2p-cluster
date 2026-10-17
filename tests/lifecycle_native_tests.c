/* CC BY-NC-SA 4.0; see LICENSE.md. */
#include "cluster_lifecycle.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); exit(1); } } while (0)
int main(void) {
    lc_owner s; lc_request r; char line[512], out[512];
    const char *daemon = "11111111111111111111111111111111";
    const char *owner = "22222222222222222222222222222222";
    lc_init(&s, daemon);
#define REQUEST(g,q,a) do { snprintf(line, sizeof(line), "M2LC 1 %s %s %d %d %s\n", daemon, owner, g,q,a); CHECK(lc_parse(line,&r)); } while(0)
    REQUEST(1,1,"RENDER"); CHECK(!lc_accept(&s,&r,100));
    REQUEST(1,1,"STOP"); CHECK(lc_accept(&s,&r,100));
    CHECK(!lc_accept(&s,&r,4000)); CHECK(s.seen_ms == 100);
    REQUEST(1,2,"PREPARE capture=h264"); CHECK(!lc_accept(&s,&r,200));
    REQUEST(2,2,"PREPARE capture=h264"); CHECK(lc_accept(&s,&r,200)); s.busy = 1;
    REQUEST(2,3,"RENDER"); CHECK(lc_accept(&s,&r,250));
    CHECK(!lc_expired(&s,5249)); CHECK(lc_expired(&s,5250)); CHECK(lc_expired(&s,10));
    REQUEST(3,4,"PREPARE capture=h264"); CHECK(!lc_accept(&s,&r,300));
    REQUEST(2,4,"STOP"); CHECK(lc_accept(&s,&r,300));
    REQUEST(2,5,"RENDER"); CHECK(!lc_accept(&s,&r,350));
    REQUEST(3,5,"PREPARE capture=h264"); CHECK(!lc_accept(&s,&r,350));
    s.busy=0;
    REQUEST(3,5,"PREPARE capture=h264"); CHECK(lc_accept(&s,&r,400)); s.busy=1;
    REQUEST(2,6,"STOP"); CHECK(!lc_accept(&s,&r,450));
    r.owner[0]='3'; CHECK(!lc_accept(&s,&r,450));
    REQUEST(3,6,"STOP"); r.daemon[0]='9'; CHECK(!lc_accept(&s,&r,450));
    REQUEST(3,6,"STOP"); CHECK(lc_accept(&s,&r,500)); s.busy=0;
    strcpy(s.state,"STOPPED"); CHECK(lc_format(&s,out,sizeof(out))); CHECK(strstr(out," 3 6 STOPPED\n"));
    CHECK(!lc_parse("M2LC 1 x x 1 1 STOP\n",&r));
    REQUEST(4,7,"PREPARE capture=h264"); line[strlen(line)-1]=0; CHECK(!lc_parse(line,&r));
    snprintf(line,sizeof(line),"M2LC 1 %s %s 4 7 STOP junk\n",daemon,owner); CHECK(!lc_parse(line,&r));
    snprintf(line,sizeof(line),"M2LC 1 %s %s 4 9223372036854775808 STOP\n",daemon,owner); CHECK(!lc_parse(line,&r));
    puts("native lifecycle admission, retirement, replay and lease tests passed");
    return 0;
}

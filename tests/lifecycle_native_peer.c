/* CC BY-NC-SA 4.0; see LICENSE.md.
 * Actual production protocol/admission code, simulated child/display effects.
 * Lets Java drive a separately compiled native process, not a Java peer double.
 */
#include "cluster_lifecycle.h"
int main(void) {
    lc_owner s; char line[512], status[512]; uint64_t now=100;
    lc_init(&s,"11111111111111111111111111111111");
    lc_format(&s,status,sizeof(status)); fputs(status,stdout); fflush(stdout);
    while(fgets(line,sizeof(line),stdin)) {
        lc_request r;
        if(!lc_parse(line,&r)) return 2;
        if(lc_accept(&s,&r,now)) {
            if(!strcmp(r.action,"STOP")) { s.busy=0; strcpy(s.state,"STOPPED"); }
            else if(!strcmp(r.action,"PREPARE") && !s.busy) { s.busy=1; strcpy(s.state,"PREPARED"); }
            else if(!strcmp(r.action,"RENDER")) strcpy(s.state,"RUNNING");
        }
        if(lc_expired(&s,now)) { s.busy=0; s.retired=1; strcpy(s.state,"STOPPED"); }
        lc_format(&s,status,sizeof(status)); fputs(status,stdout); fflush(stdout); now+=100;
    }
    return 0;
}

#include "uts.h"
#include "lib/string.h"

static char nas_uts_hostname[NAS_UTS_NODENAME_MAX+1]="naseed";

bool nas_uts_set_hostname(const char *hostname,size_t length) {
    if(length>NAS_UTS_NODENAME_MAX || (length && !hostname))return false;
    /* A failed usercopy must never partially publish a new hostname. */
    memset(nas_uts_hostname,0,sizeof(nas_uts_hostname));
    if(length)memcpy(nas_uts_hostname,hostname,length);
    return true;
}

void nas_uts_copy_hostname(char output[NAS_UTS_NODENAME_MAX+1]) {
    memcpy(output,nas_uts_hostname,sizeof(nas_uts_hostname));
}

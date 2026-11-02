#include "chrdev.h"
#include "../../errno.h"
#include <stddef.h>

#define NAS_CHRDEV_PROVIDER_LIMIT 16U
static const struct nas_chrdev_provider *providers[NAS_CHRDEV_PROVIDER_LIMIT];

int nas_chrdev_provider_register(const struct nas_chrdev_provider *provider) {
    if(!provider || !provider->name || !provider->name[0] ||
       !provider->describe || !provider->open)return -EINVAL;
    unsigned free_index=NAS_CHRDEV_PROVIDER_LIMIT;
    for(unsigned i=0;i<NAS_CHRDEV_PROVIDER_LIMIT;i++) {
        if(providers[i]==provider)return -EEXIST;
        if(!providers[i] && free_index==NAS_CHRDEV_PROVIDER_LIMIT)
            free_index=i;
    }
    if(free_index==NAS_CHRDEV_PROVIDER_LIMIT)return -ENOSPC;
    providers[free_index]=provider;
    return 0;
}
int nas_chrdev_describe(const char *absolute,struct nas_chrdev_info *out,
                        const struct nas_chrdev_provider **provider) {
    if(!absolute || absolute[0]!='/' || !out || !provider)return -EINVAL;
    *out=(struct nas_chrdev_info){0};
    *provider=NULL;
    for(unsigned i=0;i<NAS_CHRDEV_PROVIDER_LIMIT;i++) {
        if(!providers[i])continue;
        struct nas_chrdev_info found={0};
        int rc=providers[i]->describe(absolute,&found);
        if(rc==-ENOENT)continue;
        if(rc)return rc;
        if(!found.fd_kind || (found.mode&0170000U)!=0020000U)
            return -EINVAL;
        *out=found;
        *provider=providers[i];
        return 0;
    }
    return -ENOENT;
}

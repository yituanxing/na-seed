#include "chrdev.h"
#include "../../errno.h"
#include "../../lib/string.h"
#include <stddef.h>
#include <limits.h>

#define NAS_CHRDEV_PROVIDER_LIMIT 16U
#define NAS_CHRDEV_NODE_LIMIT 48U
static const struct nas_chrdev_provider *providers[NAS_CHRDEV_PROVIDER_LIMIT];
static struct nas_chrdev_node *nodes[NAS_CHRDEV_NODE_LIMIT];
static uint64_t next_devfs_ino=UINT64_C(65536);
static bool node_present(const struct nas_chrdev_node *node) {
    return node && node->registered &&
           (!node->present || node->present(node->info.unit));
}

int nas_chrdev_node_register(struct nas_chrdev_node *node) {
    if(!node || !node->path || strncmp(node->path,"/dev/",5U) ||
       !node->path[5] || !node->info.fd_kind ||
       (node->info.mode&0170000U)!=0020000U)
        return -EINVAL;
    if(node->registered || node->pins)return -EBUSY;
    if(next_devfs_ino==UINT64_MAX)return -ENOSPC;
    unsigned free_index=NAS_CHRDEV_NODE_LIMIT;
    for(unsigned i=0;i<NAS_CHRDEV_NODE_LIMIT;i++) {
        if(nodes[i]==node || (nodes[i] &&
           (!strcmp(nodes[i]->path,node->path) ||
            nodes[i]->info.rdev==node->info.rdev)))
            return -EEXIST;
        if(!nodes[i] && free_index==NAS_CHRDEV_NODE_LIMIT)free_index=i;
    }
    if(free_index==NAS_CHRDEV_NODE_LIMIT)return -ENOSPC;
    nodes[free_index]=node;
    node->devfs_ino=next_devfs_ino++;
    node->registered=true;
    return 0;
}
int nas_chrdev_node_unregister(struct nas_chrdev_node *node) {
    if(!node || !node->registered)return -EINVAL;
    if(node->pins)return -EBUSY;
    for(unsigned i=0;i<NAS_CHRDEV_NODE_LIMIT;i++) {
        if(nodes[i]!=node)continue;
        nodes[i]=NULL;
        node->registered=false;
        return 0;
    }
    return -EINVAL;
}
int nas_chrdev_node_pin(struct nas_chrdev_node *node) {
    if(!node_present(node))
        return -ENODEV;
    if(node->pins==UINT_MAX)return -EOVERFLOW;
    node->pins++;
    return 0;
}
void nas_chrdev_node_put(struct nas_chrdev_node *node) {
    if(!node || !node->pins)__builtin_trap();
    node->pins--;
}
struct nas_chrdev_node *nas_chrdev_node_at(unsigned ordinal) {
    for(unsigned i=0;i<NAS_CHRDEV_NODE_LIMIT;i++) {
        if(!node_present(nodes[i]))continue;
        if(!ordinal--)return nodes[i];
    }
    return NULL;
}
int nas_chrdev_resolve_rdev(uint64_t rdev,struct nas_chrdev_info *out,
                            const struct nas_chrdev_provider **provider,
                            struct nas_chrdev_node **node) {
    if(!out || !provider || !node)return -EINVAL;
    *out=(struct nas_chrdev_info){0};
    *provider=NULL;
    *node=NULL;
    for(unsigned i=0;i<NAS_CHRDEV_NODE_LIMIT;i++) {
        struct nas_chrdev_node *candidate=nodes[i];
        if(!candidate || candidate->info.rdev!=rdev)continue;
        if(!node_present(candidate))return -ENODEV;
        *out=candidate->info;
        *provider=candidate->provider;
        *node=candidate;
        return 0;
    }
    return -ENODEV;
}

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
                        const struct nas_chrdev_provider **provider,
                        struct nas_chrdev_node **node) {
    if(!absolute || absolute[0]!='/' || !out || !provider || !node)
        return -EINVAL;
    *out=(struct nas_chrdev_info){0};
    *provider=NULL;
    *node=NULL;
    for(unsigned i=0;i<NAS_CHRDEV_NODE_LIMIT;i++) {
        struct nas_chrdev_node *candidate=nodes[i];
        if(!candidate || strcmp(candidate->path,absolute))continue;
        if(!node_present(candidate))
            return -ENOENT;
        *out=candidate->info;
        *provider=candidate->provider;
        *node=candidate;
        return 0;
    }
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

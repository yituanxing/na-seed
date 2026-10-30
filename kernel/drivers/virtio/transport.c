#include "transport.h"
#include "../../errno.h"
#include <stdint.h>
#include <stddef.h>
static bool valid(const struct nas_virtio_transport *t) {
    return t && t->context && t->dma && t->ops &&
        t->ops->reset && t->ops->read_status && t->ops->write_status &&
        t->ops->read_features && t->ops->write_features &&
        t->ops->setup_queue && t->ops->notify_queue && t->ops->read_config32;
}
void nas_virtio_fail(const struct nas_virtio_transport *t) {
    if (t && t->context && t->ops && t->ops->read_status &&
        t->ops->write_status)
        t->ops->write_status(t->context, t->ops->read_status(t->context) |
                             NAS_VIRTIO_STATUS_FAILED);
}
int nas_virtio_negotiate(const struct nas_virtio_transport *t,
                        uint64_t required,uint64_t optional,uint64_t *accepted){
    if (!valid(t) || !accepted) return -EINVAL;
    const struct nas_virtio_transport_ops *ops=t->ops;
    ops->reset(t->context);
    ops->write_status(t->context,NAS_VIRTIO_STATUS_ACK|NAS_VIRTIO_STATUS_DRIVER);
    uint64_t supported=(uint64_t)ops->read_features(t->context,0) |
                       ((uint64_t)ops->read_features(t->context,1)<<32);
    required |= NAS_VIRTIO_F_VERSION_1; /* all NaSeed queues are modern. */
    if ((supported & required) != required) {
        nas_virtio_fail(t);
        return -EOPNOTSUPP;
    }
    uint64_t negotiated=supported&(required|optional);
    ops->write_features(t->context,0,(uint32_t)negotiated);
    ops->write_features(t->context,1,(uint32_t)(negotiated>>32));
    ops->write_status(t->context,NAS_VIRTIO_STATUS_ACK |
                       NAS_VIRTIO_STATUS_DRIVER|NAS_VIRTIO_STATUS_FEATURES_OK);
    if (!(ops->read_status(t->context)&NAS_VIRTIO_STATUS_FEATURES_OK)) {
        nas_virtio_fail(t);
        return -EOPNOTSUPP;
    }
    *accepted=negotiated;
    return 0;
}
int nas_virtio_setup_queue(const struct nas_virtio_transport *t,unsigned qid,
                           unsigned count,uint64_t desc,uint64_t driver,
                           uint64_t device) {
    if (!valid(t) || !count || !desc || !driver || !device) return -EINVAL;
    int rc=t->ops->setup_queue(t->context,qid,count,desc,driver,device);
    if (rc) nas_virtio_fail(t);
    return rc;
}
int nas_virtio_activate(const struct nas_virtio_transport *t) {
    if (!valid(t)) return -EINVAL;
    uint32_t status=t->ops->read_status(t->context);
    if (!(status&NAS_VIRTIO_STATUS_FEATURES_OK) ||
        (status&NAS_VIRTIO_STATUS_FAILED)) return -EIO;
    t->ops->write_status(t->context,status|NAS_VIRTIO_STATUS_DRIVER_OK);
    return 0;
}
void nas_virtio_notify(const struct nas_virtio_transport *t,unsigned qid) {
    if(t && t->context && t->ops && t->ops->notify_queue)
        t->ops->notify_queue(t->context,qid);
}
uint32_t nas_virtio_config32(const struct nas_virtio_transport *t,uint32_t offset){
    if(!valid(t))return 0;
    return t->ops->read_config32(t->context,offset);
}

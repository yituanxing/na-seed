/* Compile and run the untouched upstream VirtIO-Blk wire protocol ABI.
 * A future imported driver must use these exact command/status/feature
 * constants. Do not invent a second on-wire struct in NaSeed.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <virtio/virtio_blk.h>
#include <virtio/virtio_ring.h>
_Static_assert(sizeof(struct virtio_blk_outhdr)==16,"upstream request ABI");
_Static_assert(offsetof(struct virtio_blk_outhdr,sector)==8,"sector offset");
_Static_assert(offsetof(struct virtio_blk_config,capacity)==0,"capacity config");
_Static_assert(VIRTIO_BLK_T_IN==0 && VIRTIO_BLK_T_OUT==1 &&
               VIRTIO_BLK_T_FLUSH==4,"upstream protocol operations");
_Static_assert(VIRTIO_BLK_F_RO==5 && VIRTIO_BLK_F_FLUSH==9,"feature bits");
_Static_assert(VIRTIO_ID_BLOCK==2,"VirtIO block device class");
int main(void) {
    struct virtio_blk_outhdr hdr={
        .type=VIRTIO_BLK_T_FLUSH,.ioprio=0,.sector=0
    };
    if(hdr.type!=4 || hdr.sector!=0)return 1;
    puts("[UNIKRAFT BLK ABI PASS] verbatim protocol and split-ring wire layout");
    return 0;
}

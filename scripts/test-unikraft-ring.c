/* Genuine byte-identical Unikraft vring definitions and layout functions. */
#include <virtio/virtio_ring.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define N 8U
int main(void) {
    _Alignas(256) uint8_t data[4096];
    memset(data,0,sizeof(data));
    struct vring vr;
    assert(sizeof(struct vring_desc)==16);
    assert(sizeof(struct vring_used_elem)==8);
    assert(vring_size(N,256U)<=sizeof(data));
    vring_init(&vr,N,data,256U);
    assert(vr.desc==(struct vring_desc *)data);
    assert((uintptr_t)vr.avail-(uintptr_t)data==N*16U);
    assert((uintptr_t)vr.used-(uintptr_t)data==256U);
    assert(vr.num==N);
    assert(vring_need_event(0U,1U,0U));
    assert(!vring_need_event(1U,1U,0U));
    assert(vring_need_event(65535U,0U,65535U));
    puts("[UNIKRAFT VRING PASS] pinned upstream split-queue layout, alignment, wrap event");
    return 0;
}

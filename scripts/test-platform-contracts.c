/* Host-only pure contract checks, not hardware access or boot proof. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kernel/platform/virtio_qemu_classes.h"
#include "../kernel/platform/x86_pci_config.h"
#include "../kernel/mm/anon_regions.h"
#include "../kernel/time/counter.h"
static unsigned calls, fail_at;
static char logbuf[512];
static unsigned used;
void nas_print(const char *s) {
    unsigned n=(unsigned)strlen(s);
    assert(used+n<sizeof(logbuf));
    memcpy(logbuf+used,s,n+1);used+=n;
}
void nas_dec(uint64_t n) {
    char s[32];
    snprintf(s,sizeof(s),"%llu",(unsigned long long)n);
    nas_print(s);
}
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
static int registration(unsigned id) {
    assert(++calls==id);
    return calls==fail_at?-17:0;
}
int nas_async_blk_driver_register(void){return registration(1);}
int nas_virtio_blk_driver_register(void){return registration(2);}
int nas_virtio_net_driver_register(void){return registration(3);}
int nas_virtio_input_driver_register(void){return registration(4);}
int nas_virtio_gpu_driver_register(void){return registration(5);}
unsigned nas_async_blk_count(void){return 5;}
unsigned nas_virtio_blk_count(void){return 1;}
unsigned nas_virtio_net_count(void){return 2;}
unsigned nas_virtio_input_count(void){return 3;}
unsigned nas_virtio_gpu_count(void){return 4;}
#endif
int main(void) {
    assert(nas_x86_pci_address(0,0,0,0)==0x80000000U);
    assert(nas_x86_pci_address(255,31,7,255)==0x80fffffcU);
    assert(nas_x86_pci_address(0,4,0,4)==0x80002004U);
    assert(nas_counter_ns(123456789,10000000)==12345678900ULL);
    assert(nas_counter_ns(32769,32768)==1000030517ULL);
    for(unsigned k=0;k<=NAS_VMA_DEVICE_SHARED;k++){
        bool direct=k==NAS_VMA_ANON_SHARED || k==NAS_VMA_DEVICE_SHARED;
        assert(nas_vma_direct_shared(k)==direct);
        assert(nas_vma_preserves_shared_frames(k)==(direct || k==NAS_VMA_FILE_MUTABLE_SHARED));
        assert(nas_vma_provider_writes(k)==(k==NAS_VMA_DEVICE_SHARED || k==NAS_VMA_FILE_MUTABLE_SHARED));
    }
    assert(nas_qemu_virtio_supported_class(VIRTIO_ID_BLOCK));
    assert(nas_qemu_virtio_supported_class(VIRTIO_ID_NET));
    assert(nas_qemu_virtio_supported_class(VIRTIO_ID_INPUT));
    assert(nas_qemu_virtio_supported_class(VIRTIO_ID_GPU));
    assert(!nas_qemu_virtio_supported_class(0));
    assert(!nas_qemu_virtio_supported_class(UINT32_MAX));
#ifdef NAS_ENABLE_VIRTIO_BLK_ASYNC
    for(fail_at=1;fail_at<=5;fail_at++){
        calls=0;assert(nas_qemu_virtio_register_classes()==-17);
        assert(calls==fail_at);
    }
    calls=0;assert(nas_qemu_virtio_register_classes()==0 && calls==5);
    nas_qemu_virtio_report_classes("PCI");
    assert(!strcmp(logbuf,"[VIRTIO-BLK] real modern PCI devices=1\n"
        "[VIRTIO-NET] real modern PCI devices=2\n"
        "[VIRTIO-INPUT] real modern PCI devices=3\n"
        "[VIRTIO-GPU] real modern PCI devices=4\n"
        "[VIRTIO-BLK-ASYNC] real modern PCI devices=5\n"));
#else
    assert(!nas_qemu_virtio_register_classes() && !calls && !fail_at);
    nas_qemu_virtio_report_classes("MMIO");
    assert(!strcmp(logbuf,"[VIRTIO-BLK] real modern MMIO devices=0\n"
        "[VIRTIO-NET] real modern MMIO devices=0\n"));
#endif
    puts("[PLATFORM CONTRACT HOST PASS] PCI address, timer conversion, VMA policy, class order and failures");
    return 0;
}

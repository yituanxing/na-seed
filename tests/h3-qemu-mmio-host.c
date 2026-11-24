#include <assert.h>
#include "../kernel/platform/x86_qemu_hw_contract.h"

int main(void) {
    static uint32_t words[0x20000U/4U];
    static const uint32_t sizes[]={4096U,8192U,16384U,0x20000U};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(sizes[0]);i++) {
        struct nas_x86_qemu_mmio map={(uintptr_t)words,sizes[i]};
        uint32_t value=0;
        assert(nas_x86_qemu_mmio_write32(&map,0,0x12345678U)==0);
        assert(nas_x86_qemu_mmio_read32(&map,0,&value)==0 && value==0x12345678U);
        assert(nas_x86_qemu_mmio_write32(&map,map.bytes-4U,0xabcdef01U)==0);
        assert(nas_x86_qemu_mmio_read32(&map,map.bytes-4U,&value)==0 && value==0xabcdef01U);
        assert(nas_x86_qemu_mmio_write32(&map,map.bytes,1U)==-EINVAL);
        assert(nas_x86_qemu_mmio_read32(&map,1,&value)==-EINVAL);
        assert(nas_x86_qemu_mmio_read32(&map,UINT32_MAX-3U,&value)==-EINVAL);
        assert(nas_x86_qemu_mmio_read32(&map,0,NULL)==-EINVAL);
        map.bytes=0;
        assert(nas_x86_qemu_mmio_write32(&map,0,1U)==-EINVAL);
    }
    assert(nas_x86_qemu_mmio_read32(NULL,0,NULL)==-EINVAL);
    struct nas_pci_resources resources={.bar_count=2,.bars={
        {.index=0,.kind=NAS_PCI_BAR_MEM64,.base=0x1000},
        {.index=5,.kind=NAS_PCI_BAR_MEM32,.base=0x2000}
    }};
    assert(nas_x86_qemu_find_bar(&resources,0)==&resources.bars[0]);
    assert(nas_x86_qemu_find_bar(&resources,5)==&resources.bars[1]);
    assert(!nas_x86_qemu_find_bar(&resources,1));
    assert(!nas_x86_qemu_find_bar(NULL,0));
    return 0;
}

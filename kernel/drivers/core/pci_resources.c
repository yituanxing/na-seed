#include "pci_resources.h"
#include "../../errno.h"
#include <stddef.h>

static uint32_t read_config(const struct nas_pci_config_reader *r,
                            const struct nas_pci_function_info *d,
                            uint16_t offset) {
    return r->read32(r->context,d->bus,d->slot,d->function,offset);
}

static void clear_resources(struct nas_pci_resources *out) {
    unsigned char *p=(unsigned char *)out;
    for(size_t i=0;i<sizeof(*out);i++)p[i]=0;
}

int nas_pci_resources_probe(const struct nas_pci_config_reader *reader,
                             const struct nas_pci_function_info *info,
                             struct nas_pci_resources *out) {
    if(!out)return -EINVAL;
    clear_resources(out);
    if(!reader || !reader->read32 || !info ||
       info->slot>=32U || info->function>=8U)return -EINVAL;
    struct nas_pci_resources found={0};
    if(info->header_type==0U)found.bar_slots=6U;
    else if(info->header_type==1U)found.bar_slots=2U;
    else return -EIO;
    found.header_type=info->header_type;

    uint32_t command_status=read_config(reader,info,4U);
    if(command_status==UINT32_MAX)return -EIO;
    found.command=(uint16_t)command_status;
    found.status=(uint16_t)(command_status>>16);
    for(uint8_t i=0;i<found.bar_slots;i++) {
        uint32_t raw=read_config(reader,info,(uint16_t)(0x10U+4U*i));
        /* Unimplemented, unassigned or all-ones resources are NOT mapped. */
        if(raw==0U || raw==UINT32_MAX)continue;
        struct nas_pci_bar_desc bar={.index=i};
        if(raw&1U) {
            bar.kind=NAS_PCI_BAR_IO;
            bar.base=(uint64_t)(raw&~UINT32_C(3));
        } else {
            unsigned type=(raw>>1)&3U;
            bar.prefetchable=(raw&8U)!=0U;
            if(type==0U) {
                bar.kind=NAS_PCI_BAR_MEM32;
                bar.base=(uint64_t)(raw&~UINT32_C(15));
            } else if(type==2U) {
                if(i+1U>=found.bar_slots)return -EIO;
                uint32_t high=read_config(reader,info,
                                           (uint16_t)(0x10U+4U*(i+1U)));
                if(high==UINT32_MAX)return -EIO;
                bar.kind=NAS_PCI_BAR_MEM64;
                bar.base=((uint64_t)high<<32)|
                         (uint64_t)(raw&~UINT32_C(15));
                ++i;
            } else return -EIO; /* Legacy 20-bit or reserved BAR type. */
        }
        if(bar.base)found.bars[found.bar_count++]=bar;
    }

    if(found.status&(1U<<4)) {
        uint32_t pointer_reg=read_config(reader,info,0x34U);
        if(pointer_reg==UINT32_MAX)return -EIO;
        uint8_t pointer=(uint8_t)pointer_reg;
        uint8_t seen[64]={0};
        if(!pointer)return -EIO;
        while(pointer) {
            if(pointer<0x40U || (pointer&3U) || seen[pointer>>2] ||
               found.cap_count==NAS_PCI_RESOURCE_CAP_LIMIT)return -ELOOP;
            seen[pointer>>2]=1U;
            uint32_t cap=read_config(reader,info,pointer);
            if(cap==UINT32_MAX)return -EIO;
            uint8_t id=(uint8_t)cap;
            uint8_t next=(uint8_t)(cap>>8);
            if(!id || id==UINT8_MAX)return -EIO;
            found.caps[found.cap_count++]=(struct nas_pci_cap_desc){
                .offset=pointer,.id=id,.next=next
            };
            pointer=next;
        }
    }
    *out=found;
    return 0;
}

static bool pci_range_valid(uint64_t base,uint64_t bytes) {
    return base && bytes && !(base&4095U) && !(bytes&4095U) &&
           base<=UINT64_MAX-bytes;
}

int nas_pci_mmio_claim_bar(struct nas_pci_mmio_owner *owner,
                           const struct nas_pci_resources *resources,
                           uint8_t bar_index,uint64_t required_bytes,
                           struct nas_pci_mmio_claim *out) {
    if(!out)return -EINVAL;
    *out=(struct nas_pci_mmio_claim){0};
    if(!owner || !resources ||
       !pci_range_valid(owner->window_base,owner->window_bytes) ||
       !required_bytes || (required_bytes&4095U) ||
       owner->held>=NAS_PCI_MMIO_CLAIM_LIMIT) return -EINVAL;
    const struct nas_pci_bar_desc *bar=NULL;
    for(unsigned i=0;i<resources->bar_count &&
                       i<NAS_PCI_RESOURCE_BAR_LIMIT;i++) {
        if(resources->bars[i].index==bar_index) {
            if(bar)return -EIO; /* malformed duplicate BAR index */
            bar=&resources->bars[i];
        }
    }
    if(!bar || (bar->kind!=NAS_PCI_BAR_MEM32 &&
                 bar->kind!=NAS_PCI_BAR_MEM64) ||
       !pci_range_valid(bar->base,required_bytes))return -ENODEV;
    if(bar->base<owner->window_base ||
       bar->base-owner->window_base>=owner->window_bytes ||
       required_bytes>owner->window_bytes-(bar->base-owner->window_base))
        return -ENODEV;
    uint64_t end=bar->base+required_bytes;
    for(unsigned i=0;i<owner->held;i++) {
        uint64_t old_base=owner->claims[i].base;
        uint64_t old_end=old_base+owner->claims[i].bytes;
        if(bar->base<old_end && old_base<end)return -EBUSY;
    }
    struct nas_pci_mmio_claim claim={
        .base=bar->base,.bytes=required_bytes,.bar_index=bar_index
    };
    owner->claims[owner->held++]=claim;
    *out=claim;
    return 0;
}

int nas_pci_mmio_release(struct nas_pci_mmio_owner *owner,
                          const struct nas_pci_mmio_claim *claim) {
    if(!owner || !claim || !claim->bytes ||
       owner->held>NAS_PCI_MMIO_CLAIM_LIMIT)return -EINVAL;
    for(unsigned i=0;i<owner->held;i++) {
        const struct nas_pci_mmio_claim *old=&owner->claims[i];
        if(old->base!=claim->base || old->bytes!=claim->bytes ||
           old->bar_index!=claim->bar_index)continue;
        if(old->mapping_pinned)return -EBUSY;
        for(unsigned j=i+1U;j<owner->held;j++)
            owner->claims[j-1U]=owner->claims[j];
        --owner->held;
        owner->claims[owner->held]=(struct nas_pci_mmio_claim){0};
        return 0;
    }
    return -ENODEV;
}

int nas_pci_mmio_pin_mapping_attempt(struct nas_pci_mmio_owner *owner,
                                      const struct nas_pci_mmio_claim *claim) {
    if(!owner || !claim || !claim->bytes ||
       owner->held>NAS_PCI_MMIO_CLAIM_LIMIT)return -EINVAL;
    for(unsigned i=0;i<owner->held;i++) {
        struct nas_pci_mmio_claim *held=&owner->claims[i];
        if(held->base!=claim->base || held->bytes!=claim->bytes ||
           held->bar_index!=claim->bar_index)continue;
        held->mapping_pinned=true;
        return 0;
    }
    return -ENODEV;
}

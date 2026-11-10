#include "../kernel/drivers/core/pci_resources.h"
#include "../kernel/errno.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One PCI config function, entirely in memory. Only a DWORD read callback is
 * exposed: it is impossible for this test or resource decoder to size BARs,
 * alter PCI Command, turn on DMA, acknowledge MSI, or write storage. */
struct fixture {
    uint32_t reg[64];
    unsigned reads;
    unsigned bad_access;
};

static uint32_t fake_read32(void *context, uint8_t bus, uint8_t slot,
                            uint8_t function, uint16_t offset) {
    struct fixture *f=context;
    if (bus != 0U || slot != 3U || function != 0U ||
        offset >= 256U || (offset & 3U)) {
        ++f->bad_access;
        return UINT32_MAX;
    }
    ++f->reads;
    return f->reg[offset / 4U];
}

static struct nas_pci_function_info device(void) {
    return (struct nas_pci_function_info){
        .bus=0U,.slot=3U,.function=0U,
        .vendor_id=0x8086U,.device_id=0x1234U,
        .class_code=1U,.subclass=8U,.programming_interface=2U,
        .header_type=0U
    };
}

static void good_fixture(struct fixture *f) {
    memset(f,0,sizeof(*f));
    f->reg[0x00/4] = UINT32_C(0x12348086);
    /* PCI status bit 4 advertises a legacy capability list. */
    f->reg[0x04/4] = UINT32_C(0x00100007);
    f->reg[0x08/4] = UINT32_C(0x01080201);
    f->reg[0x0c/4] = 0U;
    /* 0: MEM32; 1-2: PREFETCHABLE MEM64; 3: I/O; 4: unused; 5: MEM32 */
    f->reg[0x10/4] = UINT32_C(0xfebf0000);
    f->reg[0x14/4] = UINT32_C(0x1000000c);
    f->reg[0x18/4] = UINT32_C(0x00000001);
    f->reg[0x1c/4] = UINT32_C(0x0000c001);
    f->reg[0x20/4] = 0U;
    f->reg[0x24/4] = UINT32_C(0xfff00000);
    f->reg[0x34/4] = UINT32_C(0x50);
    f->reg[0x50/4] = UINT32_C(0x00006005); /* MSI -> MSI-X */
    f->reg[0x60/4] = UINT32_C(0x00000011);
}

static void check(int ok, const char *message) {
    if (!ok) {
        fprintf(stderr,"[H2 PCI RESOURCE FAIL] %s\n",message);
        exit(1);
    }
}

static int probe(struct fixture *f,const struct nas_pci_function_info *info,
                 struct nas_pci_resources *resources) {
    const struct nas_pci_config_reader reader={f,fake_read32};
    return nas_pci_resources_probe(&reader,info,resources);
}

static void probe_failed(struct fixture *f,
                         const struct nas_pci_function_info *info,
                         int expected, const char *message) {
    struct nas_pci_resources r;
    memset(&r,0xa5,sizeof(r));
    int rc=probe(f,info,&r);
    check(rc==expected,message);
    const uint8_t *p=(const uint8_t *)&r;
    for(size_t i=0;i<sizeof(r);i++)
        check(p[i]==0U,"failure must not leak partial BAR/capability data");
    check(!f->bad_access,"invalid config access after failure");
}

int main(void) {
    struct fixture f;
    struct nas_pci_resources r;
    struct nas_pci_function_info dev=device();

    good_fixture(&f);
    check(!probe(&f,&dev,&r),"valid BAR and capability read-only snapshot");
    check(r.command==7U && (r.status & 0x10U),"status/command snapshot");
    check(r.header_type==0U && r.bar_slots==6U && r.bar_count==4U,
          "only 4 implemented BARs, no sizing writes");
    check(r.bars[0].index==0U && r.bars[0].kind==NAS_PCI_BAR_MEM32 &&
          r.bars[0].base==UINT64_C(0xfebf0000),"MEM32 resource");
    check(r.bars[1].index==1U && r.bars[1].kind==NAS_PCI_BAR_MEM64 &&
          r.bars[1].prefetchable &&
          r.bars[1].base==UINT64_C(0x0000000110000000),
          "64-bit BAR consumes a high DWORD, preserves absolute address");
    check(r.bars[2].index==3U && r.bars[2].kind==NAS_PCI_BAR_IO &&
          r.bars[2].base==UINT64_C(0x0000c000),"I/O BAR base");
    check(r.bars[3].index==5U && r.bars[3].kind==NAS_PCI_BAR_MEM32 &&
          r.bars[3].base==UINT64_C(0xfff00000),"last-slot MEM32");
    check(r.cap_count==2U && r.caps[0].id==5U &&
          r.caps[0].offset==0x50U && r.caps[0].next==0x60U &&
          r.caps[1].id==0x11U && r.caps[1].next==0U,
          "bounded MSI/MSI-X capability list, no IRQ activation");
    check(!f.bad_access && f.reads<=32U,"bounded and aligned DWORD reads");

    good_fixture(&f);
    f.reg[0x24/4]=UINT32_C(0x00001004);
    probe_failed(&f,&dev,-EIO,"MEM64 cannot consume BAR beyond slot 5");

    good_fixture(&f);
    f.reg[0x10/4]=UINT32_C(0xfebf0002);
    probe_failed(&f,&dev,-EIO,"reserved BAR memory type rejected");

    good_fixture(&f);
    f.reg[0x34/4]=UINT32_C(0x53);
    probe_failed(&f,&dev,-ELOOP,"misaligned first capability pointer");

    good_fixture(&f);
    f.reg[0x34/4]=UINT32_C(0x3c);
    probe_failed(&f,&dev,-ELOOP,"capability below the safe 0x40 boundary");

    good_fixture(&f);
    f.reg[0x50/4]=UINT32_C(0x00005005);
    probe_failed(&f,&dev,-ELOOP,"self-referential capability chain");

    good_fixture(&f);
    f.reg[0x60/4]=UINT32_C(0x00005011);
    probe_failed(&f,&dev,-ELOOP,"two-node capability loop");

    good_fixture(&f);
    /* An acyclic but deliberately oversized linked list must still be
     * rejected, rather than following unbounded firmware-provided links. */
    f.reg[0x34/4]=UINT32_C(0x40);
    for(unsigned i=0;i<=32U;i++) {
        unsigned offset=0x40U+4U*i;
        unsigned next=i==32U ? 0U : offset+4U;
        f.reg[offset/4U]=(uint32_t)((next<<8)|1U);
    }
    probe_failed(&f,&dev,-ELOOP,"capability chain exceeds fixed snapshot limit");

    good_fixture(&f);
    f.reg[0x20/4]=UINT32_MAX;
    check(!probe(&f,&dev,&r) && r.bar_count==4U,
          "all-ones unimplemented BAR ignored without sizing probe");

    good_fixture(&f);
    f.reg[0x04/4]=UINT32_C(0x00000007);
    f.reg[0x34/4]=UINT32_C(0x53);
    check(!probe(&f,&dev,&r) && r.cap_count==0U,
          "do not follow garbage pointer without status capability bit");

    good_fixture(&f);
    check(!probe(&f,&dev,&r),"restore BAR snapshot for scoped MMIO claims");
    struct nas_pci_mmio_owner owner={
        .window_base=UINT64_C(0x0000000100000000),
        .window_bytes=UINT64_C(0x0000000020000000)
    };
    struct nas_pci_mmio_claim claim={0};
    check(!nas_pci_mmio_claim_bar(&owner,&r,1U,8192U,&claim) &&
          owner.held==1U && claim.base==UINT64_C(0x0000000110000000) &&
          claim.bytes==8192U && claim.bar_index==1U,
          "independently bounded MMIO window admits chosen 8KiB BAR extent");
    struct nas_pci_mmio_claim second={0};
    check(nas_pci_mmio_claim_bar(&owner,&r,1U,4096U,&second)==-EBUSY &&
          !second.base && owner.held==1U,
          "duplicate/overlapping reservations fail closed");
    check(nas_pci_mmio_claim_bar(&owner,&r,3U,4096U,&second)==-ENODEV &&
          !second.base,"I/O BAR cannot authorize MMIO mapping");
    check(nas_pci_mmio_claim_bar(&owner,&r,0U,4096U,&second)==-ENODEV &&
          !second.base,"firmware BAR outside bounded host-bridge aperture");
    check(nas_pci_mmio_claim_bar(&owner,&r,1U,1234U,&second)==-EINVAL,
          "MMIO reservation rejects unaligned extent");
    check(!nas_pci_mmio_release(&owner,&claim) && owner.held==0U &&
          nas_pci_mmio_release(&owner,&claim)==-ENODEV,
          "MMIO claim release is explicit and idempotence is rejected");
    check(!nas_pci_mmio_claim_bar(&owner,&r,1U,4096U,&second) &&
          !nas_pci_mmio_release(&owner,&second),
          "released BAR may be reclaimed without stale ownership");

    struct nas_pci_mmio_owner bad_owner={
        .window_base=UINT64_MAX-4095U,.window_bytes=8192U
    };
    check(nas_pci_mmio_claim_bar(&bad_owner,&r,1U,4096U,&second)==-EINVAL,
          "overflowing host bridge window is not trusted");
    bad_owner=(struct nas_pci_mmio_owner){
        .window_base=UINT64_C(0x0000000110000000),.window_bytes=4096U
    };
    check(nas_pci_mmio_claim_bar(&bad_owner,&r,1U,8192U,&second)==-ENODEV,
          "requested MMIO extent must fit independently qualified window");
    struct nas_pci_resources malformed=r;
    malformed.bars[1].base=UINT64_MAX-4095U;
    check(nas_pci_mmio_claim_bar(&owner,&malformed,1U,8192U,&second)==-ENODEV,
          "BAR range cannot wrap address space");
    malformed=r;
    malformed.bars[1].base++;
    check(nas_pci_mmio_claim_bar(&owner,&malformed,1U,4096U,&second)==-ENODEV,
          "unaligned BAR base is never device-mapped");

    good_fixture(&f);
    dev.header_type=1U;
    f.reg[0x14/4]=UINT32_C(0x00001004);
    probe_failed(&f,&dev,-EIO,"bridge MEM64 last-slot pair rejected");

    good_fixture(&f);
    dev.header_type=0x7fU;
    probe_failed(&f,&dev,-EIO,"unknown header type rejected");

    good_fixture(&f);
    dev=device();
    const struct nas_pci_config_reader reader={&f,fake_read32};
    check(nas_pci_resources_probe(NULL,&dev,&r)==-EINVAL,
          "missing config reader rejected");
    check(nas_pci_resources_probe(&reader,NULL,&r)==-EINVAL,
          "missing discovered function rejected");
    check(nas_pci_resources_probe(&reader,&dev,NULL)==-EINVAL,
          "missing result rejected");

    puts("[H2 PCI RESOURCE HOST PASS] BAR types, 64-bit pairs, capability loops, read-only probe, bounded MMIO claims");
    return 0;
}

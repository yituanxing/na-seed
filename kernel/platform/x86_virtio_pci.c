/* QEMU q35 root-bus PCI enumerator. x86 owns config I/O and PTE mapping
 * ONLY; it publishes modern VirtIO device classes to the shared bus.
 * Class drivers (blk, net, etc.) own matching, not this platform file.
 * This still probes QEMU bus 0, not arbitrary PCIe topology. */
#include "../drivers/virtio/pci.h"
#include "../drivers/core/bus.h"
#include "../drivers/core/irq.h"
#include "../drivers/port/dma.h"
#include "../arch/x86_64/mmu.h"
#include "../../ports/uboot/virtio_bus.h"
#include "../../ports/uboot/virtio_net_bus.h"
#ifdef NAS_ENABLE_VIRTIO_INPUT
#include "../drivers/input/virtio_input.h"
#endif
#include "../drivers/serial.h"
#include "../errno.h"
#include <virtio/virtio_ids.h>
#include <stdint.h>
#include <stdbool.h>
#define VIRTIO_DEVICES 8U
#define BAR_COUNT 6U
#define BAR_MAX_BYTES 65536U
#define PCI_MMIO_BASE UINT64_C(0xfffffe9000000000)
#define DEVICE_VA_STRIDE (BAR_COUNT*BAR_MAX_BYTES)
/* This Q35 path uses the existing single-CPU 8259 virtual-wire route.
 * QEMU ICH9 maps bus-0 PCI INTx to PIRQ E-H for slots below 25. The
 * platform routes IRQ-capable class devices (Input and Net); PIC11 cannot
 * be inferred from PCI Interrupt Line (firmware-owned). */
#define ICH9_LPC_SLOT 31U
#define ICH9_PIRQ_E_ROUT 0x68U
#define X86_PIC_DEVICE_IRQ 11U
#define PIRQ_UNKNOWN 0xffU
static uint8_t cfg_snapshot[VIRTIO_DEVICES][256];
static struct nas_virtio_pci_resources resources[VIRTIO_DEVICES];
static struct nas_virtio_pci pci[VIRTIO_DEVICES];
static struct nas_virtio_transport transports[VIRTIO_DEVICES];
static struct nas_bus_device devices[VIRTIO_DEVICES];
static struct nas_irq_action shared_irq_acks[VIRTIO_DEVICES];
static uint8_t device_pirq[VIRTIO_DEVICES];
static bool scanned;
static unsigned block_count(void){
#ifdef NAS_ENABLE_VIRTIO_BLK
    return nas_virtio_blk_count();
#else
    return 0;
#endif
}
static unsigned net_count(void){
#ifdef NAS_ENABLE_VIRTIO_NET
    return nas_virtio_net_count();
#else
    return 0;
#endif
}
static void out32(uint16_t port,uint32_t value) {
    __asm__ volatile("outl %0,%1"::"a"(value),"Nd"(port));
}
static uint32_t in32(uint16_t port) {
    uint32_t value;__asm__ volatile("inl %1,%0":"=a"(value):"Nd"(port));
    return value;
}
static uint32_t cfg_read(uint8_t bus,uint8_t slot,uint8_t fun,
                         unsigned offset) {
    out32(0xcf8,UINT32_C(0x80000000) | ((uint32_t)bus<<16) |
        ((uint32_t)slot<<11) | ((uint32_t)fun<<8) | (offset&0xfcU));
    return in32(0xcfc);
}
static void cfg_write(uint8_t bus,uint8_t slot,uint8_t fun,
                      unsigned offset,uint32_t value) {
    out32(0xcf8,UINT32_C(0x80000000) | ((uint32_t)bus<<16) |
        ((uint32_t)slot<<11) | ((uint32_t)fun<<8) | (offset&0xfcU));
    out32(0xcfc,value);
}
static void load_config(uint8_t bus,uint8_t slot,uint8_t fun,uint8_t *cfg) {
    for(unsigned off=0;off<256;off+=4) {
        uint32_t value=cfg_read(bus,slot,fun,off);
        for(unsigned i=0;i<4;i++)cfg[off+i]=(uint8_t)(value>>(i*8));
    }
}
static uint8_t q35_bus0_pirq(uint8_t slot,uint8_t interrupt_pin) {
    /* QEMU hw/isa/lpc_ich9.c:ich9_cc_init(). For D25-D31, ICH9
     * programmable DxxIR registers override the fallback mapping, so do
     * not silently claim those slots until full PCI routing is provided. */
    if(slot>=25U || interrupt_pin<1U || interrupt_pin>4U)
        return PIRQ_UNKNOWN;
    return (uint8_t)(4U+((slot+interrupt_pin-1U)&3U));
}
static int q35_route_device_pirqs(uint8_t enabled_pirqs) {
    if(!enabled_pirqs)return 0;
    /* Intel 82801IB/ICH9 LPC is bus0:device31:function0 on Q35.  An
     * unrecognized host bridge is an explicit unsupported platform, not
     * permission to write unknown PCI config bytes. */
    if(cfg_read(0,ICH9_LPC_SLOT,0,0)!=UINT32_C(0x29188086))
        return -ENODEV;
    uint32_t route=cfg_read(0,ICH9_LPC_SLOT,0,ICH9_PIRQ_E_ROUT);
    uint32_t updated=route;
    for(unsigned pirq=4U;pirq<8U;pirq++) {
        if(!(enabled_pirqs&(1U<<pirq)))continue;
        unsigned shift=(pirq-4U)*8U;
        updated=(updated&~(UINT32_C(0xff)<<shift)) |
                ((uint32_t)X86_PIC_DEVICE_IRQ<<shift);
    }
    cfg_write(0,ICH9_LPC_SLOT,0,ICH9_PIRQ_E_ROUT,updated);
    if(cfg_read(0,ICH9_LPC_SLOT,0,ICH9_PIRQ_E_ROUT)!=updated)
        return -EIO;
    return 0;
}
static int q35_select_pci_intx(uint8_t slot,uint8_t function,
                               const uint8_t *config) {
    /* A modern-only VirtIO PCI function still has INTx.  Firmware might
     * have left MSI/MSI-X enabled or PCI COMMAND.INTX_DISABLE set; force
     * our chosen INTx path explicitly before the class driver activates. */
    uint8_t visited[256]={0};
    unsigned cap=config[0x34U];
    for(unsigned traversed=0;cap;traversed++) {
        if(traversed>=48U || cap<0x40U || cap>0xfcU ||
           (cap&3U) || visited[cap])return -EINVAL;
        visited[cap]=1U;
        if(config[cap]==0x11U) { /* MSI-X Message Control bit 15 */
            uint32_t word=cfg_read(0,slot,function,cap);
            cfg_write(0,slot,function,cap,word&~(UINT32_C(1)<<31));
        } else if(config[cap]==0x05U) { /* MSI Message Control bit 0 */
            uint32_t word=cfg_read(0,slot,function,cap);
            cfg_write(0,slot,function,cap,word&~(UINT32_C(1)<<16));
        }
        cap=config[cap+1];
    }
    uint32_t command=cfg_read(0,slot,function,4U);
    cfg_write(0,slot,function,4U,command&~(UINT32_C(1)<<10));
    return (cfg_read(0,slot,function,4U)&(UINT32_C(1)<<10))?
           -EIO:0;
}
static void q35_shared_irq_ack_only(void *unused,uint32_t status) {
    (void)unused;
    (void)status;
}
static bool map_bars(uint8_t bus,uint8_t slot,uint8_t fun,unsigned index,
                     struct nas_virtio_pci_resources *res) {
    /* Disable decode while sizing, then enable memory + bus mastering
     * only for the device whose BAR regions passed bounds checks. */
    uint32_t old_cmd=cfg_read(bus,slot,fun,4);
    cfg_write(bus,slot,fun,4,old_cmd&~7U);
    bool mapped=false;
    for(unsigned i=0;i<BAR_COUNT;i++) {
        unsigned off=0x10U+i*4U;
        uint32_t lo=cfg_read(bus,slot,fun,off);
        if(!lo || lo==UINT32_MAX || (lo&1U))continue;
        unsigned type=(lo>>1)&3U;
        if(type!=0U && type!=2U)continue;
        if(type==2U && i+1U>=BAR_COUNT)continue;
        uint32_t hi=type==2U?cfg_read(bus,slot,fun,off+4):0;
        uint64_t address=((uint64_t)hi<<32)|(lo&~UINT32_C(15));
        cfg_write(bus,slot,fun,off,UINT32_MAX);
        if(type==2U)cfg_write(bus,slot,fun,off+4,UINT32_MAX);
        uint32_t probelo=cfg_read(bus,slot,fun,off);
        uint32_t probehi=type==2U?cfg_read(bus,slot,fun,off+4):UINT32_MAX;
        cfg_write(bus,slot,fun,off,lo);
        if(type==2U)cfg_write(bus,slot,fun,off+4,hi);
        uint64_t mask=((uint64_t)probehi<<32)|(probelo&~UINT32_C(15));
        uint64_t size=~mask+1U;
        if(address && !(address&4095U) && size>=4096U &&
           size<=BAR_MAX_BYTES && !(size&(size-1U))) {
            uint64_t virt=PCI_MMIO_BASE+(uint64_t)index*DEVICE_VA_STRIDE+
                          (uint64_t)i*BAR_MAX_BYTES;
            bool ok=true;
            for(uint64_t pg=0;pg<size;pg+=4096U)
                if(!nas_arch_x86_map_device_page(address+pg,virt+pg)){
                    ok=false;break;
                }
            if(ok) {
                res->bars[i].cpu=(volatile uint8_t *)(uintptr_t)virt;
                res->bars[i].size=size;
                mapped=true;
            }
        }
        if(type==2U)i++;
    }
    cfg_write(bus,slot,fun,4,mapped?old_cmd|6U:old_cmd);
    return mapped;
}
int nas_platform_virtio_blk_probe(void) {
    if(scanned)return (int)block_count();
    int rc=0;
#ifdef NAS_ENABLE_VIRTIO_BLK
    rc=nas_virtio_blk_driver_register();
    if(rc<0)return rc;
#endif
#ifdef NAS_ENABLE_VIRTIO_NET
    rc=nas_virtio_net_driver_register();
    if(rc<0)return rc;
#endif
#ifdef NAS_ENABLE_VIRTIO_INPUT
    rc=nas_virtio_input_driver_register();
    if(rc<0)return rc;
#endif
    unsigned count=0;
    for(unsigned slot=0;slot<32U;slot++) {
        uint32_t first=cfg_read(0,(uint8_t)slot,0,0);
        if((first&0xffffU)==0xffffU)continue;
        uint32_t header=cfg_read(0,(uint8_t)slot,0,0x0c);
        unsigned funcs=(header&(1U<<23))?8U:1U;
        for(unsigned function=0;function<funcs;function++) {
            uint32_t id=cfg_read(0,(uint8_t)slot,(uint8_t)function,0);
            /* Modern VirtIO PCI ID = 0x1040 + device class. The bus
             * publishes each supported class; drivers own protocols and
             * lifetime, including input and its independent event queues. */
            if((id&0xffffU)!=0x1af4U)continue;
            uint32_t device_id=id>>16;
            if(device_id!=0x1040U+VIRTIO_ID_NET &&
               device_id!=0x1040U+VIRTIO_ID_BLOCK &&
               device_id!=0x1040U+VIRTIO_ID_INPUT)continue;
            if(count>=VIRTIO_DEVICES)return -ENOSPC;
            load_config(0,(uint8_t)slot,(uint8_t)function,
                        cfg_snapshot[count]);
            device_pirq[count]=q35_bus0_pirq((uint8_t)slot,
                                            cfg_snapshot[count][0x3dU]);
            resources[count].config=cfg_snapshot[count];
            if(!map_bars(0,(uint8_t)slot,(uint8_t)function,count,
                         &resources[count])) {
                nas_print("[VIRTIO PCI FAIL] stage=bar-map slot=");
                nas_dec(slot);nas_print(" function=");nas_dec(function);
                nas_print("\n");return -ENODEV;
            }
            rc=nas_virtio_pci_transport_init(&transports[count],&pci[count],
                                             &resources[count],
                                             &nas_dma_qemu_coherent);
            if(rc) {
                nas_print("[VIRTIO PCI FAIL] stage=caps error=");
                nas_dec((uint64_t)(-rc));nas_print(" bar-sizes=");
                for(unsigned b=0;b<BAR_COUNT;b++){
                    nas_dec(resources[count].bars[b].size);nas_print(",");
                }
                nas_print("\n");return rc;
            }
            devices[count]=(struct nas_bus_device){
                .bus=NAS_BUS_PCI,.device_id=device_id-0x1040U,
                .vendor_id=id&0xffffU,
                .resource={.kind=NAS_BUS_RESOURCE_PCI,
                           .view.pci={.config=cfg_snapshot[count],
                                      .config_bytes=NAS_BUS_PCI_CONFIG_BYTES,
                                      .bus=0,.slot=(uint8_t)slot,
                                      .function=(uint8_t)function}},
                .virtio=&transports[count]
            };
            for(unsigned bar=0;bar<BAR_COUNT;bar++)
                if(resources[count].bars[bar].cpu)
                    devices[count].resource.view.pci.mapped_bar_mask |=
                        (uint8_t)(1U<<bar);
            count++;
        }
    }
    /* Enumerate every transport before enabling a shared legacy IRQ.
     * The real IRQ-driven Input and Net classes select the ICH9 PIRQ
     * links; an otherwise polled device on a shared PIC line still needs
     * its source status ACKed to release that level-triggered line. */
    uint8_t active_pirqs=0;
    for(unsigned i=0;i<count;i++) {
        bool needs_irq=false;
#ifdef NAS_ENABLE_VIRTIO_INPUT
        if(devices[i].device_id==VIRTIO_ID_INPUT)needs_irq=true;
#endif
#ifdef NAS_ENABLE_VIRTIO_NET
        if(devices[i].device_id==VIRTIO_ID_NET)needs_irq=true;
#endif
        if(!needs_irq)continue;
        if(device_pirq[i]==PIRQ_UNKNOWN) {
            nas_print("[VIRTIO PCI FAIL] stage=q35-device-pirq\n");
            return -EOPNOTSUPP;
        }
        active_pirqs|=(uint8_t)(1U<<device_pirq[i]);
    }
    rc=q35_route_device_pirqs(active_pirqs);
    if(rc) {
        nas_print("[VIRTIO PCI FAIL] stage=q35-ich9-route\n");
        return rc;
    }
    for(unsigned i=0;i<count;i++) {
        bool shared_line=device_pirq[i]!=PIRQ_UNKNOWN &&
            (active_pirqs&(1U<<device_pirq[i]));
        if(shared_line) {
            rc=q35_select_pci_intx(devices[i].resource.view.pci.slot,
                                   devices[i].resource.view.pci.function,
                                   cfg_snapshot[i]);
            if(rc)return rc;
            devices[i].resource.irq=(struct nas_irq_route){
                .domain=NAS_IRQ_DOMAIN_X86_PIC,.hwirq=X86_PIC_DEVICE_IRQ
            };
        }
        rc=nas_bus_device_register(&devices[i]);
        if(rc) {
            nas_print("[VIRTIO PCI FAIL] stage=driver-bind error=");
            nas_dec((uint64_t)(-rc));nas_print("\n");return rc;
        }
        bool class_owns_irq=false;
#ifdef NAS_ENABLE_VIRTIO_INPUT
        if(devices[i].device_id==VIRTIO_ID_INPUT)class_owns_irq=true;
#endif
#ifdef NAS_ENABLE_VIRTIO_NET
        if(devices[i].device_id==VIRTIO_ID_NET)class_owns_irq=true;
#endif
        if(shared_line && !class_owns_irq) {
            /* A class that has bound its own IRQ action must never have
             * its read-to-clear VirtIO ISR consumed a second time here.
             * Polled devices sharing IRQ11 still must ACK their source. */
            rc=nas_irq_bind(&shared_irq_acks[i],devices[i].resource.irq,
                            nas_virtio_irq_ack_source,&transports[i],
                            q35_shared_irq_ack_only,NULL);
            if(rc)return rc;
        }
    }
    nas_print("[VIRTIO-BLK] real modern PCI devices=");
    nas_dec(block_count());nas_print("\n");
    nas_print("[VIRTIO-NET] real modern PCI devices=");
    nas_dec(net_count());nas_print("\n");
#ifdef NAS_ENABLE_VIRTIO_INPUT
    nas_print("[VIRTIO-INPUT] real modern PCI devices=");
    nas_dec(nas_virtio_input_count());nas_print("\n");
#endif
    scanned=true;
    return (int)block_count();
}

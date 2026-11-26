#include "../kernel/platform/firmware_fdt.h"
#include <libfdt.h>
#include <assert.h>
#include <stdint.h>
#include <string.h>

static _Alignas(8) unsigned char blob[4096];
#define OK(call) assert((call) == 0)
static void prop(const char *name, uint32_t value) {
    OK(fdt_property_u32(blob, name, value));
}
static void regs(uint32_t a, uint32_t s, uint32_t b, uint32_t t, bool two) {
    fdt32_t r[] = {cpu_to_fdt32(a),cpu_to_fdt32(s),
                   cpu_to_fdt32(b),cpu_to_fdt32(t)};
    OK(fdt_property(blob,"reg",r,two ? sizeof(r) : 8));
}
static void virtio(const char *name, unsigned address, unsigned id) {
    OK(fdt_begin_node(blob,name));
    OK(fdt_property_string(blob,"compatible","virtio,mmio"));
    regs(address,0x200,0,0,false);
    fdt32_t intr[]={cpu_to_fdt32(0),cpu_to_fdt32(id),cpu_to_fdt32(1)};
    OK(fdt_property(blob,"interrupts",intr,sizeof(intr)));
    OK(fdt_end_node(blob));
}
static void rv_test(void) {
    OK(fdt_create(blob,sizeof(blob)));
    OK(fdt_finish_reservemap(blob));
    OK(fdt_begin_node(blob,""));
    prop("#address-cells",1); prop("#size-cells",1);
    OK(fdt_begin_node(blob,"cpus"));
    prop("#address-cells",1); prop("#size-cells",0);
    prop("timebase-frequency",12345678);
    OK(fdt_begin_node(blob,"cpu@3"));
    prop("reg",3);
    OK(fdt_begin_node(blob,"interrupt-controller"));
    prop("phandle",2); prop("#interrupt-cells",1);
    OK(fdt_end_node(blob)); OK(fdt_end_node(blob)); OK(fdt_end_node(blob));
    OK(fdt_begin_node(blob,"soc"));
    prop("#address-cells",1); prop("#size-cells",1);
    OK(fdt_property(blob,"ranges",NULL,0));
    OK(fdt_begin_node(blob,"plic@d000000"));
    OK(fdt_property_string(blob,"compatible","riscv,plic0"));
    prop("phandle",3); prop("#interrupt-cells",1);
    regs(0x0d000000,0x400000,0,0,false);
    fdt32_t contexts[]={cpu_to_fdt32(2),cpu_to_fdt32(11),
                         cpu_to_fdt32(2),cpu_to_fdt32(9)};
    OK(fdt_property(blob,"interrupts-extended",contexts,sizeof(contexts)));
    OK(fdt_end_node(blob));
    OK(fdt_begin_node(blob,"virtio@11001000"));
    OK(fdt_property_string(blob,"compatible","virtio,mmio"));
    regs(0x11001000,0x1000,0,0,false);
    prop("interrupt-parent",3); prop("interrupts",7);
    OK(fdt_end_node(blob)); OK(fdt_end_node(blob)); OK(fdt_end_node(blob));
    OK(fdt_finish(blob));
    assert(nas_fdt_discover(blob,sizeof(blob),true,3));
    assert(nas_fdt_hw.timebase_hz==12345678 && nas_fdt_hw.plic==0x0d000000);
    assert(nas_fdt_hw.plic_enable==0x0d002080 &&
           nas_fdt_hw.plic_context==0x0d201000);
    assert(nas_fdt_hw.virtio_base==0x11001000 &&
           nas_fdt_hw.virtio_count==1 && nas_fdt_hw.virtio_irq_first==7);
    assert(!nas_fdt_discover(blob,sizeof(blob),true,0));
}
int main(void) {
    OK(fdt_create(blob,sizeof(blob)));
    OK(fdt_finish_reservemap(blob));
    OK(fdt_begin_node(blob,""));
    prop("#address-cells",1); prop("#size-cells",1); prop("interrupt-parent",1);
    OK(fdt_begin_node(blob,"gic@9000000"));
    OK(fdt_property_string(blob,"compatible","arm,cortex-a15-gic"));
    prop("phandle",1); prop("#interrupt-cells",3);
    regs(0x09000000,0x1000,0x09010000,0x1000,true);
    OK(fdt_end_node(blob));
    OK(fdt_begin_node(blob,"timer"));
    OK(fdt_property_string(blob,"compatible","arm,armv8-timer"));
    fdt32_t timer[]={cpu_to_fdt32(1),cpu_to_fdt32(13),cpu_to_fdt32(4),
                     cpu_to_fdt32(1),cpu_to_fdt32(14),cpu_to_fdt32(4),
                     cpu_to_fdt32(1),cpu_to_fdt32(12),cpu_to_fdt32(4)};
    OK(fdt_property(blob,"interrupts",timer,sizeof(timer)));
    OK(fdt_end_node(blob));
    virtio("virtio@b000200",0x0b000200,33);
    virtio("virtio@b000000",0x0b000000,32);
    OK(fdt_end_node(blob));
    OK(fdt_finish(blob));
    assert(nas_fdt_discover(blob,sizeof(blob),false,0));
    assert(nas_fdt_hw.gicd==0x09000000 && nas_fdt_hw.gicc==0x09010000);
    assert(nas_fdt_hw.timer_irq==28 && nas_fdt_hw.virtio_base==0x0b000000);
    assert(nas_fdt_hw.virtio_count==2 && nas_fdt_hw.virtio_irq_first==64);
    assert(nas_fdt_hw.virtio_stride==0x200 && nas_fdt_hw.virtio_edge_mask==3);
    assert(!nas_fdt_discover(blob,16,false,0));
    assert(!nas_fdt_hw.gicd && !nas_fdt_hw.virtio_count);
    assert(!nas_fdt_discover(blob,sizeof(blob),true,0));
    rv_test();
    return 0;
}

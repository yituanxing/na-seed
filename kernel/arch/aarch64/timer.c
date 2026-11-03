#include <stdint.h>
#include "../../task.h"
#include "../../task/sched.h"
#include "../../panic.h"
#include "../../errno.h"
#include "../../drivers/core/irq.h"
#include "../../drivers/serial.h"
#include "../arch.h"
#include "context.h"

/* QEMU 'virt' GICv2: distributor 0x08000000, CPU interface 0x08010000.
 * ARM Generic Virtual Timer is EL1-visible CNTV_*, signaled on PPI 27.
 * Register offsets are the GICv2 architectural MMIO interface, not an
 * invented NaSeed-specific interrupt protocol.
 */
#define GICD_PHYS UINT64_C(0x08000000)
#define GICC_PHYS UINT64_C(0x08010000)
#define GICD_VIRT UINT64_C(0xffffffffe0001000)
#define GICC_VIRT UINT64_C(0xffffffffe0002000)
#define ARM_VTIMER_PPI 27U
/* QEMU ARM virt exposes 32 MMIO transports on SPI 16..47.  GIC IAR
 * returns absolute INTIDs 48..79, not the SPI numbers from the FDT. */
#define ARM_VIRTIO_IRQ_FIRST 48U
#define ARM_VIRTIO_IRQ_COUNT 32U
#define GICD_CTLR       0x000U
#define GICD_TYPER      0x004U
#define GICD_IGROUPR    0x080U
#define GICD_ISENABLER  0x100U
#define GICD_ICENABLER  0x180U
#define GICD_ISPENDR    0x200U
#define GICD_ICPENDR    0x280U
#define GICD_IPRIORITYR 0x400U
#define GICD_ITARGETSR  0x800U
#define GICD_ICFGR      0xc00U
#define GICC_PMR        0x004U
#define GICC_IAR        0x00cU
#define GICC_EOIR       0x010U
#define GICC_HPPIR      0x018U
extern bool nas_arm_map_io_page(uint64_t phys, uint64_t virt);
static volatile uint32_t *dist;
static volatile uint32_t *iface;
static uint64_t period;
static uint32_t armed_virtio_irqs;
static bool virtio_irq_id(unsigned id) {
    return id>=ARM_VIRTIO_IRQ_FIRST &&
           id<ARM_VIRTIO_IRQ_FIRST+ARM_VIRTIO_IRQ_COUNT;
}
/* ARM GIC routing is a platform operation, not a VirtIO class-driver
 * operation.  Own just the QEMU virt SPI range for the currently bound
 * devices, leaving PPI27 and every unrelated interrupt untouched. */
int nas_arch_irq_route_enable(unsigned id) {
    if(!dist || !iface || !virtio_irq_id(id))return -EINVAL;
    if(id>=32U*((dist[GICD_TYPER/4]&31U)+1U))return -ENODEV;
    unsigned bit=id-ARM_VIRTIO_IRQ_FIRST;
    if(armed_virtio_irqs&(1U<<bit))return 0;
    /* The QEMU virt FDT marks these SPIs edge-rising.  Target the one
     * boot vCPU, with lower priority than the existing PPI27 timer. */
    unsigned cfg_index=(GICD_ICFGR/4)+(id/16U);
    unsigned cfg_shift=2U*(id%16U);
    uint32_t cfg=dist[cfg_index];
    cfg=(cfg&~(3U<<cfg_shift))|(2U<<cfg_shift);
    dist[cfg_index]=cfg;
    volatile uint8_t *bytes=(volatile uint8_t *)(void *)dist;
    bytes[GICD_IPRIORITYR+id]=0xa0U;
    bytes[GICD_ITARGETSR+id]=1U;
    armed_virtio_irqs|=1U<<bit;
    __asm__ volatile("dsb sy" : : : "memory");
    dist[GICD_ISENABLER/4+id/32U]=1U<<(id%32U);
    __asm__ volatile("dsb sy; isb" : : : "memory");
    /* First-batch hardware evidence: record the actual SPI register state
     * instead of inferring success merely from nas_irq_bind() returning 0.
     * HPPIR is observational; reading IAR here would wrongly claim IRQ. */
    nas_print("[M175 ARM GIC ROUTE] id=");nas_dec(id);
    nas_print(" enabled=");
    nas_dec((dist[GICD_ISENABLER/4+id/32U]>>(id%32U))&1U);
    nas_print(" pending=");
    nas_dec((dist[GICD_ISPENDR/4+id/32U]>>(id%32U))&1U);
    nas_print(" group=");
    nas_dec((dist[GICD_IGROUPR/4+id/32U]>>(id%32U))&1U);
    nas_print(" edge=");
    nas_dec((dist[GICD_ICFGR/4+id/16U]>>(2U*(id%16U)))&3U);
    nas_print(" target=");nas_dec(bytes[GICD_ITARGETSR+id]);
    nas_print(" dist_ctlr=");nas_dec(dist[GICD_CTLR/4]);
    nas_print(" cpu_ctlr=");nas_dec(iface[0]);
    nas_print(" pmr=");nas_dec(iface[GICC_PMR/4]);
    nas_print(" hppir=");nas_dec(iface[GICC_HPPIR/4]);
    nas_print("\n");
    return 0;
}
void nas_arch_irq_route_disable(unsigned id) {
    if(!dist || !virtio_irq_id(id))return;
    dist[GICD_ICENABLER/4+id/32U]=1U<<(id%32U);
    __asm__ volatile("dsb sy" : : : "memory");
    armed_virtio_irqs&=~(1U<<(id-ARM_VIRTIO_IRQ_FIRST));
    /* A hot-unbound device must not inherit a stale edge on rebind. */
    dist[GICD_ICPENDR/4+id/32U]=1U<<(id%32U);
    __asm__ volatile("dsb sy; isb" : : : "memory");
}
/* CNTVCT keeps increasing while IRQ delivery is masked, including while
 * the last process is asleep in the syscall-boundary idle dispatcher. */
uint64_t nas_arch_monotonic_ns(void) {
    uint64_t ticks, hz;
    __asm__ volatile("mrs %0,cntvct_el0" : "=r"(ticks));
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(hz));
    if(!hz) nas_fail("ARM64 monotonic counter has no frequency");
    return (ticks / hz) * 1000000000ULL +
           (ticks % hz) * 1000000000ULL / hz;
}
uint64_t nas_arch_clock_resolution_ns(void) {
    uint64_t hz;
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(hz));
    return hz ? (1000000000ULL + hz - 1) / hz : 0;
}
static void rearm_timer(void) {
    uint64_t now;
    __asm__ volatile("mrs %0,cntvct_el0" : "=r"(now));
    __asm__ volatile("msr cntv_cval_el0,%0\n isb"
                     : : "r"(now+period) : "memory");
}
void nas_arm_timer_start(void) {
    if (!nas_arm_map_io_page(GICD_PHYS,GICD_VIRT) ||
        !nas_arm_map_io_page(GICC_PHYS,GICC_VIRT))
        nas_fail("ARM64 GICv2 device mapping failed");
    dist=(volatile uint32_t *)(uintptr_t)GICD_VIRT;
    iface=(volatile uint32_t *)(uintptr_t)GICC_VIRT;
    uint64_t frequency;
    __asm__ volatile("mrs %0,cntfrq_el0" : "=r"(frequency));
    if(!frequency || frequency<100) nas_fail("ARM64 CNTFRQ invalid");
    period=frequency/100; /* exact hardware-counter rate, no guesswork */
    /* IRQ27: PPI, banked ENABLE[0] for the one QEMU vCPU. No SGIs or
     * arbitrary device IRQs are accidentally enabled by this driver. */
    volatile uint8_t *priority=(volatile uint8_t *)(void *)dist;
    priority[0x400+ARM_VTIMER_PPI]=0x80;
    dist[0x100/4] = 1U<<ARM_VTIMER_PPI;
    dist[GICD_CTLR/4] |= 1; /* group 0 distributor enable */
    iface[GICC_PMR/4]=0xff; /* priority mask */
    iface[0x00/4]=1;   /* group 0 CPU interface */
    rearm_timer();
    __asm__ volatile("msr cntv_ctl_el0,%0\n isb"
                     : : "r"(1ULL) : "memory");
    extern void nas_print(const char *);
    nas_print("[M28 ARM64] Generic Virtual Timer/GIC PPI27 active\n");
}
/* Handler runs with PSTATE.I=1; user context fully saved in trap.S.
 * IAR/EOIR must be paired even when the scheduler chooses another Task.
 */
/* EL1h IRQ from the sleeping scheduler is NOT an EL0 trap. The dedicated
 * vector keeps the kernel stack, acknowledges timer or device IRQs, and
 * returns via its original kernel ELR/SPSR without Task frame capture. */
void nas_arch_idle_wait(void) {
    __asm__ volatile("msr daifclr,#2\n\twfi\n\tmsr daifset,#2"
                     : : : "memory");
}
/* GICv2 IAR may report spurious INTID 1023 without acknowledging any
 * interrupt.  Never EOIR(1023).  Every other ID must either be the
 * existing timer or an explicitly armed device owned by the IRQ core. */
static uint32_t arm_irq_claim(void) {
    uint32_t iar=iface[GICC_IAR/4];
    unsigned id=iar&0x3ffU;
    if(id==1023U)return 0;
    if(id!=ARM_VTIMER_PPI &&
       (!virtio_irq_id(id) ||
        !(armed_virtio_irqs&(1U<<(id-ARM_VIRTIO_IRQ_FIRST))))) {
        extern void nas_print(const char *);
        extern void nas_hex(uint64_t);
        nas_print("[ARM64 GIC] unowned IAR=");nas_hex(iar);nas_print("\n");
        nas_fail("ARM64 unexpected GIC interrupt ID");
    }
    return iar;
}
static void arm_timer_complete_irq(uint32_t iar) {
    rearm_timer();
    iface[GICC_EOIR/4]=iar;
    __asm__ volatile("dsb sy" : : : "memory");
}
static void arm_device_complete_irq(uint32_t iar) {
    unsigned id=iar&0x3ffU;
    /* IRQ-core dispatch acknowledges VirtIO-MMIO InterruptStatus/ACK
     * before GIC EOI, so the level cannot immediately retrigger.  An
     * already-deasserted device line may have a zero ISR: this is an
     * ordinary unclaimed edge, not a fatal error or permission to poll. */
    bool claimed=nas_irq_dispatch(id);
    static unsigned observed;
    if(observed<16U) {
        observed++;
        nas_print("[M175 ARM GIC CLAIM] id=");nas_dec(id);
        nas_print(" claimed=");nas_dec(claimed);
        nas_print(" iar=");nas_hex(iar);
        nas_print("\n");
    }
    __asm__ volatile("dsb sy" : : : "memory");
    iface[GICC_EOIR/4]=iar;
    __asm__ volatile("dsb sy" : : : "memory");
}
void nas_arm_idle_irq_c(void) {
    uint32_t iar=arm_irq_claim();
    if(!iar)return;
    if((iar&0x3ffU)==ARM_VTIMER_PPI)arm_timer_complete_irq(iar);
    else arm_device_complete_irq(iar);
}
void nas_arm_timer_irq_c(struct nas_arm_trap_frame *frame) {
    uint32_t iar=arm_irq_claim();
    if(!iar)return;
    if((iar&0x3ffU)!=ARM_VTIMER_PPI) {
        /* No device-triggered timer tick, user-frame save, or scheduler
         * preemption.  The pending work is consumed on the next ordinary
         * dispatcher boundary; both IRQ paths remain IRQ-stack-only. */
        arm_device_complete_irq(iar);
        return;
    }
    struct nas_task *from=nas_current_task;
    if(!from || !nas_task_kernel_stack_ok(from))
        nas_fail("ARM64 timer invalid Task");
    nas_arch_save_user_frame(from,0);
    nas_arch_save_fpu(from);
    /* EOI before parking this IRQ C continuation, never afterwards. */
    arm_timer_complete_irq(iar);
    /* Native ERET resumes this exact interrupted Task; there is no
     * second frame-copy path after the dispatcher selects another Task. */
    nas_sched_preempt_select();
    (void)frame;
}

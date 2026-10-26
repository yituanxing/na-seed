#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cpu.h"
#include "../../task.h"
#include "../../task/ptrace.h"
#include "../../task/sched.h"
#include "../../task/signal.h"
#include "../../exception/exception.h"
#include "../../panic.h"
#include "irq.h"
#include "../arch.h"
void exception_report(const uint64_t *frame);
#include "mmu.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
extern void gdt_reload(const void *);
extern void syscall_entry(void);
extern void *exception_vectors[32];
extern char kernel_stack_top;
extern void nas_x86_timer_isr(void);
extern uint64_t nas_x86_kernel_stack_ptr;
void nas_arch_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static void wrmsr(uint32_t reg, uint64_t value) {
    __asm__ volatile ("wrmsr" : : "c"(reg), "a"((uint32_t)value), "d"((uint32_t)(value >> 32)));
}
static uint64_t rdmsr(uint32_t reg) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(reg));
    return ((uint64_t)hi << 32) | lo;
}

/* ABI-shaped TSS; all exceptions from user mode use the kernel stack. */
struct __attribute__((packed)) x86_tss {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2, reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3, iomap_base;
};
struct __attribute__((packed)) table_pointer { uint16_t limit; uint64_t base; };
struct __attribute__((packed)) idt_entry {
    uint16_t offset_low, selector;
    uint8_t ist, type;
    uint16_t offset_middle;
    uint32_t offset_high, reserved;
};
static struct x86_tss tss;
static uint64_t gdt[9] __attribute__((aligned(16)));
static struct idt_entry idt[256];

void nas_arch_cpu_user_fp_init(void) {
    /* Actual musl Bash emits SSE2 (e.g. movq XMM0) in static_init_tls.
     * Without CR4.OSFXSR the CPU raises #UD at its first XMM instruction.
     * M2 runs one userspace task; preserve FPU/SIMD context when adding
     * multitasking and kernel SIMD later.
     */
    uint64_t cr0, cr4;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr0 |= (1ULL << 1);                /* MP */
    cr0 &= ~((1ULL << 2) | (1ULL << 3)); /* clear EM/TS */
    cr4 |= (1ULL << 9) | (1ULL << 10);   /* OSFXSR + OSXMMEXCPT */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    __asm__ volatile ("mov %0, %%cr4" : : "r"(cr4) : "memory");
    __asm__ volatile ("fninit");
}
void nas_arch_x86_set_kernel_stack(uint64_t top) {
    /* syscall and hardware privilege transitions must select the same
     * process-owned stack; never update while changing the live RSP. */
    tss.rsp0 = top;
    nas_x86_kernel_stack_ptr = top;
}
void nas_arch_cpu_tables_init(void) {
    tss.rsp0 = (uint64_t)(uintptr_t)&kernel_stack_top;
    tss.iomap_base = sizeof(tss);
    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL; /* kernel code: 0x08 */
    gdt[2] = 0x00cf92000000ffffULL; /* kernel data: 0x10 */
    /* Preserve Linux x86-64 ELF CS=0x33 / SS=0x2b, not the
     * historical NaSeed CS=0x23 selector that GNU GDB correctly
     * classifies as 32-bit i386 compatibility mode. These must be
     * real hardware GDT descriptors, NOT fabricated ptrace register
     * values. Indices 3/4 are reserved for future compat segments. */
    gdt[3] = 0;
    gdt[4] = 0;
    gdt[5] = 0x00cff2000000ffffULL; /* user data: 0x2b */
    gdt[6] = 0x00affa000000ffffULL; /* user64 code: 0x33 */
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[7] = (limit & 0xffff) | ((base & 0xffffff) << 16)
        | (0x89ULL << 40) | (((limit >> 16) & 15) << 48)
        | (((base >> 24) & 0xff) << 56);
    gdt[8] = base >> 32;
    struct table_pointer gdtr = { sizeof(gdt) - 1, (uint64_t)(uintptr_t)gdt };
    gdt_reload(&gdtr);
    uint16_t tr = 0x38;
    __asm__ volatile ("ltr %0" : : "r"(tr));
    for (unsigned i = 0; i < 32; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)exception_vectors[i];
        idt[i].offset_low = addr & 0xffff;
        idt[i].offset_middle = (addr >> 16) & 0xffff;
        idt[i].offset_high = addr >> 32;
        idt[i].selector = 0x08;
        idt[i].ist = 0;
        /* INT3 executes in CPL3 and uses vector 3 via the IDT. The
         * Linux debug ABI requires DPL=3 on the *real* #BP gate;
         * DPL=0 raises #GP(0x1a) instead of a ptrace SIGTRAP.
         * Keep every other exception gate privileged (DPL=0). */
        idt[i].type = i==3 ? 0xee : 0x8e;
        idt[i].reserved = 0;
    }

    /* 0x20 is the remapped legacy PIT IRQ0; do not confuse it with an
     * exception vector. Never put an IRQ through exception_report(). */
    uint64_t irq_addr=(uint64_t)(uintptr_t)nas_x86_timer_isr;
    idt[0x20].offset_low=irq_addr & 0xffffU;
    idt[0x20].offset_middle=(irq_addr>>16)&0xffffU;
    idt[0x20].offset_high=irq_addr>>32;
    idt[0x20].selector=0x08;
    idt[0x20].ist=0;
    idt[0x20].type=0x8e;
    idt[0x20].reserved=0;
    struct table_pointer idtr = { sizeof(idt) - 1, (uint64_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
}
/* Saved 15 GPRs + exception vector/error + original hardware IRET
 * frame. Exception and syscall share the already-verified selected Task
 * return path. Only user #BP / user #PF with an actual ptrace owner can
 * be stopped; never mask a kernel exception as a user SIGTRAP. */
void nas_x86_traced_exception_c(uint64_t *frame) {
    unsigned vector=(unsigned)frame[15];
    unsigned signo=(vector==1||vector==3)?NAS_SIGTRAP:11U;
    uint64_t user_rip=frame[17],user_flags=frame[19],user_sp=frame[20];
    uint64_t fault_addr=0;
    if(vector==14U)__asm__ volatile("mov %%cr2,%0":"=r"(fault_addr));
    struct nas_exception exception=nas_exception_x86(vector,frame[16],
                                          fault_addr,user_rip,frame[18]);
    struct nas_task *tracee=nas_current_task;
    struct nas_exception_outcome outcome=
        exception.kind==NAS_EXC_MEMORY && tracee ?
        nas_exception_dispatch(&exception,tracee->mm) :
        (struct nas_exception_outcome){0};
    /* Page-fault diagnostics are a permanent fail-closed user MM signal
     * breadcrumb, never a synthetic fault fixup. A native C-stack switch
     * must not conceal the actual user RIP, fault VA or PTE fault flags. */
    if(vector==14U && outcome.action==NAS_EXC_SIGNAL && tracee) {
        static unsigned user_fault_reports;
        if(user_fault_reports++<16U) {
            nas_print("[USERPF] pid=");nas_dec(tracee->pid);
            nas_print(" rip=");nas_hex(user_rip);
            nas_print(" va=");nas_hex(fault_addr);
            nas_print(" error=");nas_hex(frame[16]);
            nas_print(" rsp=");nas_hex(user_sp);
            nas_print("\n");
        }
    }
    if(outcome.action==NAS_EXC_RETRY) {
        /* Hardware #PF has vector/error in front of its IRET image.
         * A successful MM fixup retries the EXACT faulting user PC. */
        for(unsigned i=0;i<5;i++)frame[15+i]=frame[17+i];
        nas_signal_user_return(frame);
        return;
    }
    if((frame[18]&3U)==3U && tracee &&
       tracee->state==NAS_TASK_RUNNING &&
       (vector==1 || vector==3 ||
        (vector==14 && outcome.action==NAS_EXC_SIGNAL))) {
        for(unsigned i=0;i<15;i++)tracee->arch.resume_regs[i]=frame[i];
        tracee->arch.resume_user_rip=user_rip;
        /* A genuine #DB after one CPU instruction ends that step.
         * It is not a software breakpoint (#BP), and the user TF must
         * clear before the debugger next resumes with PTRACE_CONT. */
        if(vector==1 && tracee->ptrace_single_step) {
            tracee->ptrace_single_step=false;
            user_flags&=~(1ULL<<8);
        }
        tracee->arch.resume_user_rflags=user_flags;
        tracee->arch.resume_user_rsp=user_sp;
        nas_arch_save_fpu(tracee);
        uint64_t signal_addr=user_rip;
        int32_t si_code=vector==1?2:0x80; /* #DB TRACE, #BP SI_KERNEL */
        if(vector==3)signal_addr=user_rip-1; /* INT3 advances RIP */
        if(vector==14) {
            signal_addr=fault_addr;
            si_code=outcome.si_code; /* VMA-first MAPERR/ACCERR */
        }
        bool traced=nas_ptrace_user_exception(tracee,signo,si_code,user_rip,
                                               user_sp,signal_addr);
        if(traced || (vector==14 && outcome.action==NAS_EXC_SIGNAL)) {
            /* For an untraced user #PF, default SIGSEGV terminates only
             * the offending process group, not the entire kernel. */
            if(!traced)nas_sched_thread_group_exit(0,outcome.signo);
            uint64_t resumed=nas_task_schedule(frame);
            /* A native traced exception returns to the SAME Task C frame.
             * Restore debugger-edited saved GPRs before constructing IRET;
             * unlike the old copy-frame scheduler this is not a new Task. */
            if(traced) {
                nas_arch_load_user_frame(nas_current_task,frame);
                resumed=nas_arch_saved_return(nas_current_task);
            }
            frame[0]=resumed;
            /* C scheduler selected a real Task/MMU and wrote the live
             * 15-GPR snapshot. Convert the exception's two metadata
             * words into the new Task's authentic five-word IRET frame. */
            const struct nas_task *selected=nas_current_task;
            frame[15]=selected->arch.resume_user_rip;
            frame[16]=0x33U;
            frame[17]=selected->arch.resume_user_rflags;
            frame[18]=selected->arch.resume_user_rsp;
            frame[19]=0x2bU;
            nas_signal_user_return(frame);
            return;
        }
    }
    /* Fatal, unsupported or kernel fault: leave the precise original
     * hardware RIP, CS, CR2 and error visible to the existing reporter. */
    exception_report(frame+15);
    nas_fail("unhandled x86 exception (not traced user debug event)");
}
void exception_report(const uint64_t *frame) {
    uint64_t cr2;
    __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));
    nas_print("[FAULT] vector="); nas_dec(frame[0]);
    nas_print(" err="); nas_hex(frame[1]);
    nas_print(" rip="); nas_hex(frame[2]);
    nas_print(" cs="); nas_hex(frame[3]);
    nas_print(" cr2="); nas_hex(cr2);
    nas_print("\n");
}

void nas_arch_syscall_enable(void) {
    uint64_t efer = rdmsr(0xc0000080);
    wrmsr(0xc0000080, efer | 1); /* EFER.SCE */
    wrmsr(0xc0000081, (0x20ULL << 48) | (0x08ULL << 32)); /* STAR */
    wrmsr(0xc0000082, (uint64_t)(uintptr_t)syscall_entry); /* LSTAR */
    wrmsr(0xc0000084, (1ULL << 9) | (1ULL << 10)); /* FMASK IF|DF */
}


void nas_arch_halt(void) { for (;;) __asm__ volatile ("cli; hlt"); }
uint64_t nas_arch_current_page_root(void) {
    uint64_t cr3; __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3)); return cr3;
}
void nas_arch_switch_page_root(uint64_t root) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(root) : "memory");
}
bool nas_arch_la57_enabled(void) {
    uint64_t cr4; __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    return (cr4 & (1ULL << 12)) != 0;
}
void nas_arch_invalidate_page(uint64_t va) {
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}
uint64_t nas_arch_cycle_counter(void) {
    uint32_t lo, hi; __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return (uint64_t)lo | ((uint64_t)hi << 32);
}
bool nas_arch_nx_enabled(void) { return (rdmsr(0xc0000080) & (1ULL << 11)) != 0; }
void nas_arch_set_user_tls(uint64_t address) { wrmsr(0xc0000100, address); }

/* Limine base revision 6 deliberately masks BSP LAPIC LINT0 ExtINT.
 * A PIT+PIC alone can never reach QEMU q35's CPU until its xAPIC
 * virtual-wire receiver is explicitly unmasked. This is NOT a local APIC
 * timer, only a correct single-CPU PIC -> LINT0 ExtINT route.
 * The LAPIC physical page is mapped uncached by the MMU provider.
 * Refuse x2APIC systems instead of performing invalid MMIO accesses.
 */
#define NAS_X86_LAPIC_VIRT UINT64_C(0xfffffe8000000000)
static volatile uint32_t *const lapic =
    (volatile uint32_t *)(uintptr_t)NAS_X86_LAPIC_VIRT;
static void nas_x86_enable_virtual_wire(void) {
    uint64_t apic_base=rdmsr(0x1b); /* IA32_APIC_BASE */
    if (!(apic_base&(1ULL<<11)) || (apic_base&(1ULL<<10)) ||
        !(apic_base&(1ULL<<8))) {
        nas_print("[M28] unsupported x86 APIC mode=");
        nas_hex(apic_base); nas_print("\n");
        nas_arch_halt();
    }
    uint64_t phys=apic_base&UINT64_C(0x000ffffffffff000);
    if (!nas_arch_x86_map_lapic_page(phys)) {
        nas_print("[M28] cannot map xAPIC MMIO page\n");
        nas_arch_halt();
    }
    /* Limine leaves LAPIC software-enabled with vector 0xff but
     * LINT0 masked; set only the state we own. TPR=0 allows IRQ0.
     * Intel MP Spec: virtual-wire ExtINT uses PIC EOI, not LAPIC EOI. */
    lapic[0x80/4]=0;              /* Task Priority Register */
    uint32_t svr=lapic[0xf0/4];
    lapic[0xf0/4]=svr|0x100U;     /* software enable */
    lapic[0x350/4]=0x700U;        /* LVT LINT0: ExtINT, edge, unmasked */
    __asm__ volatile("mfence" : : : "memory");
    if ((lapic[0x350/4]&0x10700U)!=0x700U) {
        nas_print("[M28] LINT0 ExtINT did not become enabled\n");
        nas_arch_halt();
    }
    nas_print("[M28] LAPIC BSP LINT0 ExtINT virtual-wire enabled\n");
}
/* PIT oscillator/8259 PIC: IRQ0 at vector 0x20. User IRET restores IF,
 * kernel-mode syscall/interrupt gates always run with interrupts masked.
 * Single CPU only; replace with real APIC timer/IOAPIC for portability. */
static void nas_x86_enable_legacy_pit(void) {
    nas_arch_outb(0x20,0x11);
    nas_arch_outb(0xa0,0x11);
    nas_arch_outb(0x21,0x20);
    nas_arch_outb(0xa1,0x28);
    nas_arch_outb(0x21,0x04);
    nas_arch_outb(0xa1,0x02);
    nas_arch_outb(0x21,0x01);
    nas_arch_outb(0xa1,0x01);
    nas_arch_outb(0x21,0xfe); /* IRQ0 only */
    nas_arch_outb(0xa1,0xff);
    const uint16_t divisor=11932U; /* PIT 1,193,182Hz => ~100Hz */
    nas_arch_outb(0x43,0x36); /* channel0, lo/hi, mode3 */
    nas_arch_outb(0x40,(uint8_t)divisor);
    nas_arch_outb(0x40,(uint8_t)(divisor>>8));
}
/* Shared init contract: preserve all established x86 hardware operations. */
void nas_arch_prepare_user_cpu(void) {
    /* Firmware can leave IF set. Prevent IRQ0 from entering kernel C
     * before the first explicit user IRET enables userspace interrupts. */
    __asm__ volatile("cli" : : : "memory");
    nas_arch_cpu_tables_init();
    nas_arch_cpu_user_fp_init();
    if (!nas_task_kernel_stack_ok(nas_current_task)) nas_arch_halt();
    nas_arch_x86_set_kernel_stack(nas_current_task->kernel_stack_top);
    nas_x86_enable_virtual_wire();
    nas_x86_enable_legacy_pit();
    extern void nas_x86_clock_init(void);
    nas_x86_clock_init();
}
void nas_arch_enable_user_traps(void) {
    nas_arch_syscall_enable();
}

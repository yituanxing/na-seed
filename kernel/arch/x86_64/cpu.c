#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cpu.h"
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
extern void gdt_reload(const void *);
extern void syscall_entry(void);
extern void *exception_vectors[32];
extern char kernel_stack_top;
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
static uint64_t gdt[7] __attribute__((aligned(16)));
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
void nas_arch_cpu_tables_init(void) {
    tss.rsp0 = (uint64_t)(uintptr_t)&kernel_stack_top;
    tss.iomap_base = sizeof(tss);
    gdt[0] = 0;
    gdt[1] = 0x00af9a000000ffffULL; /* kernel code: 0x08 */
    gdt[2] = 0x00cf92000000ffffULL; /* kernel data: 0x10 */
    gdt[3] = 0x00cff2000000ffffULL; /* user data: 0x1b */
    gdt[4] = 0x00affa000000ffffULL; /* user code: 0x23 */
    uint64_t base = (uint64_t)(uintptr_t)&tss;
    uint64_t limit = sizeof(tss) - 1;
    gdt[5] = (limit & 0xffff) | ((base & 0xffffff) << 16)
        | (0x89ULL << 40) | (((limit >> 16) & 15) << 48)
        | (((base >> 24) & 0xff) << 56);
    gdt[6] = base >> 32;
    struct table_pointer gdtr = { sizeof(gdt) - 1, (uint64_t)(uintptr_t)gdt };
    gdt_reload(&gdtr);
    uint16_t tr = 0x28;
    __asm__ volatile ("ltr %0" : : "r"(tr));
    for (unsigned i = 0; i < 32; i++) {
        uint64_t addr = (uint64_t)(uintptr_t)exception_vectors[i];
        idt[i].offset_low = addr & 0xffff;
        idt[i].offset_middle = (addr >> 16) & 0xffff;
        idt[i].offset_high = addr >> 32;
        idt[i].selector = 0x08;
        idt[i].ist = 0;
        idt[i].type = 0x8e;
        idt[i].reserved = 0;
    }
    struct table_pointer idtr = { sizeof(idt) - 1, (uint64_t)(uintptr_t)idt };
    __asm__ volatile ("lidt %0" : : "m"(idtr));
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
    wrmsr(0xc0000081, (0x10ULL << 48) | (0x08ULL << 32)); /* STAR */
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

/* Shared init contract: preserve all established x86 hardware operations. */
void nas_arch_prepare_user_cpu(void) {
    nas_arch_cpu_tables_init();
    nas_arch_cpu_user_fp_init();
}
void nas_arch_enable_user_traps(void) {
    nas_arch_syscall_enable();
}

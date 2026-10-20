
#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Single-core M1: run a REAL static ELF from a Limine module in ring 3.
 * The kernel's supervisor entries and HHDM remain mapped under Limine's CR3.
 * Separate process address spaces, scheduler and preemption come later.
 * Interrupts are disabled on syscall entry; TSS handles ring3 faults.
 */
extern uint64_t nas_frame_alloc(void);
extern uint64_t nas_hhdm_offset(void);
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
extern const struct limine_file *nas_user_module(void);
extern void gdt_reload(const void *);
extern void enter_user(uint64_t entry, uint64_t stack_top);
extern void syscall_entry(void);
extern void *exception_vectors[32];
extern char kernel_stack_top;
extern uint64_t syscall_user_rsp;

#define PAGE 4096ULL
#define P_PRESENT 1ULL
#define P_RW 2ULL
#define P_USER 4ULL
#define PHYS_MASK 0x000ffffffffff000ULL
#define USER_LIMIT 0x0000800000000000ULL
#define USER_STACK_TOP 0x0000000070000000ULL
#define USER_STACK_PAGES 4

static void fail(const char *message) {
    nas_print("[FAIL] M1 ");
    nas_print(message);
    nas_print("\n");
    for (;;) __asm__ volatile ("cli; hlt");
}
static void outb(uint16_t port, uint8_t value) {
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

static void cpu_tables_init(void) {
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

/* User pages: read and amend existing Limine CR3 paging hierarchy. */
static uint64_t root_phys;
static uint64_t *phys_table(uint64_t physical) {
    return (uint64_t *)(uintptr_t)(nas_hhdm_offset() + physical);
}
static bool paging_init(void) {
    uint64_t cr4, cr3;
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    if (cr4 & (1ULL << 12)) return false; /* LA57 not part of M1. */
    root_phys = cr3 & PHYS_MASK;
    return root_phys != 0;
}
static uint64_t *user_leaf(uint64_t va, bool allocate_tables) {
    if (va < PAGE || va >= USER_LIMIT || (va & (PAGE - 1))) return 0;
    uint64_t table = root_phys;
    for (int level = 3; level >= 1; --level) {
        unsigned index = (va >> (12 + (unsigned)level * 9)) & 511;
        uint64_t *entries = phys_table(table);
        uint64_t e = entries[index];
        if (!(e & P_PRESENT)) {
            if (!allocate_tables) return 0;
            uint64_t pa = nas_frame_alloc();
            if (!pa) return 0;
            entries[index] = pa | P_PRESENT | P_RW | P_USER;
            e = entries[index];
        }
        if (!(e & P_USER) || (e & (1ULL << 7))) return 0;
        table = e & PHYS_MASK;
    }
    return &phys_table(table)[(va >> 12) & 511];
}
static bool user_map(uint64_t va, bool writable) {
    uint64_t *entry = user_leaf(va, true);
    if (!entry || (*entry & P_PRESENT)) return false;
    uint64_t pa = nas_frame_alloc();
    if (!pa) return false;
    *entry = pa | P_PRESENT | P_USER | (writable ? P_RW : 0);
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
    return true;
}
static uint8_t *user_byte(uint64_t va) {
    uint64_t *entry = user_leaf(va & ~(PAGE - 1), false);
    if (!entry || !(*entry & P_PRESENT)) return 0;
    return (uint8_t *)(uintptr_t)(nas_hhdm_offset() +
        (*entry & PHYS_MASK) + (va & (PAGE - 1)));
}
static bool user_copy_in(uint64_t va, const uint8_t *data, uint64_t length) {
    for (uint64_t i = 0; i < length; i++) {
        uint8_t *dst = user_byte(va + i);
        if (!dst) return false;
        *dst = data[i];
    }
    return true;
}

/* Parse the ELF64 program headers rather than executing embedded kernel code. */
struct __attribute__((packed)) elf_hdr {
    uint8_t ident[16];
    uint16_t type, machine;
    uint32_t version;
    uint64_t entry, phoff, shoff;
    uint32_t flags;
    uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
};
struct __attribute__((packed)) elf_phdr {
    uint32_t type, flags;
    uint64_t offset, vaddr, paddr, filesz, memsz, align;
};
static bool checked_range(uint64_t start, uint64_t size, uint64_t limit) {
    return start <= limit && size <= limit - start;
}
static bool elf_load(const struct limine_file *module, uint64_t *entry) {
    if (!module || !module->address || module->size < sizeof(struct elf_hdr))
        return false;
    const uint8_t *file = (const uint8_t *)module->address;
    const struct elf_hdr *e = (const struct elf_hdr *)file;
    if (e->ident[0] != 0x7f || e->ident[1] != 'E' ||
        e->ident[2] != 'L' || e->ident[3] != 'F' ||
        e->ident[4] != 2 || e->ident[5] != 1 ||
        e->type != 2 || e->machine != 62 ||
        e->phentsize != sizeof(struct elf_phdr) ||
        !e->phnum || e->phnum > 32 ||
        !checked_range(e->phoff, (uint64_t)e->phnum * e->phentsize, module->size))
        return false;

    bool executable_entry = false;
    const struct elf_phdr *ph = (const struct elf_phdr *)(file + e->phoff);
    for (unsigned i = 0; i < e->phnum; ++i) {
        const struct elf_phdr *p = &ph[i];
        if (p->type != 1) continue;
        if (!p->memsz || p->memsz > (1ULL << 20) ||
            p->filesz > p->memsz ||
            !checked_range(p->offset, p->filesz, module->size) ||
            !checked_range(p->vaddr, p->memsz, USER_LIMIT) ||
            p->vaddr < 0x10000 ||
            p->vaddr + p->memsz > USER_STACK_TOP - 0x100000)
            return false;
        if ((p->flags & 1) &&
            e->entry >= p->vaddr && e->entry - p->vaddr < p->memsz)
            executable_entry = true;
        uint64_t begin = p->vaddr & ~(PAGE - 1);
        uint64_t end = (p->vaddr + p->memsz + PAGE - 1) & ~(PAGE - 1);
        for (uint64_t va = begin; va < end; va += PAGE)
            if (!user_map(va, (p->flags & 2) != 0)) return false;
        if (!user_copy_in(p->vaddr, file + p->offset, p->filesz))
            return false;
    }
    if (!executable_entry) return false;
    *entry = e->entry;
    return true;
}

/* Strict single-program ABI diagnostic dispatcher, for a real syscall gate. */
uint64_t syscall_dispatch(const uint64_t *args) {
    uint64_t nr = args[0];
    if (nr == 1) { /* write(fd, user_buf, count) */
        uint64_t fd = args[1], addr = args[2], len = args[3];
        if (fd != 1 && fd != 2) return (uint64_t)-9;
        if (len > 4096 || addr >= USER_LIMIT ||
            len > USER_LIMIT - addr) return (uint64_t)-14;
        for (uint64_t i = 0; i < len; i++) {
            uint8_t *byte = user_byte(addr + i);
            if (!byte) return (uint64_t)-14;
            /* This UART writer handles bare M1 output, no TTY yet. */
            char str[2] = { (char)*byte, 0 };
            nas_print(str);
        }
        return len;
    }
    if (nr == 60) { /* exit(status) */
        nas_print("[EXIT] user status="); nas_dec(args[1]); nas_print("\n");
        if (args[1] == 0) {
            nas_print("[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit\n");
            outb(0xf4, 0); /* QEMU ISA debug-exit, status 1, real user-triggered. */
        }
        fail("user program exit was non-zero");
    }
    nas_print("[MISS] syscall nr="); nas_dec(nr); nas_print("\n");
    return (uint64_t)-38;
}

static void syscall_enable(void) {
    uint64_t efer = rdmsr(0xc0000080);
    wrmsr(0xc0000080, efer | 1); /* EFER.SCE */
    wrmsr(0xc0000081, (0x10ULL << 48) | (0x08ULL << 32)); /* STAR */
    wrmsr(0xc0000082, (uint64_t)(uintptr_t)syscall_entry); /* LSTAR */
    wrmsr(0xc0000084, (1ULL << 9) | (1ULL << 10)); /* FMASK IF|DF */
}

void m1_start(void) {
    nas_print("[M1] setting up GDT, TSS, IDT, syscall MSRs\n");
    cpu_tables_init();
    if (!paging_init()) fail("requires four-level paging");
    syscall_enable();
    const struct limine_file *module = nas_user_module();
    if (!module) fail("no Limine userspace ELF module");
    uint64_t entry = 0;
    if (!elf_load(module, &entry)) fail("invalid or unmappable static ELF");
    for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
         va < USER_STACK_TOP; va += PAGE) {
        if (!user_map(va, true)) fail("cannot map user stack");
    }
    nas_print("[M1] static ELF entry="); nas_hex(entry); nas_print("\n");
    nas_print("[M1] entering actual ring 3\n");
    enter_user(entry, USER_STACK_TOP - 16);
    fail("iretq returned");
}

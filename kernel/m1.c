
#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* Single-core M1: run a REAL static ELF from a Limine module in ring 3.
 * The new process CR3 preserves Limine's supervisor upper-half/HHDM mappings.
 * Separate process address spaces, scheduler and preemption come later.
 * Interrupts are disabled on syscall entry; TSS handles ring3 faults.
 */
extern uint64_t nas_frame_alloc(void);
extern bool nas_frame_free(uint64_t physical);
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
#define USER_STACK_PAGES 32

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

static void cpu_user_fp_init(void) {
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
    /* Separate userspace CR3: copy supervisor upper half, zero the lower half. */
    uint64_t boot_root = cr3 & PHYS_MASK;
    if (!boot_root) return false;
    root_phys = nas_frame_alloc();
    if (!root_phys) return false;
    uint64_t *old = phys_table(boot_root);
    uint64_t *fresh = phys_table(root_phys);
    for (unsigned i = 256; i < 512; i++)
        fresh[i] = old[i] & ~P_USER;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(root_phys) : "memory");
    return true;
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
    if (!entry) return false;
    if (*entry & P_PRESENT) {
        if (!(*entry & P_USER)) return false;
        if (writable) *entry |= P_RW;
        __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
        return true;
    }
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
static uint64_t loaded_phdr, loaded_phnum;
static uint64_t heap_break, mmap_cursor = 0x60000000ULL;
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
        !e->phnum || e->phnum > 64 ||
        !checked_range(e->phoff, (uint64_t)e->phnum * e->phentsize, module->size))
        return false;

    bool executable_entry = false;
    const struct elf_phdr *ph = (const struct elf_phdr *)(file + e->phoff);
    for (unsigned i = 0; i < e->phnum; ++i) {
        const struct elf_phdr *p = &ph[i];
        if (p->type != 1) continue;
        if (!p->memsz || p->memsz > (64ULL << 20) ||
            p->filesz > p->memsz ||
            !checked_range(p->offset, p->filesz, module->size) ||
            !checked_range(p->vaddr, p->memsz, USER_LIMIT) ||
            p->vaddr < 0x10000 ||
            p->vaddr + p->memsz > USER_STACK_TOP - 0x100000)
            return false;
        if (e->phoff >= p->offset &&
            e->phoff - p->offset < p->filesz &&
            (uint64_t)e->phnum * e->phentsize <=
                  p->filesz - (e->phoff - p->offset)) {
            loaded_phdr = p->vaddr + (e->phoff - p->offset);
        }
        if ((p->flags & 1) &&
            e->entry >= p->vaddr && e->entry - p->vaddr < p->memsz)
            executable_entry = true;
        uint64_t segment_end = p->vaddr + p->memsz;
        if (segment_end > heap_break) heap_break = segment_end;
        uint64_t begin = p->vaddr & ~(PAGE - 1);
        uint64_t end = (p->vaddr + p->memsz + PAGE - 1) & ~(PAGE - 1);
        for (uint64_t va = begin; va < end; va += PAGE)
            if (!user_map(va, (p->flags & 2) != 0)) return false;
        if (!user_copy_in(p->vaddr, file + p->offset, p->filesz))
            return false;
    }
    if (!executable_entry) return false;
    *entry = e->entry;
    loaded_phnum = e->phnum;
    return true; /* Small ET_EXEC probes need not map ELF/PHDR headers. */
}


/* A real Linux x86-64 userspace entry stack, not argv spoofing in kernel. */
static uint64_t cstr_len(const char *str) {
    uint64_t n = 0;
    while (str[n]) n++;
    return n;
}
static bool put_stack_string(uint64_t *sp, const char *str, uint64_t *address) {
    uint64_t size = cstr_len(str) + 1;
    if (*sp < USER_STACK_TOP - USER_STACK_PAGES * PAGE + size) return false;
    *sp -= size;
    if (!user_copy_in(*sp, (const uint8_t *)str, size)) return false;
    *address = *sp;
    return true;
}
static bool build_linux_stack(bool bash, uint64_t entry, uint64_t *out_sp) {
    static const char * const args_bash[] = {
        "/bin/bash", "--noprofile", "--norc", "-c", "echo hello"
    };
    static const char * const args_hello[] = {"/bin/hello"};
    static const char * const vars[] = {
        "PATH=/bin:/usr/bin", "HOME=/", "TERM=dumb", "LC_ALL=C"
    };
    const char * const *args = bash ? args_bash : args_hello;
    unsigned argc = bash ? 5 : 1;
    uint64_t sp = USER_STACK_TOP;
    uint64_t arg_ptrs[5], env_ptrs[4];
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t tsc = (uint64_t)lo | ((uint64_t)hi << 32);
    /* Noncryptographic boot seed for AT_RANDOM. Add real entropy later. */
    uint64_t random_bytes[2] = {
        tsc ^ (uint64_t)(uintptr_t)&tss,
        (tsc << 17) ^ nas_hhdm_offset()
    };
    sp -= sizeof(random_bytes);
    if (!user_copy_in(sp, (const uint8_t *)random_bytes,
                      sizeof(random_bytes))) return false;
    uint64_t random_address = sp;
    for (unsigned i = 0; i < 4; i++)
        if (!put_stack_string(&sp, vars[i], &env_ptrs[i])) return false;
    for (unsigned i = 0; i < argc; i++)
        if (!put_stack_string(&sp, args[i], &arg_ptrs[i])) return false;

    /* argc | argv[] NULL | envp[] NULL | auxv pairs | AT_NULL */
    uint64_t words[64];
    unsigned n = 0;
    words[n++] = argc;
    for (unsigned i = 0; i < argc; i++) words[n++] = arg_ptrs[i];
    words[n++] = 0;
    for (unsigned i = 0; i < 4; i++) words[n++] = env_ptrs[i];
    words[n++] = 0;
#define AUX(key, value) do { words[n++] = (key); words[n++] = (value); } while (0)
    AUX(3, loaded_phdr);      /* AT_PHDR */
    AUX(4, sizeof(struct elf_phdr)); /* AT_PHENT */
    AUX(5, loaded_phnum);     /* AT_PHNUM */
    AUX(6, PAGE);             /* AT_PAGESZ */
    AUX(7, 0);                /* AT_BASE: static binary */
    AUX(9, entry);            /* AT_ENTRY */
    AUX(11, 0); AUX(12, 0);   /* UID / EUID */
    AUX(13, 0); AUX(14, 0);   /* GID / EGID */
    AUX(23, 0);               /* AT_SECURE */
    AUX(25, random_address);  /* AT_RANDOM */
    AUX(31, arg_ptrs[0]);    /* AT_EXECFN */
    AUX(0, 0);                /* AT_NULL */
#undef AUX
    uint64_t byte_count = n * sizeof(uint64_t);
    if (sp < USER_STACK_TOP - USER_STACK_PAGES * PAGE + byte_count + 16)
        return false;
    sp = (sp - byte_count) & ~15ULL;
    if (!user_copy_in(sp, (const uint8_t *)words, byte_count)) return false;
    *out_sp = sp;
    return true;
}
static bool bash_module(const struct limine_file *file) {
    if (!file || !file->path) return false;
    const char *p = file->path;
    for (; *p; p++)
        if (p[0] == 'b' && p[1] == 'a' && p[2] == 's' && p[3] == 'h')
            return true;
    return false;
}
static bool running_bash;
static bool saw_bash_hello;
static unsigned matched_chars;
static uint32_t missing_count[512];
static void observe_stdout(char ch) {
    static const char text[] = "hello\n";
    if (ch == text[matched_chars]) {
        if (++matched_chars == sizeof(text) - 1) {
            saw_bash_hello = true;
            matched_chars = 0;
        }
    } else matched_chars = (ch == text[0]) ? 1 : 0;
}


/* M2: genuine Linux ABI semantics for single-process TLS + anonymous VM. */
static uint64_t thread_clear_tid;
static uint64_t user_fs_base;
static void user_invalidate(uint64_t va) {
    __asm__ volatile ("invlpg (%0)" : : "r"(va) : "memory");
}
static bool user_unmap_page(uint64_t va) {
    uint64_t *pte = user_leaf(va, false);
    if (!pte || !(*pte & P_PRESENT) || !(*pte & P_USER)) return true;
    uint64_t pa = *pte & PHYS_MASK;
    *pte = 0;
    user_invalidate(va);
    return nas_frame_free(pa);
}
static uint64_t sys_anon_mmap(uint64_t hint, uint64_t length,
                              uint64_t prot, uint64_t flags,
                              uint64_t fd, uint64_t offset) {
    (void)hint;
    /* MAP_PRIVATE|MAP_ANONYMOUS; no file/offset/map-fixed emulation. */
    if (!length || length > (64ULL << 20) || length > UINT64_MAX - PAGE)
        return (uint64_t)-22;
    if ((flags & 0x22) != 0x22 || (flags & ~0x22ULL) || fd != UINT64_MAX || offset)
        return (uint64_t)-38;
    if (prot & ~7ULL) return (uint64_t)-22;
    uint64_t count = (length + PAGE - 1) / PAGE;
    if (count > mmap_cursor / PAGE) return (uint64_t)-12;
    uint64_t start = mmap_cursor - count * PAGE;
    if (start < 0x20000000 || start >= USER_LIMIT) return (uint64_t)-12;
    uint64_t va = start;
    for (; va < mmap_cursor; va += PAGE)
        if (!user_map(va, (prot & 2) != 0)) break;
    if (va < mmap_cursor) {
        while (va > start) { va -= PAGE; user_unmap_page(va); }
        return (uint64_t)-12;
    }
    mmap_cursor = start;
    return start;
}
static uint64_t sys_munmap(uint64_t addr, uint64_t len) {
    if (!len || (addr & (PAGE - 1)) || len > UINT64_MAX - (PAGE - 1))
        return (uint64_t)-22;
    uint64_t count = (len + PAGE - 1) / PAGE;
    if (addr >= USER_LIMIT || count > (USER_LIMIT - addr) / PAGE)
        return (uint64_t)-22;
    for (uint64_t i = 0; i < count; i++)
        if (!user_unmap_page(addr + i * PAGE)) return (uint64_t)-14;
    return 0;
}
static uint64_t sys_brk(uint64_t want) {
    uint64_t base = heap_break;
    if (!want) return heap_break;
    /* brk is a Linux special: failure returns the unchanged break. */
    if (want < 0x10000 || want >= 0x20000000) return base;
    uint64_t before = (heap_break + PAGE - 1) & ~(PAGE - 1);
    uint64_t after = (want + PAGE - 1) & ~(PAGE - 1);
    if (after > before) {
        uint64_t va = before;
        for (; va < after; va += PAGE)
            if (!user_map(va, true)) break;
        if (va != after) {
            while (va > before) { va -= PAGE; user_unmap_page(va); }
            return base;
        }
    } else if (after < before) {
        for (uint64_t va = after; va < before; va += PAGE)
            if (!user_unmap_page(va)) return base;
    }
    heap_break = want;
    return want;
}
static uint64_t sys_arch_prctl(uint64_t op, uint64_t address) {
    if (op == 0x1002) { /* ARCH_SET_FS */
        if (address >= USER_LIMIT) return (uint64_t)-22;
        user_fs_base = address;
        wrmsr(0xc0000100, address);
        return 0;
    }
    if (op == 0x1003) { /* ARCH_GET_FS */
        if (address >= USER_LIMIT || address > USER_LIMIT - sizeof(uint64_t))
            return (uint64_t)-14;
        for (unsigned i = 0; i < sizeof(uint64_t); i++) {
            uint8_t *p = user_byte(address + i);
            if (!p) return (uint64_t)-14;
            *p = (uint8_t)(user_fs_base >> (8 * i));
        }
        return 0;
    }
    return (uint64_t)-22;
}


/* Single-process POSIX descriptors over actual UART and device files.
 * No fake TTY, disk, /proc, or filesystem contents are supplied.
 */
extern void nas_putchar(char c);
enum { FD_CLOSED = 0, FD_UART = 1, FD_NULL = 2, FD_ZERO = 3 };
static uint8_t descriptors[64];
static uint64_t blocked_signals;
static uint64_t signal_actions[65][4];

static bool user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t n = 0; n < len; n++) {
        uint8_t *p = user_byte(addr + n);
        if (!p) return false;
        dst[n] = *p;
    }
    return true;
}
static bool user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t n = 0; n < len; n++) {
        uint64_t *pte = user_leaf((addr + n) & ~(PAGE - 1), false);
        if (!pte || !(*pte & P_PRESENT) || !(*pte & P_RW) || !(*pte & P_USER))
            return false;
    }
    for (uint64_t n = 0; n < len; n++)
        *user_byte(addr + n) = src[n];
    return true;
}
static bool read_u64(uint64_t addr, uint64_t *value) {
    uint8_t bytes[8];
    if (!user_read_bytes(addr, bytes, 8)) return false;
    *value = 0;
    for (unsigned n = 0; n < 8; n++) *value |= (uint64_t)bytes[n] << (8 * n);
    return true;
}
static bool write_u64(uint64_t addr, uint64_t value) {
    uint8_t bytes[8];
    for (unsigned n = 0; n < 8; n++) bytes[n] = (uint8_t)(value >> (8 * n));
    return user_write_bytes(addr, bytes, 8);
}
static bool same_string(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (*a != *b) return false;
    return *a == *b;
}
static uint64_t sys_open_device(uint64_t pathname, uint64_t flags) {
    char path[256];
    unsigned i = 0;
    if (pathname >= USER_LIMIT) return (uint64_t)-14;
    for (; i < sizeof(path) - 1; i++) {
        uint8_t *p = user_byte(pathname + i);
        if (!p) return (uint64_t)-14;
        path[i] = (char)*p;
        if (path[i] == 0) break;
    }
    if (i == sizeof(path) - 1) return (uint64_t)-36; /* ENAMETOOLONG */
    if (same_string(path, "/dev/tty")) return (uint64_t)-6; /* ENXIO */
    uint8_t kind = FD_CLOSED;
    if (same_string(path, "/dev/null")) kind = FD_NULL;
    if (same_string(path, "/dev/zero")) kind = FD_ZERO;
    if (!kind) return (uint64_t)-2; /* ENOENT: no filesystem exists yet */
    if (flags & (0x40ULL | 0x200ULL)) return (uint64_t)-30; /* EROFS */
    for (unsigned fd = 3; fd < 64; fd++) {
        if (descriptors[fd] != FD_CLOSED) continue;
        descriptors[fd] = kind;
        return fd;
    }
    return (uint64_t)-24; /* EMFILE */
}
static uint64_t sys_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED) return (uint64_t)-9;
    if (length > (1ULL << 20) ||
        address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-14;
    if (descriptors[fd] == FD_NULL || descriptors[fd] == FD_ZERO) return length;
    if (descriptors[fd] != FD_UART) return (uint64_t)-9;
    for (uint64_t i = 0; i < length; i++) {
        uint8_t *p = user_byte(address + i);
        if (!p) return i ? i : (uint64_t)-14;
        nas_putchar((char)*p);
        if (fd == 1 && running_bash) observe_stdout((char)*p);
    }
    return length;
}
static uint64_t sys_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED) return (uint64_t)-9;
    if (descriptors[fd] == FD_NULL) return 0;
    if (descriptors[fd] == FD_UART) return (uint64_t)-11; /* no RX-ready: EAGAIN */
    if (descriptors[fd] != FD_ZERO) return (uint64_t)-9;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-14;
    uint8_t zero = 0;
    for (uint64_t i = 0; i < length; i++)
        if (!user_write_bytes(address + i, &zero, 1)) return i ? i : (uint64_t)-14;
    return length;
}
static uint64_t sys_fd_writev(uint64_t fd, uint64_t iov, uint64_t count) {
    if (count > 64) return (uint64_t)-22;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT - iov) / 16))
        return (uint64_t)-14;
    uint64_t sent = 0;
    for (uint64_t n = 0; n < count; n++) {
        uint64_t address, bytes;
        if (!read_u64(iov + n * 16, &address) ||
            !read_u64(iov + n * 16 + 8, &bytes))
            return sent ? sent : (uint64_t)-14;
        uint64_t r = sys_fd_write(fd, address, bytes);
        if ((int64_t)r < 0) return sent ? sent : r;
        sent += r;
        if (r != bytes) break;
    }
    return sent;
}
static uint64_t sys_getcwd_root(uint64_t buffer, uint64_t size) {
    const uint8_t cwd[2] = {'/', 0};
    if (size < 2) return (uint64_t)-34; /* ERANGE */
    if (!user_write_bytes(buffer, cwd, 2)) return (uint64_t)-14;
    return 2;
}
static uint64_t sys_uname_naseed(uint64_t buffer) {
    struct uts {
        char sysname[65], node[65], release[65], version[65];
        char machine[65], domain[65];
    };
    static const struct uts data = {
        "Linux", "naseed", "0.1", "NaSeed experimental x86_64",
        "x86_64", "localdomain"
    };
    return user_write_bytes(buffer, (const uint8_t *)&data, sizeof(data))
        ? 0 : (uint64_t)-14;
}
/* Proper syscall-level rt_sigaction / sigprocmask structures, 8-byte sigset. 
 * Delivery is intentionally not claimed until an actual signal path exists. */
static uint64_t sys_sigaction(uint64_t signum, uint64_t new_action,
                              uint64_t old_action, uint64_t size) {
    if (size != 8 || signum < 1 || signum > 64) return (uint64_t)-22;
    if (new_action && (signum == 9 || signum == 19)) return (uint64_t)-22;
    if (old_action &&
        !user_write_bytes(old_action, (const uint8_t *)signal_actions[signum], 32))
        return (uint64_t)-14;
    if (new_action &&
        !user_read_bytes(new_action, (uint8_t *)signal_actions[signum], 32))
        return (uint64_t)-14;
    return 0;
}
static uint64_t sys_sigprocmask(uint64_t how, uint64_t set,
                                uint64_t previous, uint64_t size) {
    if (size != 8) return (uint64_t)-22;
    uint64_t newmask = 0;
    if (set && !read_u64(set, &newmask)) return (uint64_t)-14;
    if (previous && !write_u64(previous, blocked_signals)) return (uint64_t)-14;
    if (set) {
        if (how == 0) blocked_signals |= newmask;        /* SIG_BLOCK */
        else if (how == 1) blocked_signals &= ~newmask;  /* SIG_UNBLOCK */
        else if (how == 2) blocked_signals = newmask;    /* SIG_SETMASK */
        else return (uint64_t)-22;
        blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
    }
    return 0;
}

/* Strict single-program ABI diagnostic dispatcher, for a real syscall gate. */
uint64_t syscall_dispatch(const uint64_t *args) {
    uint64_t nr = args[0];
    if (nr == 158) return sys_arch_prctl(args[1], args[2]);
    if (nr == 218) { thread_clear_tid = args[1]; return 1; }
    if (nr == 39 || nr == 186) return 1;  /* getpid / gettid */
    if (nr == 110) return 0;  /* getppid for PID 1 */
    if (nr == 102 || nr == 104 || nr == 107 || nr == 108) return 0; /* root credentials */
    if (nr == 9) return sys_anon_mmap(args[1], args[2], args[3],
                                       args[4], args[5], args[6]);
    if (nr == 11) return sys_munmap(args[1], args[2]);
    if (nr == 12) return sys_brk(args[1]);
    if (nr == 0) return sys_fd_read(args[1], args[2], args[3]);
    if (nr == 1) return sys_fd_write(args[1], args[2], args[3]);
    if (nr == 2) return sys_open_device(args[1], args[2]);
    if (nr == 3) {
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
            return (uint64_t)-9;
        descriptors[args[1]] = FD_CLOSED;
        return 0;
    }
    if (nr == 16) {
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
            return (uint64_t)-9;
        return (uint64_t)-25; /* ENOTTY: no TTY driver yet */
    }
    if (nr == 20) return sys_fd_writev(args[1], args[2], args[3]);
    if (nr == 13) return sys_sigaction(args[1], args[2], args[3], args[4]);
    if (nr == 14) return sys_sigprocmask(args[1], args[2], args[3], args[4]);
    if (nr == 63) return sys_uname_naseed(args[1]);
    if (nr == 79) return sys_getcwd_root(args[1], args[2]);
    if (nr == 121) return args[1] == 0 || args[1] == 1 ? 1 : (uint64_t)-3;
    if (nr == 111) return 1; /* getpgrp: only process group 1 exists */
    if (nr == 60 || nr == 231) { /* exit / exit_group */
        nas_print("[EXIT] user status="); nas_dec(args[1]); nas_print("\n");
        if (running_bash) {
            if (args[1] == 0 && saw_bash_hello) {
                nas_print("[PASS] M2 real GNU Bash -c echo hello\n");
                outb(0xf4, 0);
            }
            fail("real Bash exited without exact hello and status zero");
        }
        if (args[1] == 0) {
            nas_print("[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit\n");
            outb(0xf4, 0);
        }
        fail("user program exit was non-zero");
    }
    if (nr >= 512 || ++missing_count[nr] <= 3) {
        nas_print("[MISS] syscall nr="); nas_dec(nr); nas_print("\n");
    }
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
    cpu_user_fp_init();
    if (!paging_init()) fail("requires four-level paging");
    syscall_enable();
    const struct limine_file *module = nas_user_module();
    if (!module) fail("no Limine userspace ELF module");
    running_bash = bash_module(module);
    if (running_bash) nas_print("[M2] real GNU Bash module discovered\n");
    uint64_t entry = 0;
    if (!elf_load(module, &entry)) fail("invalid or unmappable static ELF");
    for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
         va < USER_STACK_TOP; va += PAGE) {
        if (!user_map(va, true)) fail("cannot map user stack");
    }
    uint64_t stack_pointer;
    descriptors[0] = FD_UART;
    descriptors[1] = FD_UART;
    descriptors[2] = FD_UART;
    if (!build_linux_stack(running_bash, entry, &stack_pointer))
        fail("cannot initialize Linux userspace stack");
    nas_print("[M1] static ELF entry="); nas_hex(entry); nas_print("\n");
    if (running_bash) nas_print("[M2] entering real Bash in ring 3\n");
    else nas_print("[M1] entering actual ring 3\n");
    enter_user(entry, stack_pointer);
    fail("iretq returned");
}

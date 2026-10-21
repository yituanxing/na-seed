
#include <limine.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "nr.h"
#include "errno.h"
#include "bootfs.h"
#include "task.h"
#include "fd.h"
#include "tmpfs.h"
#include "path.h"
#include "pipe.h"
#include "boot_image.h"
#include "arch/arch.h"
#include "mm/vm.h"
#include "mm/usercopy.h"
#include "task/process.h"
#include "platform/platform.h"

/* Process-local state was previously static global memory. A task is now
 * its explicit owner; the single-task aliases keep the proven syscall path
 * unchanged while the upcoming arch task switch is developed separately.
 */
#define TASK (*nas_current_task)
#define root_phys TASK.root_phys
#define heap_break TASK.heap_break
#define mmap_cursor TASK.mmap_cursor
#define loaded_phdr TASK.loaded_phdr
#define loaded_phnum TASK.loaded_phnum
#define loaded_main_entry TASK.loaded_main_entry
#define loaded_interp_base TASK.loaded_interp_base
#define thread_clear_tid TASK.thread_clear_tid
#define user_fs_base TASK.arch.user_fs_base
#define descriptors TASK.fds.descriptors
#define file_slot TASK.fds.file_slot
#define file_refcount nas_file_refcount
#define fd_cloexec TASK.fds.fd_cloexec
#define file_status_flags nas_file_status_flags
#define boot_files TASK.fds.boot_files
#define boot_entries TASK.fds.boot_entries
#define boot_file_offsets nas_boot_file_offsets
#define current_workdir TASK.cwd
#define blocked_signals TASK.blocked_signals
#define signal_actions TASK.signal_actions
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine
#define saw_bash_hello TASK.saw_bash_hello
#define matched_chars TASK.matched_chars

/* Single-core M1: run a REAL static ELF from a Limine module in ring 3.
 * The new process CR3 preserves Limine's supervisor upper-half/HHDM mappings.
 * Private user address spaces and cooperative task switching are present.
 * Timer preemption and independent per-task kernel stacks are not.
 * Interrupts are disabled on syscall entry; TSS handles ring3 faults.
 */
extern uint64_t nas_frame_alloc(void);
extern bool nas_frame_free(uint64_t physical);
extern uint64_t nas_hhdm_offset(void);
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_hex(uint64_t);
extern const struct limine_file *nas_user_module(void);
extern const struct limine_file *nas_module(uint64_t index);

#define USER_STACK_TOP 0x0000000070000000ULL
#define USER_STACK_PAGES 32

__attribute__((noreturn)) static void fail(const char *message) {
    nas_print("[FAIL] M1 ");
    nas_print(message);
    nas_print("\n");
    nas_arch_halt();
}
static bool user_copy_in(uint64_t va, const uint8_t *data, uint64_t length) {
    for (uint64_t i = 0; i < length; i++) {
        uint8_t *dst = nas_mmu_user_byte(va + i);
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
#define ALPINE_APP_BASE 0x10000000ULL
#define ALPINE_LDSO_BASE 0x30000000ULL

/* ELF bytes may originate from a Limine ELF module (M1-M4) or a file
 * inside the indexed CPIO initramfs (M5.1). This is not an execve ABI.
 * PT_INTERP is still checked against the one supported musl interpreter.
 */
struct elf_image {
    const uint8_t *data;
    uint64_t size;
};
static bool elf_map_image(const struct elf_image *image,
                          uint64_t bias, bool main_image,
                          uint64_t *entry, bool *has_interp) {
    if (!image || !image->data || image->size < sizeof(struct elf_hdr))
        return false;
    const uint8_t *file = image->data;
    const struct elf_hdr *e = (const struct elf_hdr *)file;
    if (e->ident[0] != 0x7f || e->ident[1] != 'E' ||
        e->ident[2] != 'L' || e->ident[3] != 'F' ||
        e->ident[4] != 2 || e->ident[5] != 1 ||
        (e->type != 2 && e->type != 3) || e->machine != 62 ||
        e->phentsize != sizeof(struct elf_phdr) ||
        !e->phnum || e->phnum > 64 ||
        !checked_range(e->phoff, (uint64_t)e->phnum * e->phentsize,
                       image->size))
        return false;
    if ((e->type == 2 && bias) || (e->type == 3 && !bias))
        return false;
    bool has_entry = false;
    const struct elf_phdr *ph = (const struct elf_phdr *)(file + e->phoff);
    for (unsigned i = 0; i < e->phnum; i++) {
        const struct elf_phdr *p = &ph[i];
        if (p->type == 3 && main_image) { /* PT_INTERP */
            static const char name[] = "/lib/ld-musl-x86_64.so.1";
            if (!checked_range(p->offset, p->filesz, image->size) ||
                p->filesz != sizeof(name)) return false;
            for (unsigned n = 0; n < sizeof(name); n++)
                if (file[p->offset + n] != (uint8_t)name[n]) return false;
            *has_interp = true;
        }
        if (p->type != 1) continue;
        if (!p->memsz || p->memsz > (64ULL << 20) ||
            p->filesz > p->memsz ||
            !checked_range(p->offset, p->filesz, image->size) ||
            !checked_range(p->vaddr, p->memsz, USER_LIMIT) ||
            p->vaddr > UINT64_MAX - bias ||
            p->memsz > UINT64_MAX - bias - p->vaddr)
            return false;
        uint64_t start = p->vaddr + bias, end = start + p->memsz;
        if (start < 0x10000 || end > USER_STACK_TOP - 0x100000 ||
            end > UINT64_MAX - PAGE) return false;
        if ((p->flags & 1) && e->entry >= p->vaddr &&
            e->entry - p->vaddr < p->memsz) has_entry = true;
        if (main_image && e->phoff >= p->offset &&
            e->phoff - p->offset < p->filesz &&
            (uint64_t)e->phnum * e->phentsize <=
                p->filesz - (e->phoff - p->offset))
            loaded_phdr = start + (e->phoff - p->offset);
        if (main_image && end > heap_break) heap_break = end;
        uint64_t first = start & ~(PAGE - 1);
        uint64_t last = (end + PAGE - 1) & ~(PAGE - 1);
        for (uint64_t va = first; va < last; va += PAGE)
            if (!nas_mmu_map_page(va, (p->flags & 2) != 0)) return false;
        if (!user_copy_in(start, file + p->offset, p->filesz)) return false;
    }
    if (!has_entry || e->entry > UINT64_MAX - bias) return false;
    *entry = e->entry + bias;
    if (main_image) loaded_phnum = e->phnum;
    return true;
}
static bool elf_load(const struct elf_image *image,
                     const struct elf_image *interpreter, uint64_t *entry) {
    if (!image || !image->data || image->size < sizeof(struct elf_hdr))
        return false;
    const struct elf_hdr *e = (const struct elf_hdr *)image->data;
    uint64_t bias = e->type == 3 ? ALPINE_APP_BASE : 0;
    bool has_interp = false;
    if (!elf_map_image(image, bias, true, &loaded_main_entry, &has_interp))
        return false;
    *entry = loaded_main_entry;
    if (!has_interp) return true;
    if (!interpreter || !interpreter->data) return false;
    loaded_interp_base = ALPINE_LDSO_BASE;
    bool nested_interp = false;
    if (!elf_map_image(interpreter, loaded_interp_base, false, entry, &nested_interp))
        return false;
    nas_print("[DYN] real musl interpreter loaded, entry=");
    nas_hex(*entry); nas_print("\n");
    return true;
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
/* A boot test selects user argv via Limine's executable command line.
 * Kept deliberately independent of filesystem paths and kernel program names.
 * Quotes/backslash group arguments; this is NOT a shell or a second ABI.
 * Missing cmdline means legacy M0-M4 regression argv; malformed cmdline fails.
 */
#define NAS_ARGV_MAX 12
static char boot_argv_chars[256];
static const char *boot_argv[NAS_ARGV_MAX];
static bool boot_argv_override;
static unsigned boot_argc;
static bool parse_boot_argv(const char *cmdline,
                            const char *const **out_args, unsigned *out_argc) {
    static const char prefix[] = "naseed.argv=";
    const char *p = cmdline;
    for (unsigned i = 0; i < sizeof(prefix) - 1; i++)
        if (p[i] != prefix[i]) return false;
    p += sizeof(prefix) - 1;
    unsigned count = 0, used = 0;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (count == NAS_ARGV_MAX) return false;
        boot_argv[count] = &boot_argv_chars[used];
        char quote = 0;
        while (*p) {
            char ch = *p++;
            if (ch == '\\') {
                if (!*p) return false;
                ch = *p++;
            } else if (ch == '\'' || ch == '"') {
                if (!quote) { quote = ch; continue; }
                if (quote == ch) { quote = 0; continue; }
            } else if (!quote && (ch == ' ' || ch == '\t')) {
                break;
            }
            if ((unsigned char)ch < 32 || used + 2 > sizeof(boot_argv_chars))
                return false;
            boot_argv_chars[used++] = ch;
        }
        if (quote || used + 1 > sizeof(boot_argv_chars)) return false;
        boot_argv_chars[used++] = 0;
        count++;
    }
    if (!count || !boot_argv_chars[0]) return false;
    *out_args = boot_argv;
    *out_argc = count;
    boot_argc = count;
    boot_argv_override = true;
    return true;
}
#define NAS_ENV_MAX 16
static bool build_user_stack(const char *const *args, unsigned argc,
                             const char *const *vars, unsigned envc,
                             const char *execfn, uint64_t *out_sp) {
    uint64_t sp = USER_STACK_TOP;
    uint64_t arg_ptrs[NAS_ARGV_MAX], env_ptrs[NAS_ENV_MAX];
    uint64_t execfn_ptr;
    if (argc > NAS_ARGV_MAX || envc > NAS_ENV_MAX) return false;
    uint64_t tsc = nas_arch_cycle_counter();
    /* Noncryptographic boot seed for AT_RANDOM. Add real entropy later. */
    uint64_t random_bytes[2] = {
        tsc ^ (uint64_t)(uintptr_t)&build_user_stack,
        (tsc << 17) ^ nas_hhdm_offset()
    };
    sp -= sizeof(random_bytes);
    if (!user_copy_in(sp, (const uint8_t *)random_bytes,
                      sizeof(random_bytes))) return false;
    uint64_t random_address = sp;
    if (!put_stack_string(&sp, execfn, &execfn_ptr)) return false;
    for (unsigned i = 0; i < envc; i++)
        if (!put_stack_string(&sp, vars[i], &env_ptrs[i])) return false;
    for (unsigned i = 0; i < argc; i++)
        if (!put_stack_string(&sp, args[i], &arg_ptrs[i])) return false;

    /* argc | argv[] NULL | envp[] NULL | auxv pairs | AT_NULL */
    uint64_t words[96];
    unsigned n = 0;
    words[n++] = argc;
    for (unsigned i = 0; i < argc; i++) words[n++] = arg_ptrs[i];
    words[n++] = 0;
    for (unsigned i = 0; i < envc; i++) words[n++] = env_ptrs[i];
    words[n++] = 0;
#define AUX(key, value) do { words[n++] = (key); words[n++] = (value); } while (0)
    AUX(3, loaded_phdr);      /* AT_PHDR */
    AUX(4, sizeof(struct elf_phdr)); /* AT_PHENT */
    AUX(5, loaded_phnum);     /* AT_PHNUM */
    AUX(6, PAGE);             /* AT_PAGESZ */
    AUX(7, loaded_interp_base); /* AT_BASE: real interpreter */
    AUX(9, loaded_main_entry); /* AT_ENTRY: main ELF */
    AUX(11, 0); AUX(12, 0);   /* UID / EUID */
    AUX(13, 0); AUX(14, 0);   /* GID / EGID */
    AUX(23, 0);               /* AT_SECURE */
    AUX(25, random_address);  /* AT_RANDOM */
    AUX(31, execfn_ptr);    /* AT_EXECFN */
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
static bool build_linux_stack(bool bash, bool busybox, uint64_t *out_sp) {
    static const char * const args_bash[] = {
        "/bin/bash", "--noprofile", "--norc", "-c", "echo hello"
    };
    static const char * const args_hello[] = {"/bin/hello"};
    static const char * const args_busybox[] = {"/bin/busybox", "echo", "hello"};
    static const char * const vars[] = {
        "PATH=/bin:/usr/bin", "HOME=/", "TERM=dumb", "LC_ALL=C"
    };
    const char * const *args = bash ? args_bash : (busybox ? args_busybox : args_hello);
    unsigned argc = bash ? 5 : (busybox ? 3 : 1);
    extern const char *nas_cmdline(void);
    const char *cmdline = nas_cmdline();
    if (cmdline && *cmdline) {
        /* m1_start parses once, before the archive-only ELF lookup. The same
         * exact argv is handed to libc on the Linux entry stack.
         */
        if (!boot_argv_override) return false;
        args = boot_argv;
        argc = boot_argc;
        nas_print("[CMDLINE] userspace argv from Limine: ");
        nas_print(cmdline);
        nas_print("\n");
    }
    return build_user_stack(args, argc, vars, 4, args[0], out_sp);
}
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


/* User-copy contract now declared in mm/usercopy.h. */
static uint64_t sys_arch_prctl(uint64_t op, uint64_t address) {
    if (op == 0x1002) { /* ARCH_SET_FS */
        if (address >= USER_LIMIT) return (uint64_t)-EINVAL;
        user_fs_base = address;
        nas_arch_set_user_tls(address);
        return 0;
    }
    if (op == 0x1003) { /* ARCH_GET_FS */
        if (address >= USER_LIMIT || address > USER_LIMIT - sizeof(uint64_t))
            return (uint64_t)-EFAULT;
        uint8_t bytes[sizeof(uint64_t)];
        for (unsigned i = 0; i < sizeof(bytes); i++)
            bytes[i] = (uint8_t)(user_fs_base >> (8 * i));
        return nas_user_write_bytes(address, bytes, sizeof(bytes))
            ? 0 : (uint64_t)-EFAULT;
    }
    return (uint64_t)-EINVAL;
}


/* Single-process POSIX descriptors over actual UART and device files.
 * No fake TTY, disk, /proc, or filesystem contents are supplied.
 */
extern void nas_putchar(char c);
enum { FD_CLOSED = 0, FD_UART = 1, FD_NULL = 2, FD_ZERO = 3,
       FD_BOOTFS = 4, FD_ARCHIVE = 5, FD_DIR = 6,
       FD_PIPE_R = 7, FD_PIPE_W = 8, FD_TMP = 9, FD_TMP_DIR = 10 };
static bool same_string(const char *a, const char *b);
/* Per-FD handles share a refcounted open-file description after dup().
 * All fd aliases must observe the same read/seek/directory cursor.
 */
/* Read-only boot filesystem backed by bytes of the actual Limine modules.
 * Paths name exactly the binaries the bootloader provided, not fake files.
 */

/* Pipe ring/object lifetime is private to pipe.c. The syscall layer
 * assigns per-task FDs and validates user buffers before copying bytes.
 */
/* FD bookkeeping is syscall-local, but inode lifetime and slot identity now
 * live in the dedicated RAM /tmp backend, with shared file descriptions. */
static const struct nas_tmp_file *tmp_by_fd(uint64_t fd) {
    return tmp_by_slot(file_slot[fd]);
}

/* The two endpoints are one pipe object, with separate shared-description
 * slots. Allocation remains transactional until both FD/slot pairs are
 * available; only then is the backend created. */
static uint64_t sys_pipe2(uint64_t user_pair, uint64_t flags) {
    if (flags & ~(0x800ULL | 0x80000ULL)) return (uint64_t)-EINVAL;
    if (user_pair >= USER_LIMIT || user_pair > USER_LIMIT - 8)
        return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < 8; i++)
        if (!nas_mmu_user_byte(user_pair + i)) return (uint64_t)-EFAULT;
    unsigned fds[2], slots[2], nfds = 0, nslots = 0;
    for (unsigned fd = 3; fd < NAS_FD_LIMIT && nfds < 2; fd++)
        if (descriptors[fd] == FD_CLOSED) fds[nfds++] = fd;
    if (nfds != 2) return (uint64_t)-EMFILE;
    for (unsigned slot = 0; slot < NAS_FD_LIMIT && nslots < 2; slot++)
        if (!file_refcount[slot]) slots[nslots++] = slot;
    if (nslots != 2) return (uint64_t)-ENFILE;
    int result = nas_pipe_create(slots[0], slots[1]);
    if (result) return (uint64_t)(int64_t)result;
    for (unsigned i = 0; i < 2; i++) {
        unsigned fd = fds[i], slot = slots[i];
        descriptors[fd] = i ? FD_PIPE_W : FD_PIPE_R;
        file_slot[fd] = (uint8_t)slot;
        file_refcount[slot] = 1;
        file_status_flags[slot] = (i ? 1ULL : 0ULL) | (flags & 0x800ULL);
        fd_cloexec[fd] = !!(flags & 0x80000ULL);
    }
    uint8_t output[8] = {0};
    for (unsigned i = 0; i < 2; i++)
        for (unsigned j = 0; j < 4; j++)
            output[i * 4 + j] = (uint8_t)(fds[i] >> (j * 8));
    if (!nas_user_write_bytes(user_pair, output, sizeof(output)))
        fail("pipe2 validated destination became inaccessible");
    nas_print("[PIPE] created read="); nas_dec(fds[0]);
    nas_print(" write="); nas_dec(fds[1]); nas_print("\n");
    return 0;
}
static uint64_t pipe_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    unsigned slot = file_slot[fd];
    int64_t available = nas_pipe_read_amount(
        slot, length, !!(file_status_flags[slot] & 0x800ULL));
    if (available <= 0) return (uint64_t)available;
    unsigned count = (unsigned)available, n = 0;
    for (; n < count; n++)
        if (!nas_mmu_user_byte(address + n)) break;
    if (!n) return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < n; i++) {
        uint8_t value = nas_pipe_read_byte(slot, i);
        if (!nas_user_write_bytes(address + i, &value, 1))
            fail("pipe read validated user mapping became inaccessible");
    }
    nas_pipe_read_commit(slot, n);
    return n;
}
static uint64_t pipe_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    unsigned slot = file_slot[fd];
    int64_t available = nas_pipe_write_amount(
        slot, length, !!(file_status_flags[slot] & 0x800ULL));
    if (available <= 0) return (uint64_t)available;
    unsigned count = (unsigned)available, n = 0;
    for (; n < count; n++) {
        uint8_t *src = nas_mmu_user_byte(address + n);
        if (!src) break;
        nas_pipe_write_byte(slot, n, *src);
    }
    if (!n) return (uint64_t)-EFAULT;
    nas_pipe_write_commit(slot, n);
    return n;
}

/* FD table aliases and shared open descriptions are owned by fd.c.
 * Release the pipe and RAM-inode backends only after the last reference
 * disappears. dup2 must close an existing destination through this hook.
 */
static void fd_close_raw(uint64_t fd) {
    unsigned final_slot=nas_fd_close(nas_current_task, (unsigned)fd);
    if (final_slot == NAS_FD_LIMIT) return;
    nas_pipe_release_description(final_slot);
    tmp_release_slot(final_slot);
}
static uint64_t fd_clone(uint64_t src, uint64_t dst, bool cloexec) {
    if (src >= NAS_FD_LIMIT || descriptors[src] == FD_CLOSED ||
        dst >= NAS_FD_LIMIT) return (uint64_t)-EBADF;
    if (src == dst) return dst;
    fd_close_raw(dst);
    return nas_fd_clone(nas_current_task, src, dst, cloexec);
}
static uint64_t fd_duplicate(uint64_t oldfd, uint64_t minimum,
                             bool cloexec) {
    return nas_fd_duplicate(nas_current_task, oldfd, minimum, cloexec);
}
static uint64_t sys_fcntl(uint64_t fd, uint64_t command, uint64_t arg) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED)
        return (uint64_t)-EBADF;
    unsigned slot = file_slot[fd];
    switch (command) {
    case 0: return fd_duplicate(fd, arg, false);  /* F_DUPFD */
    case 1: return fd_cloexec[fd];                 /* F_GETFD */
    case 2: fd_cloexec[fd] = (uint8_t)(arg & 1); return 0; /* F_SETFD */
    case 3: return file_status_flags[slot];       /* F_GETFL */
    case 4: /* F_SETFL: only the O_NONBLOCK/O_APPEND mutable subset */
        file_status_flags[slot] =
            (file_status_flags[slot] & ~(0x800ULL | 0x400ULL)) |
            (arg & (0x800ULL | 0x400ULL));
        return 0;
    case 1030: return fd_duplicate(fd, arg, true); /* F_DUPFD_CLOEXEC */
    default: return (uint64_t)-EINVAL; /* unsupported fcntl cmd */
    }
}
static const struct limine_file *bootfs_lookup(const char *path) {
    if (same_string(path, "/bin/busybox")) return nas_module(0);
    if (same_string(path, "/lib/ld-musl-x86_64.so.1") ||
        same_string(path, "/lib/libc.musl-x86_64.so.1"))
        return nas_module(1);
    return 0;
}

static bool user_read_bytes(uint64_t addr, uint8_t *dst, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t n = 0; n < len; n++) {
        uint8_t *p = nas_mmu_user_byte(addr + n);
        if (!p) return false;
        dst[n] = *p;
    }
    return true;
}
bool nas_user_write_bytes(uint64_t addr, const uint8_t *src, uint64_t len) {
    if (addr >= USER_LIMIT || len > USER_LIMIT - addr) return false;
    for (uint64_t n = 0; n < len; n++) {
        if (!nas_mmu_user_writable(addr + n))
            return false;
    }
    for (uint64_t n = 0; n < len; n++)
        *nas_mmu_user_byte(addr + n) = src[n];
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
    return nas_user_write_bytes(addr, bytes, 8);
}
static bool same_string(const char *a, const char *b) {
    for (; *a && *b; a++, b++) if (*a != *b) return false;
    return *a == *b;
}
/* Lexically combine absolute, cwd-relative and dirfd-relative paths.
 * bootfs_resolve performs archive symlink traversal at the actual lookup.
 */
/* This adapter owns user memory and dirfd lookup. Component processing
 * lives in path.c and can be tested independently of the syscall ABI. */
static uint64_t resolve_path(uint64_t dirfd, uint64_t pathname,
                             char out[256]) {
    char src[256];
    unsigned n=0;
    if (pathname>=USER_LIMIT) return (uint64_t)-EFAULT;
    for (;n<sizeof(src);n++) {
        uint8_t *p=nas_mmu_user_byte(pathname+n);
        if (!p) return (uint64_t)-EFAULT;
        src[n]=(char)*p;
        if (!src[n]) break;
    }
    if (n==sizeof(src)) return (uint64_t)-ENAMETOOLONG;
    if (!n) return (uint64_t)-ENOENT;
    const char *base=0;
    if (src[0]!='/') {
        if (dirfd==(uint64_t)-100) base=current_workdir;
        else {
            if (dirfd>=NAS_FD_LIMIT || descriptors[dirfd]==FD_CLOSED)
                return (uint64_t)-EBADF;
            if (descriptors[dirfd]==FD_TMP_DIR) {
                const struct nas_tmp_file *dir=tmp_by_fd(dirfd);
                if (!dir) return (uint64_t)-EBADF;
                base=dir->path;
            } else {
                if (descriptors[dirfd]!=FD_DIR || !boot_entries[dirfd])
                    return (uint64_t)-ENOTDIR;
                base=boot_entries[dirfd]->path;
            }
        }
    }
    return (uint64_t)(int64_t)nas_path_resolve(base,src,out);
}

static uint64_t tmp_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    const struct nas_tmp_file *file = tmp_by_fd(fd);
    if (!file) return (uint64_t)-EBADF;
    if (!length) return 0;
    uint64_t slot = file_slot[fd], offset = boot_file_offsets[slot];
    if (offset >= file->size) return 0;
    if (length > file->size - offset) length = file->size - offset;
    if (!nas_user_write_bytes(address, file->bytes + offset, length))
        return (uint64_t)-EFAULT;
    boot_file_offsets[slot] += length;
    return length;
}
static uint64_t tmp_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    unsigned slot = file_slot[fd];
    struct nas_tmp_file *file = tmp_by_slot(slot);
    if (!file) return (uint64_t)-EBADF;
    if (!length) return 0;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    uint64_t offset = (file_status_flags[slot] & 0x400ULL) ?
                      file->size : boot_file_offsets[slot];
    if (offset >= TMP_FILE_CAP) return (uint64_t)-ENOSPC;
    if (length > TMP_FILE_CAP - offset) length = TMP_FILE_CAP - offset;
    uint64_t n = 0;
    for (; n < length; n++) {
        uint8_t *src = nas_mmu_user_byte(address + n);
        if (!src) break;
        file->bytes[offset + n] = *src;
    }
    if (!n) return (uint64_t)-EFAULT;
    for (uint64_t pos = file->size; pos < offset; pos++)
        file->bytes[pos] = 0;
    if (offset + n > file->size) file->size = offset + n;
    boot_file_offsets[slot] = offset + n;
    return n;
}
/* Linux syscall decoders retain user-pointer validation and task-relative
 * path resolution; the RAM filesystem backend owns namespace operations. */
static uint64_t sys_mkdirat_tmp(uint64_t dirfd, uint64_t pathname,
                                uint64_t mode) {
    char path[256];
    uint64_t ret = resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_mkdir(path, mode);
}
static uint64_t sys_unlinkat_tmp(uint64_t dirfd, uint64_t pathname,
                                 uint64_t flags) {
    char path[256];
    uint64_t ret = resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_unlink(path, flags);
}
static uint64_t sys_renameat_tmp(uint64_t olddirfd, uint64_t oldptr,
                                 uint64_t newdirfd, uint64_t newptr,
                                 uint64_t flags) {
    /* Reject unsupported flags before reading user paths, as before. */
    if (flags & ~1ULL) return (uint64_t)-EINVAL;
    char oldpath[256], newpath[256];
    uint64_t ret = resolve_path(olddirfd, oldptr, oldpath);
    if ((int64_t)ret < 0) return ret;
    ret = resolve_path(newdirfd, newptr, newpath);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_rename(oldpath, newpath, flags);
}
static uint64_t sys_open_path(const char *path, uint64_t flags) {
    const uint64_t permitted = 3ULL | 0x800ULL | 0x8000ULL |
                               0x10000ULL | 0x20000ULL | 0x80000ULL | 0x100ULL |
                               0x80ULL | /* O_EXCL */
                               0x40ULL | 0x200ULL | 0x400ULL;
    if (flags & ~permitted) return (uint64_t)-EINVAL;
    if (same_string(path, "/dev/tty")) return (uint64_t)-ENXIO;
    uint8_t kind = FD_CLOSED;
    const struct bootfs_entry *entry = 0;
    const struct limine_file *real_file = 0;
    struct nas_tmp_file *tmp_file = 0;
    bool create_tmp = false;
    if (same_string(path, "/dev/null")) kind = FD_NULL;
    else if (same_string(path, "/dev/zero")) kind = FD_ZERO;
    else {
        if (bootfs_mounted()) {
            char actual[256];
            int err = bootfs_resolve(path, !(flags & 0x20000ULL), actual);
            if (!err) {
                entry = bootfs_find(actual);
                if (bootfs_is_link(entry)) return (uint64_t)-ELOOP;
                kind = bootfs_is_dir(entry) ? FD_DIR : FD_ARCHIVE;
            } else if (err != -ENOENT) {
                return (uint64_t)(int64_t)err;
            }
        }
        if (!kind && running_alpine && (real_file = bootfs_lookup(path)))
            kind = FD_BOOTFS; /* Legacy M4 module mapping. */
        if (!kind && bootfs_mounted() && tmp_path(path) &&
            bootfs_find("/tmp")) {
            tmp_file = tmp_find(path);
            if (tmp_file) kind = tmp_file->is_dir ? FD_TMP_DIR : FD_TMP;
            else {
                int parent = tmp_parent_check(path);
                if (parent) return (uint64_t)(int64_t)parent;
                if (flags & 0x40ULL) { kind = FD_TMP; create_tmp = true; }
            }
        }
    }
    if (!kind) return (flags & 0x40ULL) && bootfs_mounted()
        ? (uint64_t)-EROFS : (uint64_t)-ENOENT;
    if ((flags & 0x10000ULL) && kind != FD_DIR && kind != FD_TMP_DIR)
        return (uint64_t)-ENOTDIR;
    if (kind == FD_TMP_DIR && ((flags & 3ULL) || (flags & 0x200ULL)))
        return (uint64_t)-EISDIR;
    if (kind == FD_TMP) {
        if (tmp_file && (flags & 0xc0ULL) == 0xc0ULL)
            return (uint64_t)-EEXIST;
        if ((flags & 0x200ULL) && !(flags & 3ULL))
            return (uint64_t)-EINVAL;
    }
    /* O_CREAT on an existing /dev/null or /dev/zero is not a request
     * to create a new file. O_TRUNC has no effect on character devices.
     * Archive-backed data, however, stays strictly read-only.
     */
    /* O_CREAT means "create if absent", not "write". A pre-existing
     * immutable CPIO regular file can be opened O_RDONLY|O_CREAT.
     * O_CREAT|O_EXCL must refuse an existing name with EEXIST.
     */
    if ((flags & 0xc0ULL) == 0xc0ULL &&
        (kind == FD_ARCHIVE || kind == FD_DIR || kind == FD_BOOTFS))
        return (uint64_t)-EEXIST;
    if (kind != FD_NULL && kind != FD_ZERO && kind != FD_TMP &&
        (flags & 0x200ULL))
        return (uint64_t)-EROFS;
    if (kind == FD_BOOTFS && (flags & (0x40ULL | 0x400ULL)))
        return (uint64_t)-EROFS;
    if ((kind == FD_ARCHIVE || kind == FD_DIR) && (flags & 3ULL))
        return kind == FD_DIR ? (uint64_t)-EISDIR : (uint64_t)-EROFS;
    if (kind == FD_BOOTFS && (flags & 3ULL)) return (uint64_t)-EACCES;
    for (unsigned fd = 3; fd < 64; fd++) {
        if (descriptors[fd] != FD_CLOSED) continue;
        for (unsigned slot = 0; slot < 64; slot++) {
            if (file_refcount[slot]) continue;
            if (create_tmp) {
                tmp_file = tmp_create(path, false);
                if (!tmp_file) return (uint64_t)-ENOSPC;
            }
            if ((kind == FD_TMP || kind == FD_TMP_DIR) && !tmp_file) return (uint64_t)-ENOENT;
            if (kind == FD_TMP && (flags & 0x200ULL)) tmp_file->size = 0;
            file_refcount[slot] = 1;
            if (kind == FD_TMP || kind == FD_TMP_DIR)
                tmp_bind_slot(slot, tmp_file);
            file_slot[fd] = (uint8_t)slot;
            file_status_flags[slot] = flags & (3ULL | 0x800ULL | 0x400ULL);
            fd_cloexec[fd] = !!(flags & 0x80000ULL);
            boot_file_offsets[slot] = 0;
            descriptors[fd] = kind;
            boot_files[fd] = real_file;
            boot_entries[fd] = entry;
            return fd;
        }
        return (uint64_t)-ENFILE;
    }
    return (uint64_t)-EMFILE;
}
static uint64_t sys_open_device(uint64_t pathname, uint64_t flags) {
    char path[256];
    uint64_t result = resolve_path((uint64_t)-100, pathname, path);
    return (int64_t)result < 0 ? result : sys_open_path(path, flags);
}
static uint64_t sys_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED) return (uint64_t)-EBADF;
    if ((file_status_flags[file_slot[fd]] & 3ULL) == 0)
        return (uint64_t)-EBADF;
    if (length > (1ULL << 20) ||
        address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    if (descriptors[fd] == FD_NULL || descriptors[fd] == FD_ZERO) return length;
    if (descriptors[fd] == FD_PIPE_W) return pipe_fd_write(fd, address, length);
    if (descriptors[fd] == FD_TMP) return tmp_fd_write(fd, address, length);
    if (descriptors[fd] == FD_BOOTFS || descriptors[fd] == FD_ARCHIVE ||
        descriptors[fd] == FD_DIR) return (uint64_t)-EBADF; /* O_RDONLY */
    if (descriptors[fd] != FD_UART) return (uint64_t)-EBADF;
    for (uint64_t i = 0; i < length; i++) {
        uint8_t *p = nas_mmu_user_byte(address + i);
        if (!p) return i ? i : (uint64_t)-EFAULT;
        nas_putchar((char)*p);
        if (fd == 1 && (running_bash || running_busybox)) observe_stdout((char)*p);
    }
    return length;
}
static uint64_t sys_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED) return (uint64_t)-EBADF;
    if ((file_status_flags[file_slot[fd]] & 3ULL) == 1)
        return (uint64_t)-EBADF;
    if (descriptors[fd] == FD_NULL) return 0;
    if (descriptors[fd] == FD_PIPE_R) return pipe_fd_read(fd, address, length);
    if (descriptors[fd] == FD_TMP) return tmp_fd_read(fd, address, length);
    if (descriptors[fd] == FD_DIR || descriptors[fd] == FD_TMP_DIR) return (uint64_t)-EISDIR;
    if (descriptors[fd] == FD_ARCHIVE) {
        const struct bootfs_entry *entry = boot_entries[fd];
        if (!entry) return (uint64_t)-EBADF;
        uint64_t start = boot_file_offsets[file_slot[fd]];
        if (start >= entry->size) return 0;
        uint64_t available = entry->size - start;
        if (length > available) length = available;
        if (!nas_user_write_bytes(address, entry->data + start, length))
            return (uint64_t)-EFAULT;
        boot_file_offsets[file_slot[fd]] += length;
        return length;
    }
    if (descriptors[fd] == FD_BOOTFS) {
        const struct limine_file *file = boot_files[fd];
        if (!file || !file->address) return (uint64_t)-EBADF;
        uint64_t start = boot_file_offsets[file_slot[fd]];
        if (start > file->size) return 0;
        uint64_t available = file->size - start;
        if (length > available) length = available;
        if (!nas_user_write_bytes(address, (const uint8_t *)file->address + start,
                              length)) return (uint64_t)-EFAULT;
        boot_file_offsets[file_slot[fd]] += length;
        return length;
    }
    if (descriptors[fd] == FD_UART) return (uint64_t)-EAGAIN; /* no RX-ready: EAGAIN */
    if (descriptors[fd] != FD_ZERO) return (uint64_t)-EBADF;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    uint8_t zero = 0;
    for (uint64_t i = 0; i < length; i++)
        if (!nas_user_write_bytes(address + i, &zero, 1)) return i ? i : (uint64_t)-EFAULT;
    return length;
}
static uint64_t sys_fd_writev(uint64_t fd, uint64_t iov, uint64_t count) {
    if (count > 64) return (uint64_t)-EINVAL;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT - iov) / 16))
        return (uint64_t)-EFAULT;
    uint64_t sent = 0;
    for (uint64_t n = 0; n < count; n++) {
        uint64_t address, bytes;
        if (!read_u64(iov + n * 16, &address) ||
            !read_u64(iov + n * 16 + 8, &bytes))
            return sent ? sent : (uint64_t)-EFAULT;
        uint64_t r = sys_fd_write(fd, address, bytes);
        if ((int64_t)r < 0) return sent ? sent : r;
        sent += r;
        if (r != bytes) break;
    }
    return sent;
}
/* poll() readiness for real in-memory files and available output devices.
 * There is no scheduler/TTY RX yet: unsupported blocking waits return
 * -EOPNOTSUPP rather than pretending that a timeout elapsed.
 * Linux pollfd ABI: int32 fd, int16 events, int16 revents (8 bytes).
 */
static uint64_t sys_poll(uint64_t address, uint64_t nfds, uint64_t timeout) {
    const uint16_t POLLIN = 0x0001, POLLOUT = 0x0004, POLLNVAL = 0x0020;
    if (nfds > 64) return (uint64_t)-EINVAL;
    if (nfds && (address >= USER_LIMIT || nfds > (USER_LIMIT - address) / 8))
        return (uint64_t)-EFAULT;
    uint64_t ready = 0;
    for (uint64_t i = 0; i < nfds; ++i) {
        uint64_t word;
        if (!read_u64(address + i * 8, &word)) return (uint64_t)-EFAULT;
        int32_t fd = (int32_t)(uint32_t)word;
        uint16_t events = (uint16_t)(word >> 32);
        uint16_t revents = 0;
        if (fd >= 0) {
            if (fd >= 64 || descriptors[(unsigned)fd] == FD_CLOSED)
                revents = POLLNVAL;
            else {
                uint8_t kind = descriptors[(unsigned)fd];
                if (kind == FD_ARCHIVE || kind == FD_BOOTFS ||
                    kind == FD_DIR || kind == FD_TMP_DIR || kind == FD_NULL || kind == FD_ZERO ||
                    kind == FD_TMP)
                    revents = events & (POLLIN | POLLOUT);
                else if (kind == FD_UART)
                    revents = events & POLLOUT;
                else if (kind == FD_PIPE_R && nas_pipe_poll_readable(file_slot[(unsigned)fd]))
                    revents = events & POLLIN;
                else if (kind == FD_PIPE_W && nas_pipe_poll_writable(file_slot[(unsigned)fd]))
                    revents = events & POLLOUT;
            }
        }
        if (revents) ready++;
        word = (word & 0x0000ffffffffffffULL) | ((uint64_t)revents << 48);
        if (!write_u64(address + i * 8, word)) return (uint64_t)-EFAULT;
    }
    if (ready || (int64_t)timeout == 0) return ready;
    return (uint64_t)-EOPNOTSUPP; /* no scheduler-backed blocking poll */
}

static uint64_t sys_getcwd_root(uint64_t buffer, uint64_t size) {
    uint64_t length = 0;
    while (current_workdir[length]) length++;
    length++; /* Linux raw getcwd includes NUL in returned length. */
    if (size < length) return (uint64_t)-ERANGE;
    return nas_user_write_bytes(buffer, (const uint8_t *)current_workdir, length)
        ? length : (uint64_t)-EFAULT;
}
static uint64_t sys_chdir(uint64_t pathname) {
    char path[256];
    uint64_t ret = resolve_path((uint64_t)-100, pathname, path);
    if ((int64_t)ret < 0) return ret;
    char actual[256];
    int err = bootfs_resolve(path, true, actual);
    if (err) {
        struct nas_tmp_file *t = tmp_find(path);
        if (!t) return (uint64_t)(int64_t)err;
        if (!t->is_dir) return (uint64_t)-ENOTDIR;
        unsigned n = 0;
        do { current_workdir[n] = path[n]; } while (path[n++]);
        return 0;
    }
    const struct bootfs_entry *entry = bootfs_find(actual);
    if (!bootfs_is_dir(entry)) return (uint64_t)-ENOTDIR;
    unsigned i = 0;
    do { current_workdir[i] = actual[i]; } while (actual[i++]);
    return 0;
}
static uint64_t sys_fchdir(uint64_t fd) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED)
        return (uint64_t)-EBADF;
    const char *path;
    if (descriptors[fd] == FD_TMP_DIR) {
        const struct nas_tmp_file *dir = tmp_by_fd(fd);
        if (!dir || !dir->linked) return (uint64_t)-ENOENT;
        path = dir->path;
    } else {
        if (descriptors[fd] != FD_DIR || !boot_entries[fd])
            return (uint64_t)-ENOTDIR;
        path = boot_entries[fd]->path;
    }
    unsigned i = 0;
    do { current_workdir[i] = path[i]; } while (path[i++]);
    return 0;
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
    return nas_user_write_bytes(buffer, (const uint8_t *)&data, sizeof(data))
        ? 0 : (uint64_t)-EFAULT;
}

/* Linux x86-64 struct stat layout, 144 bytes. File size is actual module.
 * Device entries have character-device type; mounted files are regular.
 */
struct linux_stat {
    uint64_t dev, ino, nlink;
    uint32_t mode, uid, gid, pad;
    uint64_t rdev;
    int64_t size, blksize, blocks;
    int64_t atime_sec, atime_nsec, mtime_sec, mtime_nsec, ctime_sec, ctime_nsec;
    int64_t unused[3];
};
_Static_assert(sizeof(struct linux_stat) == 144, "Linux x86-64 stat ABI");
static uint64_t sys_fstat_boot(uint64_t fd, uint64_t address) {
    if (fd >= 64 || !descriptors[fd]) return (uint64_t)-EBADF;
    struct linux_stat st;
    volatile uint8_t *bytes = (volatile uint8_t *)&st;
    for (unsigned i = 0; i < sizeof(st); i++) bytes[i] = 0;
    st.nlink = 1;
    st.blksize = PAGE;
    if (descriptors[fd] == FD_DIR || descriptors[fd] == FD_ARCHIVE) {
        const struct bootfs_entry *entry = boot_entries[fd];
        if (!entry) return (uint64_t)-EBADF;
        st.dev = 3; st.ino = entry->ino;
        st.mode = entry->mode;
        st.nlink = bootfs_is_dir(entry) ? 2 : 1;
        st.size = (int64_t)entry->size;
        st.blocks = (int64_t)((entry->size + 511) / 512);
    } else if (descriptors[fd] == FD_TMP || descriptors[fd] == FD_TMP_DIR) {
        const struct nas_tmp_file *file = tmp_by_fd(fd);
        if (!file) return (uint64_t)-EBADF;
        st.dev = 5; st.ino = 5000 + tmp_number(file);
        st.mode = file->mode;
        st.nlink = file->linked ? (file->is_dir ? 2 : 1) : 0;
        st.size = (int64_t)file->size;
        st.blocks = (int64_t)((file->size + 511) / 512);
    } else if (descriptors[fd] == FD_PIPE_R || descriptors[fd] == FD_PIPE_W) {
        st.dev = 4; st.ino = (uint64_t)file_slot[fd] + 1000;
        st.mode = 0010600; /* S_IFIFO | owner read/write */
    } else if (descriptors[fd] == FD_BOOTFS) {
        const struct limine_file *file = boot_files[fd];
        if (!file) return (uint64_t)-EBADF;
        st.dev = 1; st.ino = fd + 100;
        st.mode = 0100555;
        st.size = (int64_t)file->size;
        st.blocks = (int64_t)((file->size + 511) / 512);
    } else {
        st.dev = 2; st.ino = fd + 10;
        st.mode = 0020666; /* real character device backed by UART/null/zero */
    }
    if (!nas_user_write_bytes(address, (const uint8_t *)&st, sizeof(st)))
        return (uint64_t)-EFAULT;
    return 0;
}
static uint64_t sys_lseek_boot(uint64_t fd, uint64_t off, uint64_t whence) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED)
        return (uint64_t)-EBADF;
    uint64_t size = 0;
    if (descriptors[fd] == FD_BOOTFS && boot_files[fd])
        size = boot_files[fd]->size;
    else if (descriptors[fd] == FD_ARCHIVE && boot_entries[fd])
        size = boot_entries[fd]->size;
    else if (descriptors[fd] == FD_TMP || descriptors[fd] == FD_TMP_DIR) {
        const struct nas_tmp_file *file = tmp_by_fd(fd);
        if (!file) return (uint64_t)-EBADF;
        size = file->size;
    }
    else if (descriptors[fd] != FD_DIR || !boot_entries[fd])
        return (uint64_t)-ESPIPE;
    uint64_t base = 0;
    if (whence == 1) base = boot_file_offsets[file_slot[fd]];
    else if (whence == 2 && descriptors[fd] != FD_DIR && descriptors[fd] != FD_TMP_DIR) base = size;
    else if (whence != 0) return (uint64_t)-EINVAL;
    int64_t signed_off = (int64_t)off;
    if (signed_off < 0) {
        uint64_t magnitude = 0 - off;
        if (magnitude > base) return (uint64_t)-EINVAL;
        base -= magnitude;
    } else {
        if (base > UINT64_MAX - off) return (uint64_t)-EINVAL;
        base += off;
    }
    boot_file_offsets[file_slot[fd]] = base;
    return base;
}
static uint64_t sys_openat_boot(uint64_t dirfd, uint64_t pathname, uint64_t flags) {
    char path[256];
    uint64_t result = resolve_path(dirfd, pathname, path);
    return (int64_t)result < 0 ? result : sys_open_path(path, flags);
}
static uint64_t write_boot_stat(const struct bootfs_entry *entry, uint64_t dst) {
    struct linux_stat st;
    uint8_t *p = (uint8_t *)&st;
    for (unsigned i = 0; i < sizeof(st); i++) p[i] = 0;
    st.dev = 3;
    st.ino = entry->ino;
    st.nlink = bootfs_is_dir(entry) ? 2 : 1;
    st.mode = entry->mode;
    st.blksize = PAGE;
    st.size = (int64_t)entry->size;
    st.blocks = (int64_t)((entry->size + 511) / 512);
    return nas_user_write_bytes(dst, (const uint8_t *)&st, sizeof(st))
        ? 0 : (uint64_t)-EFAULT;
}
static uint64_t sys_fstatat_boot(uint64_t dirfd, uint64_t pathname,
                                 uint64_t statbuf, uint64_t flags) {
    const uint64_t AT_SYMLINK_NOFOLLOW = 0x100ULL;
    const uint64_t AT_EMPTY_PATH = 0x1000ULL;
    if (flags & ~(AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH))
        return (uint64_t)-EINVAL;
    uint8_t first;
    if (!user_read_bytes(pathname, &first, 1)) return (uint64_t)-EFAULT;
    if (first == 0 && (flags & AT_EMPTY_PATH))
        return sys_fstat_boot(dirfd, statbuf);
    char path[256];
    uint64_t result = resolve_path(dirfd, pathname, path);
    if ((int64_t)result < 0) return result;
    if (bootfs_mounted()) {
        char actual[256];
        int err = bootfs_resolve(path, !(flags & AT_SYMLINK_NOFOLLOW), actual);
        if (!err) return write_boot_stat(bootfs_find(actual), statbuf);
        if (err != -ENOENT) return (uint64_t)(int64_t)err;
    }
    /* Legacy modules and character devices retain their existing fstat. */
    result = sys_open_path(path, 0);
    if ((int64_t)result < 0) return result;
    uint64_t ret = sys_fstat_boot(result, statbuf);
    fd_close_raw(result);
    return ret;
}

/* Linux x86-64 linux_dirent64: ino64, off64, reclen16, dtype8, name[].
 * The offset cookie indexes '.', '..', then direct children of a directory.
 */
/* Linux readlink/readlinkat return the link bytes without a terminating NUL,
 * and deliberately do not follow the last component. */
static uint64_t sys_readlinkat_boot(uint64_t dirfd, uint64_t pathname,
                                     uint64_t dst, uint64_t capacity) {
    if (!capacity) return (uint64_t)-EINVAL;
    char path[256], actual[256];
    uint64_t r = resolve_path(dirfd, pathname, path);
    if ((int64_t)r < 0) return r;
    int err = bootfs_resolve(path, false, actual);
    if (err) return (uint64_t)(int64_t)err;
    const struct bootfs_entry *entry = bootfs_find(actual);
    if (!bootfs_is_link(entry)) return (uint64_t)-EINVAL;
    uint64_t len = entry->size < capacity ? entry->size : capacity;
    return nas_user_write_bytes(dst, entry->data, len) ? len : (uint64_t)-EFAULT;
}

static uint64_t sys_getdents64(uint64_t fd, uint64_t dst, uint64_t capacity) {
    if (fd >= 64 || descriptors[fd] == FD_CLOSED) return (uint64_t)-EBADF;
    if (descriptors[fd] != FD_TMP_DIR &&
        (descriptors[fd] != FD_DIR || !boot_entries[fd]))
        return (uint64_t)-ENOTDIR;
    if (capacity < 24) return (uint64_t)-EINVAL;
    if (dst >= USER_LIMIT || capacity > USER_LIMIT - dst)
        return (uint64_t)-EFAULT;
    const struct bootfs_entry *dir = boot_entries[fd];
    const struct nas_tmp_file *tmpdir =
        descriptors[fd] == FD_TMP_DIR ? tmp_by_fd(fd) : 0;
    const char *dirpath = tmpdir ? tmpdir->path : dir->path;
    uint64_t dirino = tmpdir ? 5000 + tmp_number(tmpdir) : dir->ino;
    uint64_t cursor = boot_file_offsets[file_slot[fd]];
    uint64_t written = 0;
    while (written + 24 <= capacity) {
        const struct bootfs_entry *item;
        const char *name;
        uint64_t ino;
        unsigned dtype;
        if (cursor == 0) {
            name = "."; ino = dirino; dtype = 4;
        } else if (cursor == 1) {
            name = ".."; dtype = 4;
            char parent[256];
            unsigned len = 0;
            while (dirpath[len]) { parent[len] = dirpath[len]; len++; }
            while (len > 1 && parent[len - 1] != '/') len--;
            if (len > 1) len--;
            parent[len] = 0;
            item = bootfs_find(parent);
            struct nas_tmp_file *tparent = tmp_find(parent);
            ino = tparent ? 5000 + tmp_number(tparent) :
                  item ? item->ino : dirino;
        } else if (same_string(dirpath, "/tmp") || tmpdir) {
            if (cursor - 2 >= TMP_FILE_COUNT) break;
            const struct nas_tmp_file *file =
                tmp_child(dirpath, (unsigned)(cursor - 2));
            if (!file) break;
            name = file->path;
            for (const char *p = file->path; *p; p++)
                if (*p == '/') name = p + 1;
            ino = 5000 + tmp_number(file);
            dtype = file->is_dir ? 4 : 8;
        } else {
            if (cursor - 2 > 95) break;
            item = bootfs_child(dir, (unsigned)(cursor - 2));
            if (!item) break;
            name = item->path;
            for (const char *p = item->path; *p; p++)
                if (*p == '/') name = p + 1;
            ino = item->ino;
            dtype = bootfs_is_dir(item) ? 4 : bootfs_is_link(item) ? 10 : 8;
        }
        unsigned len = 0;
        while (name[len]) len++;
        unsigned reclen = (19 + len + 1 + 7) & ~7U;
        if (reclen > capacity - written) {
            if (!written) return (uint64_t)-EINVAL;
            break;
        }
        uint8_t record[280];
        for (unsigned i = 0; i < reclen; i++) record[i] = 0;
        for (unsigned i = 0; i < 8; i++) {
            record[i] = (uint8_t)(ino >> (i * 8));
            record[8 + i] = (uint8_t)((cursor + 1) >> (i * 8));
        }
        record[16] = (uint8_t)reclen;
        record[17] = (uint8_t)(reclen >> 8);
        record[18] = (uint8_t)dtype;
        for (unsigned i = 0; i < len; i++) record[19 + i] = (uint8_t)name[i];
        if (!nas_user_write_bytes(dst + written, record, reclen))
            return written ? written : (uint64_t)-EFAULT;
        written += reclen;
        cursor++;
        boot_file_offsets[file_slot[fd]] = cursor;
    }
    return written;
}

/* Proper syscall-level rt_sigaction / sigprocmask structures, 8-byte sigset. 
 * Delivery is intentionally not claimed until an actual signal path exists. */
static uint64_t sys_sigaction(uint64_t signum, uint64_t new_action,
                              uint64_t old_action, uint64_t size) {
    if (size != 8 || signum < 1 || signum > 64) return (uint64_t)-EINVAL;
    if (new_action && (signum == 9 || signum == 19)) return (uint64_t)-EINVAL;
    if (old_action &&
        !nas_user_write_bytes(old_action, (const uint8_t *)signal_actions[signum], 32))
        return (uint64_t)-EFAULT;
    if (new_action &&
        !user_read_bytes(new_action, (uint8_t *)signal_actions[signum], 32))
        return (uint64_t)-EFAULT;
    return 0;
}
static uint64_t sys_sigprocmask(uint64_t how, uint64_t set,
                                uint64_t previous, uint64_t size) {
    if (size != 8) return (uint64_t)-EINVAL;
    uint64_t newmask = 0;
    if (set && !read_u64(set, &newmask)) return (uint64_t)-EFAULT;
    if (previous && !write_u64(previous, blocked_signals)) return (uint64_t)-EFAULT;
    if (set) {
        if (how == 0) blocked_signals |= newmask;        /* SIG_BLOCK */
        else if (how == 1) blocked_signals &= ~newmask;  /* SIG_UNBLOCK */
        else if (how == 2) blocked_signals = newmask;    /* SIG_SETMASK */
        else return (uint64_t)-EINVAL;
        blocked_signals &= ~((1ULL << 8) | (1ULL << 18));
    }
    return 0;
}

/* Single-process execve: replace this process's user image, not fork.
 * Snapshot ALL user pointers before changing CR3. The ELF and interpreter
 * bytes are immutable CPIO entries, so they remain valid after replacement.
 * Stage image in a fresh root, rollback if load fails; only on success
 * close CLOEXEC descriptors, release old user pages and re-enter Ring 3.
 */
#define EXEC_STRING_MAX 256
static struct exec_snapshot {
    char filename[256];
    char argbuf[NAS_ARGV_MAX][EXEC_STRING_MAX];
    char envbuf[NAS_ENV_MAX][EXEC_STRING_MAX];
    const char *argv[NAS_ARGV_MAX];
    const char *envp[NAS_ENV_MAX];
    unsigned argc, envc;
    /* #! interpreter and its single optional argument survive user CR3
     * replacement; never retain pointers into a CPIO line buffer. */
    char shebang_path[256], shebang_arg[256];
} pending_exec;

/* Linux binfmt_script: interpret a bounded CPIO #! line as an ELF
 * interpreter plus one optional argument. The script itself is NOT mapped
 * as ELF and argv[0] of the original caller is replaced by the interpreter.
 * No recursive shebangs or Linux-binfmt registrations are pretended.
 */
static int read_shebang(const struct elf_image *image,
                        char interpreter[256], char option[256]) {
    if (!image || image->size < 3 || image->data[0] != '#' ||
        image->data[1] != '!') return -ENOEXEC;
    unsigned end = 2;
    while (end < image->size && end < 256 &&
           image->data[end] != '\n' && image->data[end] != 0)
        end++;
    /* Reject a truncated first line rather than guessing an interpreter. */
    if (end == 256 || end == image->size || image->data[end] == 0)
        return -ENOEXEC;
    unsigned i = 2;
    while (i < end && (image->data[i] == ' ' || image->data[i] == '\t'))
        i++;
    unsigned start = i;
    while (i < end && image->data[i] != ' ' && image->data[i] != '\t' &&
           image->data[i] != '\r')
        i++;
    if (i == start || image->data[start] != '/') return -ENOEXEC;
    unsigned n = i - start;
    for (unsigned j = 0; j < n; j++)
        interpreter[j] = (char)image->data[start + j];
    interpreter[n] = 0;
    while (i < end && (image->data[i] == ' ' || image->data[i] == '\t'))
        i++;
    start = i;
    while (end > start && (image->data[end - 1] == ' ' ||
                            image->data[end - 1] == '\t' ||
                            image->data[end - 1] == '\r'))
        end--;
    n = end - start;
    for (unsigned j = 0; j < n; j++)
        option[j] = (char)image->data[start + j];
    option[n] = 0;
    return 0;
}
static uint64_t shebang_rewrite_args(void) {
    /* argv[0] is replaced, not appended. Preserve argv[1..] and envp. */
    const bool option = pending_exec.shebang_arg[0] != 0;
    unsigned original = pending_exec.argc;
    if (original + 1 + (option ? 1U : 0U) > NAS_ARGV_MAX)
        return (uint64_t)-E2BIG;
    const char *rebuilt[NAS_ARGV_MAX];
    unsigned n = 0;
    rebuilt[n++] = pending_exec.shebang_path;
    if (option) rebuilt[n++] = pending_exec.shebang_arg;
    rebuilt[n++] = pending_exec.filename;
    for (unsigned i = 1; i < original; i++)
        rebuilt[n++] = pending_exec.argv[i];
    for (unsigned i = 0; i < n; i++) pending_exec.argv[i] = rebuilt[i];
    pending_exec.argc = n;
    return 0;
}

static uint64_t copy_exec_string(uint64_t addr, char dst[EXEC_STRING_MAX]) {
    if (addr >= USER_LIMIT) return (uint64_t)-EFAULT;
    for (unsigned i = 0; i < EXEC_STRING_MAX; i++) {
        uint8_t ch;
        if (!user_read_bytes(addr + i, &ch, 1)) return (uint64_t)-EFAULT;
        dst[i] = (char)ch;
        if (!ch) return 0;
    }
    return (uint64_t)-E2BIG;
}
static uint64_t copy_exec_vector(uint64_t user_array, bool environment) {
    unsigned limit = environment ? NAS_ENV_MAX : NAS_ARGV_MAX;
    if (!user_array) {
        if (!environment) {
            pending_exec.argv[0] = pending_exec.argbuf[0];
            pending_exec.argbuf[0][0] = 0;
            pending_exec.argc = 1; /* argc>=1 Linux compatibility */
        }
        return 0;
    }
    for (unsigned i = 0; i <= limit; i++) {
        uint64_t value;
        if (user_array >= USER_LIMIT ||
            i > (USER_LIMIT - user_array) / 8 ||
            !read_u64(user_array + i * 8, &value))
            return (uint64_t)-EFAULT;
        if (!value) {
            if (environment) pending_exec.envc = i;
            else {
                pending_exec.argc = i;
                if (!i) {
                    pending_exec.argc = 1;
                    pending_exec.argv[0] = pending_exec.argbuf[0];
                    pending_exec.argbuf[0][0] = 0;
                }
            }
            return 0;
        }
        if (i == limit) return (uint64_t)-E2BIG;
        char *storage = environment ? pending_exec.envbuf[i] : pending_exec.argbuf[i];
        uint64_t ret = copy_exec_string(value, storage);
        if ((int64_t)ret < 0) return ret;
        if (environment) pending_exec.envp[i] = storage;
        else pending_exec.argv[i] = storage;
    }
    return (uint64_t)-E2BIG;
}
static uint64_t sys_execve(uint64_t pathname, uint64_t argv, uint64_t envp) {
    if (!bootfs_mounted()) return (uint64_t)-ENOENT;
    uint64_t result = resolve_path((uint64_t)-100, pathname, pending_exec.filename);
    if ((int64_t)result < 0) return result;
    char actual[256];
    int err = bootfs_resolve(pending_exec.filename, true, actual);
    if (err) return (uint64_t)(int64_t)err;
    const struct bootfs_entry *app = bootfs_find(actual);
    if (bootfs_is_dir(app) || !(app->mode & 0111U))
        return (uint64_t)-EACCES;
    struct elf_image main_img = { app->data, app->size };
    bool scripted = false;
    if (main_img.size >= 2 && main_img.data[0] == '#' &&
        main_img.data[1] == '!') {
        err = read_shebang(&main_img, pending_exec.shebang_path,
                           pending_exec.shebang_arg);
        if (err) return (uint64_t)(int64_t)err;
        char resolved_interpreter[256];
        err = bootfs_resolve(pending_exec.shebang_path, true,
                             resolved_interpreter);
        if (err) return (uint64_t)(int64_t)err;
        const struct bootfs_entry *bin = bootfs_find(resolved_interpreter);
        if (!bin) return (uint64_t)-ENOENT;
        if (bootfs_is_dir(bin) || !(bin->mode & 0111U))
            return (uint64_t)-EACCES;
        main_img = (struct elf_image){ bin->data, bin->size };
        unsigned n = 0;
        do { actual[n] = resolved_interpreter[n]; }
        while (resolved_interpreter[n++]);
        scripted = true;
    }
    /* Scripts are accepted only through an actual executable ELF
     * interpreter; never claim a nested or invalid #! succeeds. */
    if (main_img.size < sizeof(struct elf_hdr) ||
        main_img.data[0] != 0x7f || main_img.data[1] != 'E' ||
        main_img.data[2] != 'L' || main_img.data[3] != 'F')
        return (uint64_t)-ENOEXEC;
    const struct bootfs_entry *loader = bootfs_find("/lib/ld-musl-x86_64.so.1");
    struct elf_image interp_img = {0};
    const struct elf_image *interp = 0;
    if (loader && !bootfs_is_dir(loader)) {
        interp_img = (struct elf_image){loader->data, loader->size};
        interp = &interp_img;
    }
    result = copy_exec_vector(argv, false);
    if ((int64_t)result < 0) return result;
    result = copy_exec_vector(envp, true);
    if ((int64_t)result < 0) return result;
    if (scripted) {
        result = shebang_rewrite_args();
        if ((int64_t)result < 0) return result;
    }
    uint64_t new_root = nas_mmu_alloc_root();
    if (!new_root) return (uint64_t)-ENOMEM;
    uint64_t previous_root = root_phys;
    uint64_t old_heap = heap_break, old_cursor = mmap_cursor;
    uint64_t old_phdr = loaded_phdr, old_phnum = loaded_phnum;
    uint64_t old_entry = loaded_main_entry, old_interp = loaded_interp_base;
    nas_mmu_switch_root(new_root);
    heap_break = 0;
    mmap_cursor = 0x60000000ULL;
    loaded_phdr = loaded_phnum = loaded_main_entry = loaded_interp_base = 0;
    uint64_t entry, sp = 0;
    bool good = elf_load(&main_img, interp, &entry);
    if (good) {
        for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
             va < USER_STACK_TOP; va += PAGE)
            if (!nas_mmu_map_page(va, true)) { good = false; break; }
    }
    if (good)
        good = build_user_stack(pending_exec.argv, pending_exec.argc,
                                pending_exec.envp, pending_exec.envc,
                                pending_exec.filename, &sp);
    if (!good) {
        nas_mmu_switch_root(previous_root);
        if (!nas_mmu_reclaim_root(new_root)) fail("execve rollback page-table corruption");
        heap_break = old_heap;
        mmap_cursor = old_cursor;
        loaded_phdr = old_phdr;
        loaded_phnum = old_phnum;
        loaded_main_entry = old_entry;
        loaded_interp_base = old_interp;
        return (uint64_t)-ENOEXEC;
    }
    /* Commit: syscall can no longer return to old userspace RIP or RSP. */
    for (unsigned fd = 0; fd < 64; fd++)
        if (descriptors[fd] != FD_CLOSED && fd_cloexec[fd])
            fd_close_raw(fd);
    thread_clear_tid = 0;
    user_fs_base = 0;
    nas_arch_set_user_tls(0); /* fresh musl/glibc TLS initialization */
    for (unsigned i = 0; i < 65; i++)
        for (unsigned j = 0; j < 4; j++)
            signal_actions[i][j] = 0; /* exec resets user signal handlers */
    if (!nas_mmu_reclaim_root(previous_root))
        fail("execve stale address-space frame reclaim failure");
    const char *basename = actual;
    for (const char *p = basename; *p; p++)
        if (*p == '/') basename = p + 1;
    running_busybox = same_string(basename, "busybox");
    running_bash = same_string(basename, "bash");
    running_alpine = running_busybox &&
        ((const struct elf_hdr *)main_img.data)->type == 3;
    if (scripted) {
        nas_print("[EXEC] CPIO #! interpreter: ");
        nas_print(pending_exec.shebang_path); nas_print("\n");
    }
    nas_print("[EXEC] replaced process with CPIO ELF: ");
    nas_print(pending_exec.filename);
    nas_print("\n");
    nas_arch_enter_user(entry, sp);
    fail("execve iretq returned unexpectedly");
}


/* M6.4 syscall-boundary cooperative scheduler. All live syscalls unwind on
 * the same kernel stack. Parked syscalls are re-issued ONLY after a wake
 * condition is true and the task's own CR3 and FS state are restored.
 * Thus a switched task always receives the real read/write/wait4 result;
 * a fabricated success or USER RIP rewind would corrupt Linux userspace.
 */
static bool task_io_ready(const struct nas_task *task) {
    uint64_t fd = task->blocked_args[0];
    uint8_t kind = nas_task_fd_kind(task, fd);
    unsigned slot = nas_task_fd_slot(task, fd);
    if (slot >= NAS_FD_LIMIT) return true; /* replay to return EBADF */
    if (task->blocked_nr == __NR_read)
        return kind != FD_PIPE_R || nas_pipe_read_ready(slot);
    if (task->blocked_nr == __NR_write ||
        task->blocked_nr == __NR_writev)
        return kind != FD_PIPE_W || nas_pipe_write_ready(slot);
    return true;
}
static bool task_wait_ready(const struct nas_task *task) {
    uint64_t desired = task->blocked_args[0];
    for (unsigned slot = 0; slot < NAS_TASK_LIMIT; slot++) {
        struct nas_task *child = nas_task_child_at(task->pid, slot);
        if (!child) continue;
        if (desired != UINT64_MAX && desired != child->pid) continue;
        if (child->state == NAS_TASK_ZOMBIE) return true;
    }
    return false;
}
static bool task_can_wake(const struct nas_task *task) {
    if (task->state != NAS_TASK_BLOCKED || !task->resume_valid ||
        !task->pending_valid) return false;
    if (task->blocked_nr == __NR_wait4) return task_wait_ready(task);
    if (task->blocked_nr == __NR_read ||
        task->blocked_nr == __NR_write ||
        task->blocked_nr == __NR_writev)
        return task_io_ready(task);
    return false;
}
static uint64_t task_replay(struct nas_task *task) {
    uint64_t nr = task->blocked_nr;
    const uint64_t *a = task->blocked_args;
    if (nr == __NR_read) return sys_fd_read(a[0], a[1], a[2]);
    if (nr == __NR_write) return sys_fd_write(a[0], a[1], a[2]);
    if (nr == __NR_writev) return sys_fd_writev(a[0], a[1], a[2]);
    if (nr == __NR_wait4) return nas_process_wait4(a[0], a[1], a[2], a[3]);
    fail("scheduler unknown blocked syscall");
}

/* Broken pipe generates SIGPIPE. POSIX/Linux default action terminates the
 * writer; returning EPIPE alone is insufficient (BusyBox yes retries forever).
 * Blocked or ignored SIGPIPE returns EPIPE to userspace as Linux does.
 * A custom userspace handler requires signal-frame delivery (not yet here).
 */
#define NAS_SIGPIPE 13
static bool sigpipe_default_on_fd(uint64_t fd) {
    return TASK.ppid && fd < NAS_FD_LIMIT &&
        descriptors[fd] == FD_PIPE_W &&
        signal_actions[NAS_SIGPIPE][0] == 0 &&
        !(blocked_signals & (1ULL << (NAS_SIGPIPE - 1)));
}
static void task_mark_child_exit(uint32_t code, uint32_t signum) {
    struct nas_task *child = nas_current_task;
    if (!child->ppid || !nas_task_by_pid(child->ppid))
        fail("child termination without living parent");
    child->exit_code = code & 255U;
    child->termination_signal = signum;
    child->state = NAS_TASK_ZOMBIE;
    child->pending_valid = false;
    child->blocked_nr = 0;
    for (unsigned fd = 0; fd < NAS_FD_LIMIT; fd++)
        if (descriptors[fd] != FD_CLOSED) fd_close_raw(fd);
    if (signum) {
        nas_print("[SIGNAL] default SIGPIPE terminated pid=");
        nas_dec(child->pid);
        nas_print("\n");
    } else {
        nas_print("[PROC] child exited pid="); nas_dec(child->pid);
        nas_print(" status="); nas_dec(child->exit_code);
        nas_print("; parent pid="); nas_dec(child->ppid);
        nas_print("\n");
    }
}
static unsigned schedule_count;
static uint64_t task_schedule(uint64_t *frame) {
retry_schedule: ; /* label must precede a statement in C11 */
    struct nas_task *previous = nas_current_task;
    uint64_t previous_pid = previous->pid; /* zombie may be reaped on replay */
    /* A READY task always gets one turn before re-evaluating blocked tasks.
     * This allows ash to construct consumers after a producer fills its pipe.
     */
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (unsigned slot = 0; slot < NAS_TASK_LIMIT; ++slot) {
            struct nas_task *next = nas_task_at(slot);
            if (next == previous || !next->resume_valid) continue;
            if (pass == 0 && next->state != NAS_TASK_READY) continue;
            if (pass == 1 && !task_can_wake(next)) continue;
            nas_current_task = next;
            nas_arch_restore_cpu(next);
            uint64_t result = next->arch.resume_regs[0];
            if (next->pending_valid) {
                result = task_replay(next);
                if ((int64_t)result == -EPIPE &&
                    (next->blocked_nr == __NR_write ||
                     next->blocked_nr == __NR_writev) &&
                    sigpipe_default_on_fd(next->blocked_args[0])) {
                    task_mark_child_exit(0, NAS_SIGPIPE);
                    goto retry_schedule; /* parent can now observe zombie */
                }
                if ((int64_t)result == -EOPNOTSUPP &&
                    (next->blocked_nr == __NR_read ||
                     next->blocked_nr == __NR_write ||
                     next->blocked_nr == __NR_writev)) {
                    next->state = NAS_TASK_BLOCKED;
                    nas_current_task = previous;
                    nas_arch_restore_cpu(previous);
                    continue;
                }
                next->pending_valid = false;
                next->blocked_nr = 0;
            }
            next->state = NAS_TASK_RUNNING;
            next->resume_valid = false;
            for (unsigned i = 0; i < 15; i++)
                frame[i] = next->arch.resume_regs[i];
            syscall_user_rsp = next->arch.resume_user_rsp;
            if (schedule_count++ < 48) {
                nas_print("[SCHED] from="); nas_dec(previous_pid);
                nas_print(" to="); nas_dec(next->pid); nas_print("\n");
            }
            return result;
        }
    }
    /* Fatal deadlock diagnostic: dump who holds each pipe endpoint. */
    for (unsigned i = 0; i < NAS_PIPE_COUNT; i++) {
        unsigned count = 0;
        bool readers = false, writers = false;
        if (!nas_pipe_snapshot(i, &count, &readers, &writers)) continue;
        nas_print("[DEADLOCK] pipe="); nas_dec(i);
        nas_print(" bytes="); nas_dec(count);
        nas_print(" readers="); nas_dec(readers);
        nas_print(" writers="); nas_dec(writers);
        nas_print("\n");
    }
    for (unsigned i = 0; i < NAS_TASK_LIMIT; i++) {
        struct nas_task *task = nas_task_at(i);
        if (task->state == NAS_TASK_FREE) continue;
        nas_print("[DEADLOCK] pid="); nas_dec(task->pid);
        nas_print(" state="); nas_dec(task->state);
        nas_print(" blocked_nr="); nas_dec(task->blocked_nr);
        nas_print(" fd="); nas_dec(task->blocked_args[0]);
        nas_print("\n");
        for (unsigned fd = 0; fd < NAS_FD_LIMIT; fd++) {
            uint8_t kind = nas_task_fd_kind(task, fd);
            if (kind != FD_PIPE_R && kind != FD_PIPE_W) continue;
            unsigned slot = nas_task_fd_slot(task, fd);
            nas_print("[DEADLOCK] holder pid="); nas_dec(task->pid);
            nas_print(" fd="); nas_dec(fd);
            nas_print(" kind="); nas_dec(kind);
            nas_print(" slot="); nas_dec(slot);
            nas_print("\n");
        }
    }
    fail("scheduler deadlock: all live tasks blocked; no ready process");
}
static uint64_t task_block(uint64_t *frame, uint64_t nr,
                           uint64_t a, uint64_t b, uint64_t c,
                           uint64_t d) {
    struct nas_task *task = nas_current_task;
    for (unsigned i = 0; i < 15; i++) task->arch.resume_regs[i] = frame[i];
    task->arch.resume_user_rsp = syscall_user_rsp;
    task->resume_valid = true;
    task->blocked_nr = nr;
    task->pending_valid = true;
    task->blocked_args[0] = a;
    task->blocked_args[1] = b;
    task->blocked_args[2] = c;
    task->blocked_args[3] = d;
    task->state = NAS_TASK_BLOCKED;
    nas_arch_save_fpu(task);
    if (schedule_count < 48) {
        nas_print("[SCHED] blocked pid="); nas_dec(task->pid);
        nas_print(" syscall="); nas_dec(nr); nas_print("\n");
    }
    return task_schedule(frame);
}
static uint64_t task_pipe_io(uint64_t *frame, uint64_t nr,
                             uint64_t fd, uint64_t buf, uint64_t count) {
    uint64_t ret;
    if (nr == __NR_read) ret = sys_fd_read(fd, buf, count);
    else if (nr == __NR_write) ret = sys_fd_write(fd, buf, count);
    else ret = sys_fd_writev(fd, buf, count);
    if ((int64_t)ret == -EPIPE &&
        (nr == __NR_write || nr == __NR_writev) &&
        sigpipe_default_on_fd(fd)) {
        task_mark_child_exit(0, NAS_SIGPIPE);
        return task_schedule(frame);
    }
    if ((int64_t)ret == -EOPNOTSUPP && fd < NAS_FD_LIMIT &&
        (descriptors[fd] == FD_PIPE_R || descriptors[fd] == FD_PIPE_W) &&
        !(file_status_flags[file_slot[fd]] & 0x800ULL))
        return task_block(frame, nr, fd, buf, count, 0);
    return ret;
}

/* Strict single-program ABI diagnostic dispatcher, for a real syscall gate. */
uint64_t syscall_dispatch(const uint64_t *args) {
    uint64_t nr = args[0];
    /* Real-program execution trace: syscall identity, callsite and args.
     * Capped to avoid serial storms; not needed in exception context.
     */
#ifdef NASEED_SYSCALL_TRACE
    static unsigned trace_count;
    if ((running_bash || running_busybox) && trace_count++ < 96) {
        nas_print("[TRACE] nr="); nas_dec(nr);
        nas_print(" rip="); nas_hex(args[7]);
        nas_print(" rsp="); nas_hex(syscall_user_rsp);
        nas_print(" a1="); nas_hex(args[1]);
        nas_print(" a2="); nas_hex(args[2]);
        nas_print(" a3="); nas_hex(args[3]);
        nas_print("\n");
    }
#endif
    if (nr == __NR_arch_prctl) return sys_arch_prctl(args[1], args[2]);
    if (nr == __NR_set_tid_address) { thread_clear_tid = args[1]; return TASK.pid; }
    if (nr == __NR_getpid || nr == __NR_gettid) return TASK.pid;
    if (nr == __NR_getppid) return TASK.ppid;
    if (nr == __NR_getuid || nr == __NR_getgid || nr == __NR_geteuid || nr == __NR_getegid) return 0; /* root credentials */
    if (nr == __NR_mprotect) return nas_vm_mprotect(args[1], args[2], args[3]);
    if (nr == __NR_mmap) return nas_vm_anon_mmap(args[1], args[2], args[3],
                                       args[4], args[5], args[6]);
    if (nr == __NR_munmap) return nas_vm_munmap(args[1], args[2]);
    if (nr == __NR_brk) return nas_vm_brk(args[1]);
    if (nr == __NR_read) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == __NR_write) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == __NR_rename)
        return sys_renameat_tmp((uint64_t)-100,args[1],
                                (uint64_t)-100,args[2],0);
    if (nr == __NR_renameat)
        return sys_renameat_tmp(args[1],args[2],args[3],args[4],0);
    if (nr == __NR_renameat2)
        return sys_renameat_tmp(args[1],args[2],args[3],args[4],args[5]);
    if (nr == __NR_mkdir) return sys_mkdirat_tmp((uint64_t)-100,args[1],args[2]);
    if (nr == __NR_mkdirat) return sys_mkdirat_tmp(args[1],args[2],args[3]);
    if (nr == __NR_unlink) return sys_unlinkat_tmp((uint64_t)-100,args[1],0);
    if (nr == __NR_rmdir) return sys_unlinkat_tmp((uint64_t)-100,args[1],0x200ULL);
    if (nr == __NR_unlinkat) return sys_unlinkat_tmp(args[1],args[2],args[3]);
    if (nr == __NR_open) return sys_open_device(args[1], args[2]);
    if (nr == __NR_openat) return sys_openat_boot(args[1], args[2], args[3]);
    if (nr == __NR_fstat) return sys_fstat_boot(args[1], args[2]);
    if (nr == __NR_newfstatat) return sys_fstatat_boot(args[1], args[2], args[3], args[4]);
    if (nr == __NR_stat) return sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0);
    if (nr == __NR_lstat) return sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0x100ULL);
    if (nr == __NR_lseek) return sys_lseek_boot(args[1], args[2], args[3]);
    if (nr == __NR_getdents64) return sys_getdents64(args[1], args[2], args[3]);
    if (nr == __NR_readlink) return sys_readlinkat_boot((uint64_t)-100, args[1], args[2], args[3]);
    if (nr == __NR_readlinkat) return sys_readlinkat_boot(args[1], args[2], args[3], args[4]);
    if (nr == __NR_close) {
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
            return (uint64_t)-EBADF;
        fd_close_raw(args[1]);
        return 0;
    }
    if (nr == __NR_ioctl) {
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
            return (uint64_t)-EBADF;
        return (uint64_t)-ENOTTY; /* ENOTTY: no TTY driver yet */
    }
    if (nr == __NR_writev) return task_pipe_io((uint64_t *)(uintptr_t)args, nr, args[1], args[2], args[3]);
    if (nr == __NR_rt_sigaction) return sys_sigaction(args[1], args[2], args[3], args[4]);
    if (nr == __NR_rt_sigprocmask) return sys_sigprocmask(args[1], args[2], args[3], args[4]);
    if (nr == __NR_uname) return sys_uname_naseed(args[1]);
    if (nr == __NR_getcwd) return sys_getcwd_root(args[1], args[2]);
    if (nr == __NR_fork) return nas_process_fork((uint64_t *)(uintptr_t)args);
    if (nr == __NR_pipe) return sys_pipe2(args[1], 0);
    if (nr == __NR_pipe2) return sys_pipe2(args[1], args[2]);
    if (nr == __NR_wait4) {
        uint64_t ret = nas_process_wait4(args[1], args[2], args[3], args[4]);
        if ((int64_t)ret == -EOPNOTSUPP && args[4] == 0 &&
            args[3] == 0 && ((int64_t)args[1] == -1 || (int64_t)args[1] > 0))
            return task_block((uint64_t *)(uintptr_t)args, nr,
                              args[1], args[2], args[3], args[4]);
        return ret;
    }
    if (nr == __NR_execve) return sys_execve(args[1], args[2], args[3]);
    if (nr == __NR_fcntl) return sys_fcntl(args[1], args[2], args[3]);
    if (nr == __NR_poll) return sys_poll(args[1], args[2], args[3]);
    if (nr == __NR_dup) return fd_duplicate(args[1], 0, false);
    if (nr == __NR_dup2) return fd_clone(args[1], args[2], false);
    if (nr == __NR_dup3) {
        if (args[1] == args[2] || (args[3] & ~0x80000ULL))
            return (uint64_t)-EINVAL;
        return fd_clone(args[1], args[2], (args[3] & 0x80000ULL) != 0);
    }
    if (nr == __NR_chdir) return sys_chdir(args[1]);
    if (nr == __NR_fchdir) return sys_fchdir(args[1]);
    if (nr == __NR_getpgid) return args[1] == 0 || args[1] == TASK.pid ? TASK.pid : (uint64_t)-ESRCH;
    if (nr == __NR_getpgrp) return TASK.pid; /* one process group today */
    if (nr == __NR_exit || nr == __NR_exit_group) { /* exit / exit_group */
        if (TASK.ppid) {
            task_mark_child_exit((uint32_t)args[1], 0);
            return task_schedule((uint64_t *)(uintptr_t)args);
        }
        TASK.exit_code = (uint32_t)(args[1] & 255ULL);
        TASK.termination_signal = 0;
        TASK.state = NAS_TASK_ZOMBIE;
        nas_print("[EXIT] user status="); nas_dec(args[1]); nas_print("\n");
        /* In cmdline-driven tests, the host verifies real stdout and status.
         * Do not encode applet-specific success checks into the kernel.
         * Legacy M0-M4 tests keep their strict hello-specific acceptance.
         */
        if (boot_argv_override) {
            if (args[1] != 0) fail("cmdline userspace exited nonzero");
            nas_print("[PASS] cmdline userspace exit 0\n");
            nas_platform_debug_exit(0);
            fail("QEMU debug exit returned unexpectedly");
        }
        if (running_busybox) {
            if (args[1] == 0 && saw_bash_hello) {
                if (running_alpine)
                    nas_print("[PASS] M4 Alpine dynamic BusyBox echo hello\n");
                else
                    nas_print("[PASS] M2 Debian BusyBox echo hello\n");
                nas_platform_debug_exit(0);
            }
            fail("real BusyBox exited without stdout hello and status 0");
        }
        if (running_bash) {
            if (args[1] == 0 && saw_bash_hello) {
                nas_print("[PASS] M2 real GNU Bash -c echo hello\n");
                nas_platform_debug_exit(0);
            }
            fail("real Bash exited without exact hello and status zero");
        }
        if (args[1] == 0) {
            nas_print("[PASS] M1 ELF / ring3 / syscall write / ENOSYS / exit\n");
            nas_platform_debug_exit(0);
        }
        fail("user program exit was non-zero");
    }
    if (nr >= sizeof(missing_count) / sizeof(missing_count[0]) ||
        ++missing_count[nr] <= 3) {
        nas_print("[MISS] syscall nr="); nas_dec(nr); nas_print("\n");
    }
    return (uint64_t)-ENOSYS;
}

void m1_start(void) {
    nas_print("[M1] setting up GDT, TSS, IDT, syscall MSRs\n");
    nas_task_boot_init();
    nas_arch_cpu_tables_init();
    nas_arch_cpu_user_fp_init();
    if (!nas_mmu_bootstrap()) fail("requires four-level paging");
    nas_arch_syscall_enable();
    /* Select the boot ELF with the very same argv[0] passed to userspace.
     * No shell, PATH lookup, or alternative program-name ABI is invented.
     * Legacy M0-M5 configurations without cmdline preserve their defaults.
     */
    extern const char *nas_cmdline(void);
    const char *cmdline = nas_cmdline();
    if (cmdline && *cmdline) {
        const char *const *args;
        unsigned argc;
        if (!parse_boot_argv(cmdline, &args, &argc))
            fail("invalid Limine naseed.argv configuration");
    }
    /* File-system bytes and the selected executable are separate:
     * boot_image.c recognizes the Limine handoff and indexes CPIO via
     * bootfs.c; only this layer maps the actual ELF into userspace.
     */
    struct nas_boot_image selected_image;
    const char *boot_error = nas_boot_prepare(
        boot_argv_override ? boot_argv[0] : 0, &selected_image);
    if (boot_error) fail(boot_error);
    struct elf_image main_image = {
        selected_image.image, selected_image.size
    };
    struct elf_image interp_image = {
        selected_image.interpreter, selected_image.interpreter_size
    };
    const struct elf_image *interpreter =
        selected_image.has_interpreter ? &interp_image : 0;
    running_bash = selected_image.bash;
    running_busybox = selected_image.busybox;
    running_alpine = selected_image.alpine;
    uint64_t entry = 0;
    if (!elf_load(&main_image, interpreter, &entry))
        fail("invalid or unmappable userspace ELF");
    for (uint64_t va = USER_STACK_TOP - USER_STACK_PAGES * PAGE;
         va < USER_STACK_TOP; va += PAGE) {
        if (!nas_mmu_map_page(va, true)) fail("cannot map user stack");
    }
    uint64_t stack_pointer;
    for (unsigned fd = 0; fd < 3; fd++) {
        descriptors[fd] = FD_UART;
        file_slot[fd] = (uint8_t)fd;
        file_refcount[fd] = 1;
        file_status_flags[fd] = fd == 0 ? 0 : 1; /* stdin R, stdout/err W */
    }
    if (!build_linux_stack(running_bash, running_busybox, &stack_pointer))
        fail("cannot initialize Linux userspace stack");
    nas_print("[ELF] userspace entry="); nas_hex(entry); nas_print("\n");
    if (running_bash) nas_print("[M2] entering real Bash in ring 3\n");
    else if (running_alpine) nas_print("[M4] real Alpine busybox via musl interpreter in ring 3\n");
    else if (running_busybox) nas_print("[M2] entering real BusyBox in ring 3\n");
    else nas_print("[M1] entering actual ring 3\n");
    nas_arch_enter_user(entry, stack_pointer);
    fail("iretq returned");
}

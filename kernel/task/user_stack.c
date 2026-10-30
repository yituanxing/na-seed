#include <stdint.h>
#include "../arch/abi.h"
#include <stdbool.h>
#include "../task.h"
#include "../arch/arch.h"
#include "../mm/frame.h"
#include "../random.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "../lib/string.h"
#include "elf.h"
#include "user_stack.h"
extern void nas_print(const char *);
#define TASK (*nas_current_task)
#define loaded_phdr TASK.loaded_phdr
#define loaded_phnum TASK.loaded_phnum
#define loaded_main_entry TASK.loaded_main_entry
#define loaded_interp_base TASK.loaded_interp_base
#define USER_STACK_TOP NAS_USER_STACK_TOP

/* One architecture-neutral initial main-thread stack mapping policy.
 * x86-64, RV64 and ARM64 use the SAME page owner and physical allocator.
 * execve already owns a disposable new address-space root on failure. */
bool nas_user_stack_map_initial(void) {
    for(uint64_t va=USER_STACK_TOP-NAS_USER_STACK_PAGES*PAGE;
        va<USER_STACK_TOP;va+=PAGE)
        if(!nas_mmu_map_page(va,true))return false;
    return nas_vma_record(&TASK.mm->mmap_vmas,
                          USER_STACK_TOP-NAS_USER_STACK_PAGES*PAGE,
                          NAS_USER_STACK_PAGES*PAGE,3,NAS_VMA_STACK,0);
}

/* Share one kernel-BSS argument pointer staging area. A 2048-argument
 * stack must never allocate tens of KiB on the privileged kernel stack.
 * Current kernel syscall execution is serialized on its one CPU. */
static struct {
    uint64_t arg_ptrs[NAS_ARGV_MAX];
    uint64_t env_ptrs[NAS_ENV_MAX];
    uint64_t words[NAS_ARGV_MAX + NAS_ENV_MAX + 3 + 28];
} nas_exec_stack_scratch;
/* Shared Linux ELF64 argc/argv/envp/auxv stack builder, not argv spoofing. */
static bool put_stack_string(uint64_t *sp, const char *str, uint64_t *address) {
    uint64_t size = strlen(str) + 1;
    if (*sp < USER_STACK_TOP - NAS_USER_STACK_PAGES * PAGE + size) return false;
    *sp -= size;
    if (!nas_user_copy_to_mapped(*sp, (const uint8_t *)str, size)) return false;
    *address = *sp;
    return true;
}
/* A boot test selects user argv via Limine's executable command line.
 * Kept deliberately independent of filesystem paths and kernel program names.
 * Quotes/backslash group arguments; this is NOT a shell or a second ABI.
 * Missing cmdline means legacy M0-M4 regression argv; malformed cmdline fails.
 */
/* Only the bootloader-selected *initial* argv needs this BSS staging area.
 * A genuine `sh -c '<multi-command script>'` or dynamic-linker smoke can
 * easily exceed the old 256-byte limit. Keep it distinct from Linux's
 * execve ARG_MAX and the 1-MiB actual user stack mapping: the latter still
 * validates its real pointer/string placement through put_stack_string().
 * 4096 bytes, including each argument's NUL, remain bounded in kernel BSS. */
#define NAS_BOOT_ARGV_STORAGE_BYTES 4096U
static char boot_argv_chars[NAS_BOOT_ARGV_STORAGE_BYTES];
static const char *boot_argv[NAS_ARGV_MAX];
static bool boot_argv_override;
static unsigned boot_argc;
bool nas_parse_boot_argv(const char *cmdline,
                            const char *const **out_args, unsigned *out_argc) {
    static const char prefix[] = "naseed.argv=";
    /* A failed second parse must never expose pointers to partially
     * overwritten strings from an earlier successful boot-argv parse. */
    boot_argv_override = false;
    boot_argc = 0;
    if (out_args) *out_args = 0;
    if (out_argc) *out_argc = 0;
    if (!cmdline || !out_args || !out_argc) return false;
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
bool nas_build_user_stack(const char *const *args, unsigned argc,
                             const char *const *vars, unsigned envc,
                             const char *execfn, uint64_t *out_sp) {
    uint64_t sp = USER_STACK_TOP;
    uint64_t *arg_ptrs = nas_exec_stack_scratch.arg_ptrs;
    uint64_t *env_ptrs = nas_exec_stack_scratch.env_ptrs;
    uint64_t execfn_ptr;
    if (argc > NAS_ARGV_MAX || envc > NAS_ENV_MAX) return false;
    uint64_t tsc = nas_arch_cycle_counter();
    /* Noncryptographic boot seed for AT_RANDOM. Add real entropy later. */
    uint64_t random_bytes[2] = {
        tsc ^ (uint64_t)(uintptr_t)&nas_build_user_stack,
        (tsc << 17) ^ nas_hhdm_offset()
    };
    /* Improve AT_RANDOM with the same boot-seeded stream when available;
     * the documented insecure legacy fallback is not getrandom success. */
    (void)nas_random_bytes((uint8_t *)random_bytes,sizeof(random_bytes));
    sp -= sizeof(random_bytes);
    if (!nas_user_copy_to_mapped(sp, (const uint8_t *)random_bytes,
                      sizeof(random_bytes))) return false;
    uint64_t random_address = sp;
    if (!put_stack_string(&sp, execfn, &execfn_ptr)) return false;
    for (unsigned i = 0; i < envc; i++)
        if (!put_stack_string(&sp, vars[i], &env_ptrs[i])) return false;
    for (unsigned i = 0; i < argc; i++)
        if (!put_stack_string(&sp, args[i], &arg_ptrs[i])) return false;

    /* argc | argv[] NULL | envp[] NULL | auxv pairs | AT_NULL */
    /* argc + argv[] + NULL + env[] + NULL + 14 ELF64 auxiliary pairs.
     * Keeping this array tied to the ABI's validated vector capacities
     * prevents corruption when genuine linker recipes exceed 12 args. */
    uint64_t *words = nas_exec_stack_scratch.words;
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
    if (sp < USER_STACK_TOP - NAS_USER_STACK_PAGES * PAGE + byte_count + 16)
        return false;
    sp = (sp - byte_count) & ~15ULL;
    if (!nas_user_copy_to_mapped(sp, (const uint8_t *)words, byte_count)) return false;
    *out_sp = sp;
    return true;
}
bool nas_build_linux_stack(bool bash, bool busybox, uint64_t *out_sp) {
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
    if(!nas_build_user_stack(args, argc, vars, 4, args[0], out_sp))
        return false;
    nas_task_set_commandline(nas_current_task,args,argc);
    return true;
}
bool nas_boot_argv_enabled(void) { return boot_argv_override; }
const char *nas_boot_argv_first(void) { return boot_argv_override ? boot_argv[0] : 0; }

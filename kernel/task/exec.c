#include <stdint.h>
#include "../arch/abi.h"
#include <stdbool.h>
#include "../errno.h"
#include "../bootfs.h"
#include "../fs/lookup.h"
#include "../tmpfs.h"
#include "../task.h"
#include "../fd.h"
#include "../arch/arch.h"
#include "../arch/mmu.h"
#include "../mm/usercopy.h"
#include "../fs/syscall.h"
#include "../fs/io.h"
#include "../drivers/serial.h"
#include "../panic.h"
#include "elf.h"
#include "user_stack.h"
#include "../lib/string.h"
#include "exec.h"
#include "../time/clock.h"
#include "process.h"
#include "signal.h"
#define TASK (*nas_current_task)
#define root_phys TASK.root_phys
#define loaded_phdr TASK.loaded_phdr
#define loaded_phnum TASK.loaded_phnum
#define loaded_main_entry TASK.loaded_main_entry
#define loaded_interp_base TASK.loaded_interp_base
#define thread_clear_tid TASK.thread_clear_tid
#define descriptors TASK.fds->descriptors
#define fd_cloexec TASK.fds->fd_cloexec
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine

/* Boot/exec program-name comparison. FD path matching lives in fs/syscall.c. */
/* Snapshot all user pointers before changing CR3. ELF bytes are
 * kernel-owned (immutable CPIO or tmpfs inode), valid across the staged
 * root switch. Map through the existing shared ELF loader, commit only
 * when successful, and roll back without invalidating the old process. */
/* Real upstream BusyBox Kbuild invokes /bin/sh -c with command strings
 * exceeding the former 1023-byte bound on every ISA; the native GNU Make
 * and shell were spuriously rejected with E2BIG.
 *
 * Keep an explicit per-string cap (8191 bytes), max 64 argv and
 * 128 env entries. Actual strings and pointers are checked against the
 * same 1-MiB mapped exec stack in nas_build_user_stack; these are bounded
 * interim Linux ABI limits, not a claim of full Linux ARG_MAX support.
 * Exec snapshots remain kernel-owned until the new VM is committed. */
#define EXEC_STRING_MAX 8192
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

/* The executable source is owned by its filesystem, not by an ISA-
 * specific ELF loader. Immutable CPIO stays zero-copy, writable tmpfs is
 * read through the same sparse inode API as normal file descriptors. */
static bool tmp_elf_read(const void *context, uint64_t offset,
                         uint8_t *destination, uint64_t length) {
    const struct nas_tmp_file *file = (const struct nas_tmp_file *)context;
    return tmp_read_at(file, offset, destination, length) == (int64_t)length;
}
static struct elf_image elf_from_fs_node(const struct nas_fs_node *node) {
    struct elf_image image = {.data = node->data, .size = node->size};
    if (node->backend == NAS_FS_TMPFS) {
        image.read_at = tmp_elf_read;
        image.context = node->tmp;
    }
    return image;
}

/* Linux binfmt_script: interpret a bounded CPIO #! line as an ELF
 * interpreter plus one optional argument. The script itself is NOT mapped
 * as ELF and argv[0] of the original caller is replaced by the interpreter.
 * No recursive shebangs or Linux-binfmt registrations are pretended.
 */
static int read_shebang(const struct elf_image *image,
                        char interpreter[256], char option[256]) {
    uint8_t line[256];
    if (!image || image->size < 3) return -ENOEXEC;
    uint64_t nread = image->size < sizeof(line) ? image->size : sizeof(line);
    if (!nas_elf_image_read(image, 0, line, nread) ||
        line[0] != '#' || line[1] != '!') return -ENOEXEC;
    unsigned end = 2;
    while (end < image->size && end < 256 &&
           line[end] != '\n' && line[end] != 0)
        end++;
    /* Reject a truncated first line rather than guessing an interpreter. */
    if (end == 256 || end == image->size || line[end] == 0)
        return -ENOEXEC;
    unsigned i = 2;
    while (i < end && (line[i] == ' ' || line[i] == '\t'))
        i++;
    unsigned start = i;
    while (i < end && line[i] != ' ' && line[i] != '\t' &&
           line[i] != '\r')
        i++;
    if (i == start || line[start] != '/') return -ENOEXEC;
    unsigned n = i - start;
    for (unsigned j = 0; j < n; j++)
        interpreter[j] = (char)line[start + j];
    interpreter[n] = 0;
    while (i < end && (line[i] == ' ' || line[i] == '\t'))
        i++;
    start = i;
    while (end > start && (line[end - 1] == ' ' ||
                            line[end - 1] == '\t' ||
                            line[end - 1] == '\r'))
        end--;
    n = end - start;
    for (unsigned j = 0; j < n; j++)
        option[j] = (char)line[start + j];
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
    /* Only ONE page-table lookup for each 4-KiB user page. As with the
     * generic usercopy implementation, HHDM memory is contiguous within
     * one translated leaf, never across adjacent user virtual pages.
     * We still validate every page and NUL-terminate before exec commit. */
    uint64_t available = USER_LIMIT - addr;
    for (unsigned i = 0; i < EXEC_STRING_MAX;) {
        if ((uint64_t)i >= available) return (uint64_t)-EFAULT;
        uint64_t at = addr + i;
        const uint8_t *source = nas_mmu_user_byte(at);
        if (!source) return (uint64_t)-EFAULT;
        unsigned chunk = (unsigned)(PAGE - (at & (PAGE - 1)));
        if (chunk > EXEC_STRING_MAX - i) chunk = EXEC_STRING_MAX - i;
        if ((uint64_t)chunk > available - i) chunk = (unsigned)(available - i);
        for (unsigned j = 0; j < chunk; j++) {
            char byte = (char)source[j];
            dst[i + j] = byte;
            if (!byte) return 0;
        }
        i += chunk;
    }
    nas_print("[EXEC ARG LIMIT] single argv/env string exceeded 8191 bytes\n");
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
            !nas_user_read_u64(user_array + i * 8, &value))
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
        if (i == limit) {
            nas_print("[EXEC ARG LIMIT] ");
            nas_print(environment ? "environment count=" : "argv count=");
            nas_dec(limit);
            nas_print("\n");
            return (uint64_t)-E2BIG;
        }
        char *storage = environment ? pending_exec.envbuf[i] : pending_exec.argbuf[i];
        uint64_t ret = copy_exec_string(value, storage);
        if ((int64_t)ret < 0) return ret;
        if (environment) pending_exec.envp[i] = storage;
        else pending_exec.argv[i] = storage;
    }
    return (uint64_t)-E2BIG;
}
uint64_t nas_task_execve(uint64_t pathname, uint64_t argv, uint64_t envp) {
    if (!bootfs_mounted()) return (uint64_t)-ENOENT;
    uint64_t result = nas_fs_resolve_path((uint64_t)-100, pathname, pending_exec.filename);
    if ((int64_t)result < 0) return result;
    char actual[256];
    struct nas_fs_node app;
    int err=nas_fs_lookup_node(pending_exec.filename,true,&app,actual);
    if(err)return (uint64_t)(int64_t)err;
    if(app.is_dir || !(app.mode&0111U))return (uint64_t)-EACCES;
    struct elf_image main_img=elf_from_fs_node(&app);
#ifdef NASEED_SYSCALL_TRACE
    static unsigned exec_images_logged;
    if (exec_images_logged++ < 16) {
        nas_print("[EXEC IMAGE] path="); nas_print(actual);
        nas_print(" bytes="); nas_hex(main_img.size);
        nas_print(" mode="); nas_hex(app.mode);
        nas_print("\n");
    }
#endif
    uint8_t magic[4] = {0};
    uint64_t available = main_img.size < sizeof(magic) ?
                         main_img.size : sizeof(magic);
    if (!nas_elf_image_read(&main_img, 0, magic, available))
        return (uint64_t)-ENOEXEC;
    bool scripted=false;
    if(main_img.size>=2 && magic[0]=='#' && magic[1]=='!') {
        err=read_shebang(&main_img,pending_exec.shebang_path,
                         pending_exec.shebang_arg);
        if(err)return (uint64_t)(int64_t)err;
        char resolved_interpreter[256];
        struct nas_fs_node bin;
        err=nas_fs_lookup_node(pending_exec.shebang_path,true,
                               &bin,resolved_interpreter);
        if(err)return (uint64_t)(int64_t)err;
        if(bin.is_dir || !(bin.mode&0111U))return (uint64_t)-EACCES;
        main_img=elf_from_fs_node(&bin);
        app=bin; /* report the backend of the actual ELF interpreter */
        unsigned n=0;
        do { actual[n]=resolved_interpreter[n]; }
        while(resolved_interpreter[n++]);
        scripted=true;
    }
    if(main_img.size<sizeof(struct elf_hdr) ||
       !nas_elf_image_read(&main_img, 0, magic, sizeof(magic)) ||
       magic[0]!=0x7f || magic[1]!='E' ||
       magic[2]!='L' || magic[3]!='F')
        return (uint64_t)-ENOEXEC;
    /* Do not consult main_img.data after exec: a sparse tmpfs ELF has no
     * contiguous base pointer. Keep the validated header in kernel stack
     * storage for the post-exec BusyBox/PIE accounting. */
    struct elf_hdr executable_header;
    if (!nas_elf_image_open(&main_img, NAS_ELF_MACHINE, true, 64,
                            &executable_header)) {
        struct elf_hdr rejected={0};
        nas_print("[ELF HEADER REJECT] "); nas_print(actual);
        if (nas_elf_image_read(&main_img,0,&rejected,sizeof(rejected))) {
            nas_print(" machine="); nas_hex(rejected.machine);
            nas_print(" type="); nas_hex(rejected.type);
            nas_print(" phnum="); nas_hex(rejected.phnum);
            nas_print(" size="); nas_hex(main_img.size);
        }
        nas_print("\n");
        return (uint64_t)-ENOEXEC;
    }
    struct nas_fs_node loader;
    char loader_path[256];
    struct elf_image interp_img={0};
    const struct elf_image *interp=0;
    if(!nas_fs_lookup_node(NAS_ELF_INTERP,true,&loader,loader_path) &&
       !loader.is_dir) {
        interp_img=elf_from_fs_node(&loader);
        interp=&interp_img;
    }
    result = copy_exec_vector(argv, false);
    if ((int64_t)result < 0) return result;
    result = copy_exec_vector(envp, true);
    if ((int64_t)result < 0) return result;
    if (scripted) {
        result = shebang_rewrite_args();
        if ((int64_t)result < 0) return result;
    }
    /* Load and validate the complete new image FIRST. On a failed
     * execve, every existing pthread, FD, robust list and VM must remain
     * untouched; only the successful commit may terminate sibling tasks.
     * The public syscall ABI is identical on all three architectures. */
    uint64_t new_root = nas_mmu_alloc_root();
    if (!new_root) return (uint64_t)-ENOMEM;
    uint64_t previous_root = root_phys;
    uint64_t old_heap = TASK.vm_owner->heap_break;
    uint64_t old_phdr = loaded_phdr, old_phnum = loaded_phnum;
    uint64_t old_entry = loaded_main_entry, old_interp = loaded_interp_base;
    nas_mmu_switch_root(new_root);
    TASK.vm_owner->heap_break = 0;
    loaded_phdr = loaded_phnum = loaded_main_entry = loaded_interp_base = 0;
    uint64_t entry, sp = 0;
    bool good = nas_elf_load(&main_img, interp, &entry);
#ifdef NASEED_SYSCALL_TRACE
    if (!good) { nas_print("[EXEC REJECT] failed ELF or interpreter load: "); nas_print(actual); nas_print("\n"); }
#endif
    if (good) {
        good=nas_user_stack_map_initial();
#ifdef NASEED_SYSCALL_TRACE
        if (!good) nas_print("[EXEC REJECT] failed user stack map\n");
#endif
    }
    if (good) {
        good=nas_signal_install_trampoline();
#ifdef NASEED_SYSCALL_TRACE
        if (!good) nas_print("[EXEC REJECT] failed signal trampoline\n");
#endif
    }
    if (good) {
        good = nas_build_user_stack(pending_exec.argv, pending_exec.argc,
                                pending_exec.envp, pending_exec.envc,
                                pending_exec.filename, &sp);
#ifdef NASEED_SYSCALL_TRACE
        if (!good) nas_print("[EXEC REJECT] failed argv/env/auxv stack\n");
#endif
    }
    if (!good) {
        nas_mmu_switch_root(previous_root);
        if (!nas_mmu_reclaim_root(new_root)) nas_fail("execve rollback page-table corruption");
        TASK.vm_owner->heap_break = old_heap;
        loaded_phdr = old_phdr;
        loaded_phnum = old_phnum;
        loaded_main_entry = old_entry;
        loaded_interp_base = old_interp;
        return (uint64_t)-ENOEXEC;
    }
    /* Image validated: unwind every other thread against OLD mappings
     * before freeing the former root. This also marks abandoned robust
     * mutexes OWNER_DIED and clears their child TIDs in the right VM.
     * The new root is kept intact through this temporary switch. */
    uint64_t committed_root = root_phys;
    struct nas_task *exec_thread = nas_current_task;
    struct nas_task *old_leader = exec_thread->vm_owner;
    uint64_t inherited_tgid=old_leader->tgid;
    bool worker_exec=exec_thread!=old_leader;
    nas_mmu_switch_root(previous_root);
    nas_process_exec_quiesce_siblings();
    nas_mmu_switch_root(committed_root);

    /* exec from CLONE_THREAD must look like the original *process* to
     * parent wait4. Transfer the stable TGID to the surviving task slot,
     * and make its owned FDs/VM metadata independent of the dead leader.
     * File-description references are NOT duplicated: the old table and
     * the worker both pointed to one CLONE_FILES object. The parent sees
     * one child PID and precisely one eventual SIGCHLD. */
    if(worker_exec) {
        exec_thread->fds_owned=*old_leader->fds;
        exec_thread->fds=&exec_thread->fds_owned;
        exec_thread->umask=old_leader->umask;
        memcpy(exec_thread->cwd,old_leader->cwd,sizeof(exec_thread->cwd));
        memcpy(exec_thread->signal_actions,old_leader->signal_actions,
               sizeof(exec_thread->signal_actions));
        exec_thread->heap_break=old_leader->heap_break;
        exec_thread->anonymous=old_leader->anonymous;
        exec_thread->pid=inherited_tgid;
        exec_thread->tgid=inherited_tgid;
        exec_thread->ppid=old_leader->ppid;
        exec_thread->pgid=old_leader->pgid;
        exec_thread->sid=old_leader->sid;
        exec_thread->vm_owner=exec_thread;
        exec_thread->is_thread=false;
        exec_thread->group_exit_pending=false;
        exec_thread->vfork_parent_pid=0;
        exec_thread->vfork_child_pid=0;
        exec_thread->vfork_shared_vm=false;
        nas_task_release(old_leader);
        nas_print("[THREAD EXEC] worker adopted original process tgid=");
        nas_dec(inherited_tgid);nas_print("\n");
    } else {
        exec_thread->group_exit_pending=false;
        nas_print("[THREAD EXEC] leader reaped siblings tgid=");
        nas_dec(inherited_tgid);nas_print("\n");
    }
    /* Commit: syscall can no longer return to old userspace RIP or RSP. */
    for (unsigned fd = 0; fd < 64; fd++)
        if (descriptors[fd] != FD_CLOSED && fd_cloexec[fd])
            nas_fs_close_fd(fd);
    /* Old anonymous ownership dies with the retired page-table root.
     * No metadata is mutated until exec commits, so rollback remains exact. */
    nas_anon_reset(&TASK.vm_owner->anonymous);
    nas_time_timers_drop(); /* POSIX timers never survive execve. */
    TASK.execed = true; /* fork parent may no longer setpgid after exec */
    TASK.signal_frame_active = false;
    TASK.signal_frame_sp = 0;
    TASK.ppoll_mask_active=false;
    TASK.signal_force_deliver=0;
    thread_clear_tid = 0;
    TASK.robust_list_head=0; /* exec replaces userspace robust list */
    nas_arch_context_set_tls(&TASK.arch, 0);
    nas_arch_set_user_tls(0); /* fresh musl/glibc TLS initialization */
    /* Linux execve resets caught handlers to default but preserves ignored
     * dispositions, the process signal mask and pending standard signals. */
    for (unsigned i = 1; i < 65; i++) {
        if (TASK.vm_owner->signal_actions[i][0] == 1) continue; /* SIG_IGN survives exec */
        for (unsigned j = 0; j < 4; j++) TASK.vm_owner->signal_actions[i][j] = 0;
    }
    /* A vfork child shared its old page table with its suspended parent.
     * Its new ELF root has committed, but that old root remains parent-owned. */
    if (TASK.vfork_shared_vm)
        nas_process_vfork_release();
    else if (!nas_mmu_reclaim_root(previous_root))
        nas_fail("execve stale address-space frame reclaim failure");
    const char *basename = actual;
    for (const char *p = basename; *p; p++)
        if (*p == '/') basename = p + 1;
    running_busybox = strcmp(basename, "busybox") == 0;
    running_bash = strcmp(basename, "bash") == 0;
    running_alpine = running_busybox && executable_header.type == 3;
    if (scripted) {
        nas_print("[EXEC] CPIO #! interpreter: ");
        nas_print(pending_exec.shebang_path); nas_print("\n");
    }
    if(app.backend==NAS_FS_TMPFS)
        nas_print("[EXEC] replaced process with tmpfs ELF: ");
    else
        nas_print("[EXEC] replaced process with CPIO ELF: ");
    nas_print(pending_exec.filename);
    nas_print("\n");
    nas_arch_enter_user(entry, sp);
    nas_fail("architecture user-entry returned unexpectedly");
}

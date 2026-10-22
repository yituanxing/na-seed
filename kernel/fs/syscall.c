#include <limine.h>
#include <stdint.h>
#include <stdbool.h>
#include "../errno.h"
#include "../syscall/ops.h"
#include "../task.h"
#include "../fd.h"
#include "../bootfs.h"
#include "../tmpfs.h"
#include "../pipe.h"
#include "../path.h"
#include "../mm/usercopy.h"
#include "../arch/arch.h"
#include "../arch/abi.h"
#include "../arch/linux_stat_abi.h"
#include "../arch/linux_fcntl_abi.h"
#include "../arch/mmu.h"
#include "../drivers/tty.h"
#include "io.h"
#include "syscall.h"
/* Linux x86-64 filesystem syscall adapter. Backend state is already
 * task-local or refcounted via fd.c, pipe.c, bootfs.c and tmpfs.c.
 * The user ABI stat/dirent encodings are still x86-specific in v1. */
#define TASK (*nas_current_task)
#define descriptors TASK.fds.descriptors
#define file_slot TASK.fds.file_slot
#define file_refcount nas_file_refcount
#define fd_cloexec TASK.fds.fd_cloexec
#define file_status_flags nas_file_status_flags
#define boot_files TASK.fds.boot_files
#define boot_entries TASK.fds.boot_entries
#define boot_file_offsets nas_boot_file_offsets
#define current_workdir TASK.cwd
#define running_bash TASK.running_bash
#define running_busybox TASK.running_busybox
#define running_alpine TASK.running_alpine
#define saw_bash_hello TASK.saw_bash_hello
#define matched_chars TASK.matched_chars
extern void nas_print(const char *);
extern void nas_dec(uint64_t);
extern void nas_putchar(char);
extern const struct limine_file *nas_module(uint64_t);
static void nas_fs_fail(const char *msg) __attribute__((noreturn));
static void nas_fs_fail(const char *msg) {
    nas_print("[FAIL] M1 "); nas_print(msg); nas_print("\n");
    nas_arch_halt();
}

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
/* Single-process POSIX descriptors over actual UART and device files.
 * No fake TTY, disk, /proc, or filesystem contents are supplied.
 */
extern void nas_putchar(char c);

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
        nas_fs_fail("pipe2 validated destination became inaccessible");
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
            nas_fs_fail("pipe read validated user mapping became inaccessible");
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
void nas_fs_close_fd(uint64_t fd) {
    unsigned final_slot=nas_fd_close(nas_current_task, (unsigned)fd);
    if (final_slot == NAS_FD_LIMIT) return;
    nas_pipe_release_description(final_slot);
    tmp_release_slot(final_slot);
}
static uint64_t fd_clone(uint64_t src, uint64_t dst, bool cloexec) {
    if (src >= NAS_FD_LIMIT || descriptors[src] == FD_CLOSED ||
        dst >= NAS_FD_LIMIT) return (uint64_t)-EBADF;
    if (src == dst) return dst;
    nas_fs_close_fd(dst);
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
    if (same_string(path, NAS_ELF_INTERP) ||
        same_string(path, NAS_ELF_LIBC_ALIAS))
        return nas_module(1);
    return 0;
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
uint64_t nas_fs_resolve_path(uint64_t dirfd, uint64_t pathname,
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
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_mkdir(path, mode);
}
static uint64_t sys_unlinkat_tmp(uint64_t dirfd, uint64_t pathname,
                                 uint64_t flags) {
    char path[256];
    uint64_t ret = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_unlink(path, flags);
}
static uint64_t sys_renameat_tmp(uint64_t olddirfd, uint64_t oldptr,
                                 uint64_t newdirfd, uint64_t newptr,
                                 uint64_t flags) {
    /* Reject unsupported flags before reading user paths, as before. */
    if (flags & ~1ULL) return (uint64_t)-EINVAL;
    char oldpath[256], newpath[256];
    uint64_t ret = nas_fs_resolve_path(olddirfd, oldptr, oldpath);
    if ((int64_t)ret < 0) return ret;
    ret = nas_fs_resolve_path(newdirfd, newptr, newpath);
    return (int64_t)ret < 0 ? ret :
        (uint64_t)(int64_t)tmp_rename(oldpath, newpath, flags);
}
static uint64_t sys_open_path(const char *path, uint64_t native_flags) {
    uint64_t flags;
    if (!nas_linux_open_flags(native_flags, &flags)) return (uint64_t)-EINVAL;
    const uint64_t permitted = 3ULL | 0x800ULL | 0x8000ULL |
                               0x10000ULL | 0x20000ULL | 0x80000ULL | 0x100ULL |
                               0x80ULL | /* O_EXCL */
                               0x40ULL | 0x200ULL | 0x400ULL;
    if (flags & ~permitted) return (uint64_t)-EINVAL;
    if(same_string(path,"/dev/tty") && !nas_tty_controlling())
        return (uint64_t)-ENXIO;
    uint8_t kind = FD_CLOSED;
    const struct bootfs_entry *entry = 0;
    const struct limine_file *real_file = 0;
    struct nas_tmp_file *tmp_file = 0;
    bool create_tmp = false;
    if(same_string(path,"/dev/tty") || same_string(path,"/dev/console") ||
       same_string(path,"/dev/ttyS0"))kind=FD_TTY;
    else if (same_string(path, "/dev/null")) kind = FD_NULL;
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
    if (kind != FD_NULL && kind != FD_ZERO && kind != FD_TTY &&
        kind != FD_TMP &&
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
    uint64_t result = nas_fs_resolve_path((uint64_t)-100, pathname, path);
    return (int64_t)result < 0 ? result : sys_open_path(path, flags);
}
uint64_t nas_fs_fd_write(uint64_t fd, uint64_t address, uint64_t length) {
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
    if (descriptors[fd] != FD_UART && descriptors[fd] != FD_TTY)
        return (uint64_t)-EBADF;
    for (uint64_t i = 0; i < length; i++) {
        uint8_t *p = nas_mmu_user_byte(address + i);
        if (!p) return i ? i : (uint64_t)-EFAULT;
        if(*p=='\n' && nas_tty_onlcr())nas_putchar('\r');
        nas_putchar((char)*p);
        if (fd == 1 && (running_bash || running_busybox)) observe_stdout((char)*p);
    }
    return length;
}
uint64_t nas_fs_fd_read(uint64_t fd, uint64_t address, uint64_t length) {
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
    if(descriptors[fd]==FD_UART || descriptors[fd]==FD_TTY)
        return nas_tty_read(address,length,
             (file_status_flags[file_slot[fd]]&0x800ULL)!=0);
    if (descriptors[fd] != FD_ZERO) return (uint64_t)-EBADF;
    if (address >= USER_LIMIT || length > USER_LIMIT - address)
        return (uint64_t)-EFAULT;
    uint8_t zero = 0;
    for (uint64_t i = 0; i < length; i++)
        if (!nas_user_write_bytes(address + i, &zero, 1)) return i ? i : (uint64_t)-EFAULT;
    return length;
}
uint64_t nas_fs_fd_writev(uint64_t fd, uint64_t iov, uint64_t count) {
    if (count > 64) return (uint64_t)-EINVAL;
    if (count && (iov >= USER_LIMIT || count > (USER_LIMIT - iov) / 16))
        return (uint64_t)-EFAULT;
    uint64_t sent = 0;
    for (uint64_t n = 0; n < count; n++) {
        uint64_t address, bytes;
        if (!nas_user_read_u64(iov + n * 16, &address) ||
            !nas_user_read_u64(iov + n * 16 + 8, &bytes))
            return sent ? sent : (uint64_t)-EFAULT;
        uint64_t r = nas_fs_fd_write(fd, address, bytes);
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
        if (!nas_user_read_u64(address + i * 8, &word)) return (uint64_t)-EFAULT;
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
                else if (kind == FD_UART || kind == FD_TTY)
                    revents = events & POLLOUT;
                else if (kind == FD_PIPE_R && nas_pipe_poll_readable(file_slot[(unsigned)fd]))
                    revents = events & POLLIN;
                else if (kind == FD_PIPE_W && nas_pipe_poll_writable(file_slot[(unsigned)fd]))
                    revents = events & POLLOUT;
            }
        }
        if (revents) ready++;
        word = (word & 0x0000ffffffffffffULL) | ((uint64_t)revents << 48);
        if (!nas_user_write_u64(address + i * 8, word)) return (uint64_t)-EFAULT;
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
    uint64_t ret = nas_fs_resolve_path((uint64_t)-100, pathname, path);
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

/* Linux x86-64 struct stat layout, 144 bytes. File size is actual module.
 * Device entries have character-device type; mounted files are regular.
 */
static uint64_t sys_fstat_boot(uint64_t fd, uint64_t address) {
    if (fd >= 64 || !descriptors[fd]) return (uint64_t)-EBADF;
    struct nas_linux_stat st;
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
    uint64_t result = nas_fs_resolve_path(dirfd, pathname, path);
    return (int64_t)result < 0 ? result : sys_open_path(path, flags);
}
static uint64_t write_boot_stat(const struct bootfs_entry *entry, uint64_t dst) {
    struct nas_linux_stat st;
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
    if (!nas_user_read_bytes(pathname, &first, 1)) return (uint64_t)-EFAULT;
    if (first == 0 && (flags & AT_EMPTY_PATH))
        return sys_fstat_boot(dirfd, statbuf);
    char path[256];
    uint64_t result = nas_fs_resolve_path(dirfd, pathname, path);
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
    nas_fs_close_fd(result);
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
    uint64_t r = nas_fs_resolve_path(dirfd, pathname, path);
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

/* A known filesystem operation returns its genuine Linux ABI result.
 * Non-FS operations remain owned by the common dispatcher. */
#define FS_RESULT(expr) return (struct nas_fs_result){ .handled = true, .value = (expr) }
struct nas_fs_result nas_fs_syscall(enum nas_syscall_op op,
                                     const uint64_t *args) {
    switch (op) {
    case NAS_SC_rename: FS_RESULT(sys_renameat_tmp((uint64_t)-100, args[1], (uint64_t)-100, args[2], 0));
    case NAS_SC_renameat: FS_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], 0));
    case NAS_SC_renameat2: FS_RESULT(sys_renameat_tmp(args[1], args[2], args[3], args[4], args[5]));
    case NAS_SC_mkdir: FS_RESULT(sys_mkdirat_tmp((uint64_t)-100, args[1], args[2]));
    case NAS_SC_mkdirat: FS_RESULT(sys_mkdirat_tmp(args[1], args[2], args[3]));
    case NAS_SC_unlink: FS_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0));
    case NAS_SC_rmdir: FS_RESULT(sys_unlinkat_tmp((uint64_t)-100, args[1], 0x200ULL));
    case NAS_SC_unlinkat: FS_RESULT(sys_unlinkat_tmp(args[1], args[2], args[3]));
    case NAS_SC_open: FS_RESULT(sys_open_device(args[1], args[2]));
    case NAS_SC_openat: FS_RESULT(sys_openat_boot(args[1], args[2], args[3]));
    case NAS_SC_fstat: FS_RESULT(sys_fstat_boot(args[1], args[2]));
    case NAS_SC_newfstatat: FS_RESULT(sys_fstatat_boot(args[1], args[2], args[3], args[4]));
    case NAS_SC_stat: FS_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0));
    case NAS_SC_lstat: FS_RESULT(sys_fstatat_boot((uint64_t)-100, args[1], args[2], 0x100ULL));
    case NAS_SC_lseek: FS_RESULT(sys_lseek_boot(args[1], args[2], args[3]));
    case NAS_SC_getdents64: FS_RESULT(sys_getdents64(args[1], args[2], args[3]));
    case NAS_SC_readlink: FS_RESULT(sys_readlinkat_boot((uint64_t)-100, args[1], args[2], args[3]));
    case NAS_SC_readlinkat: FS_RESULT(sys_readlinkat_boot(args[1], args[2], args[3], args[4]));
    case NAS_SC_getcwd: FS_RESULT(sys_getcwd_root(args[1], args[2]));
    case NAS_SC_pipe: FS_RESULT(sys_pipe2(args[1], 0));
    case NAS_SC_pipe2: FS_RESULT(sys_pipe2(args[1], args[2]));
    case NAS_SC_fcntl: FS_RESULT(sys_fcntl(args[1], args[2], args[3]));
    case NAS_SC_poll: FS_RESULT(sys_poll(args[1], args[2], args[3]));
    case NAS_SC_dup: FS_RESULT(fd_duplicate(args[1], 0, false));
    case NAS_SC_dup2: FS_RESULT(fd_clone(args[1], args[2], false));
    case NAS_SC_chdir: FS_RESULT(sys_chdir(args[1]));
    case NAS_SC_fchdir: FS_RESULT(sys_fchdir(args[1]));
    case NAS_SC_close:
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
            FS_RESULT((uint64_t)-EBADF);
        nas_fs_close_fd(args[1]);
        FS_RESULT(0);
    case NAS_SC_ioctl:
        if (args[1] >= 64 || descriptors[args[1]] == FD_CLOSED)
            FS_RESULT((uint64_t)-EBADF);
        /* Raw boot UART fd0/1/2 are diagnostic pipes, not interactive
         * TTYs: BusyBox -c must not switch rm into confirmation mode.
         * Real explicit /dev/tty opens get FD_TTY. A PID1 can still
         * opt in to physical COM1 through TIOCSCTTY on raw stdin. */
        FS_RESULT(descriptors[args[1]]==FD_TTY ||
                  (descriptors[args[1]]==FD_UART &&
                   (args[2]==0x540e || nas_tty_enabled())) ?
                  nas_tty_ioctl(args[2],args[3]) : (uint64_t)-ENOTTY);
    case NAS_SC_dup3:
        if (args[1] == args[2] || (args[3] & ~0x80000ULL))
            FS_RESULT((uint64_t)-EINVAL);
        FS_RESULT(fd_clone(args[1], args[2], (args[3] & 0x80000ULL) != 0));
    default:
        return (struct nas_fs_result){ .handled = false, .value = 0 };
    }
}
#undef FS_RESULT

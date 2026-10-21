#!/bin/sh
# Low-cost correctness gate to run BEFORE pushing a feature branch.
set -eu

# GitHub checkout uses fetch-depth: 2, so the parent commit is available.
git diff --check HEAD^ HEAD
git diff --check
git diff --cached --check

for script in scripts/*.sh; do
    sh -n "$script"
done

# Keep the M7 /tmp inode storage boundary intact. The syscall layer may
# call the backend but must not own inode/description tables. The backend
# must not dereference userspace addresses or decode syscall user pointers.
if grep -Eq 'tmp_files\[|tmp_slot_owner\[' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] /tmp backend storage leaked into syscall layer' >&2
    exit 1
fi
if grep -Eq 'user_byte\(|user_write_bytes\(|resolve_path\(' kernel/tmpfs.c; then
    echo '[PREFLIGHT] /tmp backend uses user-copy/syscall pathname code' >&2
    exit 1
fi

# M7.1: one owner for open-description reference/position tables.
if grep -Eq '^(uint16_t nas_file_refcount|uint64_t nas_file_status_flags|uint64_t nas_boot_file_offsets)' kernel/task.c kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] open-file-description storage escaped fd.c' >&2
    exit 1
fi
if grep -Eq 'pipe_release_description|tmp_release_slot|tmp_bind_slot' kernel/fd.c; then
    echo '[PREFLIGHT] generic fd.c acquired a backend dependency' >&2
    exit 1
fi

# Kernel pathname logic must never reach into raw user memory or FD
# tables. Only syscall/FS ABI adapters perform the user argument copying.
if grep -Eq 'user_byte\(|user_write_bytes\(|nas_task_fd_|descriptors\[' kernel/path.c; then
    echo '[PREFLIGHT] path.c reached into syscall user/FD layer' >&2
    exit 1
fi

# M7.3: bounded pipe data and ownership must not move back into common dispatch.
# Backend must not depend on user-copy, task switching or signal delivery.
if grep -Eq 'pipe_table\[|pipe_slot_owner\[|struct nas_pipe[[:space:]]*\{' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] pipe backing leaked back into syscall layer' >&2
    exit 1
fi
if grep -Eq 'user_byte\(|user_read_bytes\(|user_write_bytes\(|task_schedule\(|signal_actions\[' kernel/pipe.c; then
    echo '[PREFLIGHT] pipe backend acquired syscall/task/user-copy dependencies' >&2
    exit 1
fi

# M7.4: a boot *image* selector, not an ELF loader, syscall table,
# or a second CPIO filesystem; keep these dependencies outside boot_image.c.
if grep -Eq 'user_byte\(|user_write_bytes\(|syscall_dispatch\(|elf_load\(|user_map\(|alloc_user_root\(' kernel/boot_image.c; then
    echo '[PREFLIGHT] boot_image.c acquired syscall/ELF VM responsibilities' >&2
    exit 1
fi
if grep -Eq 'bootfs_mount\(|nas_user_module\(' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] Limine/CPIO boot selection leaked into common init/exec/dispatcher' >&2
    exit 1
fi

# M8.1: generic VM / syscall code must not manipulate x86 PTEs directly.
if grep -Eq 'P_PRESENT|P_RW|P_USER|PHYS_MASK|user_leaf' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c kernel/mm/vm.c; then
    echo '[PREFLIGHT] x86 PTE details escaped arch/x86_64/mmu.c' >&2
    exit 1
fi
if grep -Eq 'syscall_dispatch|__NR_|nas_pipe_|nas_tmp_' kernel/mm/vm.c; then
    echo '[PREFLIGHT] VM policy acquired syscall/FS dependencies' >&2
    exit 1
fi
# M8.3: ELF parsing, user stack, usercopy and arch_prctl have owning modules.
if grep -Eq 'struct __attribute__\(\(packed\)\) elf_hdr|static bool user_read_bytes\(|static uint64_t sys_arch_prctl\(|static uint64_t sys_sigaction\(' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] old ELF/usercopy/syscall ABI implementation leaked into common init/exec/dispatcher' >&2
    exit 1
fi

# M8.4: descriptor/path/stat/pipe syscall bodies reside with FS.
if grep -Eq '^static uint64_t (sys_pipe2|sys_open_path|sys_fcntl|sys_fstat_boot|sys_getdents64|sys_poll)\(' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] FS syscall implementation leaked back into common dispatch' >&2
    exit 1
fi
if ! grep -q 'nas_fs_syscall(nr, args)' kernel/syscall/dispatch.c kernel/task/exec.c kernel/init/main.c; then
    echo '[PREFLIGHT] common syscall dispatcher stopped forwarding FS calls' >&2
    exit 1
fi

# M8.5: the monolithic syscall image has been removed.
test ! -e kernel/m1.c
if grep -Eq 'syscall_dispatch\(|__NR_' kernel/init/main.c kernel/task/exec.c; then
    echo '[PREFLIGHT] architecture syscall ABI leaked into init or exec' >&2
    exit 1
fi

# Architecture syscall tables must be decoded before shared FS, process,
# scheduler and VM; guard this deliberately during future ISA ports.
if grep -Eq '__NR_[[:alnum:]_]+' kernel/syscall/dispatch.c kernel/fs/syscall.c kernel/task/sched.c; then
    echo '[PREFLIGHT] raw ISA syscall numbers leaked into common core' >&2
    exit 1
fi
if grep -Eq 'NASEED_PORT_M0' kernel/task/*.c kernel/fs/*.c kernel/mm/vm.c; then
    echo '[PREFLIGHT] architecture-only M0 gate leaked into common kernel' >&2
    exit 1
fi

# M12: process table bound and orphan adoption are common-Task mechanisms,
# not ISA-specific or synthetic syscall fallbacks.
grep -Fq '#define NAS_TASK_LIMIT 16' kernel/task.h
# Standard C init PID1 gets a real Linux fork-exec-wait oracle even
# before downloading BusyBox or booting any guest.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m13-init.c -o /tmp/naseed-m13-init
[ "$(/tmp/naseed-m13-init --linux-oracle)" = m13-init-ok ]
grep -Fq 'nas_task_adopt_orphans(child->pid)' kernel/task/sched.c
grep -Fq 'BUSYBOX_EXTRA_DIR' scripts/stage-busybox-rootfs.sh

# Compare our pinned Linux x86-64 syscall numbers against the host UAPI.
python3 scripts/check-syscall-nr.py
python3 scripts/check-syscall-errno.py

cc -std=c11 -Wall -Wextra -Werror \
    scripts/test-anon-regions.c kernel/mm/anon_regions.c \
    -o /tmp/naseed-anon-regions
/tmp/naseed-anon-regions
sh scripts/check-full-kernel-sources.sh
cc -std=c11 -Wall -Wextra -Werror scripts/test-wait-pid-abi.c \
    -o /tmp/naseed-wait-pid
/tmp/naseed-wait-pid
cc -std=c11 -Wall -Wextra -Werror scripts/test-mmap-abi.c \
    -o /tmp/naseed-mmap-abi
/tmp/naseed-mmap-abi
make -j2 kernel
test -s build/kernel.elf
readelf -h build/kernel.elf > /dev/null
printf '%s\n' '[PREFLIGHT] diff / shell syntax / kernel compile+link / ELF: PASS'

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

# One canonical kernel-only C runtime, checked on all real target triples.
sh scripts/check-klib.sh
sh scripts/check-block-foundation.sh
sh scripts/check-vfs-provider.sh
sh scripts/check-task-stacks.sh
sh scripts/check-m91-kcontext.sh
sh scripts/check-m95-kthread.sh
sh scripts/check-m96-k-sync.sh
sh scripts/check-m97-k-timeouts.sh
sh scripts/check-m98-sched-core.sh
sh scripts/check-m101-cow.sh
sh scripts/check-m102-tty-cow.sh
sh scripts/check-m100-signal-failure.sh
sh scripts/check-frame-refs.sh
sh scripts/check-m86-txn.sh
sh scripts/check-m88-exceptions.sh
sh scripts/check-m89-poll.sh
sh scripts/check-m90-io-wait.sh
# M65: native procfs owner host regression (not a simulated guest PASS).
cc -std=c11 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-procfs.c kernel/fs/procfs.c kernel/lib/string.c \
    -o /tmp/naseed-procfs-test
/tmp/naseed-procfs-test
# M66: account actual clock differences using the common Task mechanism.
cc -std=c11 -Wall -Wextra -Werror -fno-builtin -I kernel \
    scripts/test-task-accounting.c kernel/task/accounting.c \
    -o /tmp/naseed-task-accounting
/tmp/naseed-task-accounting
# M71: selected VM root, noncontiguous physical pages, fault bounds.
cc -std=c11 -Wall -Wextra -Werror -fno-builtin \
  -ffunction-sections -fdata-sections -Wl,--gc-sections -I kernel \
  scripts/test-remote-task-read.c kernel/mm/remote_copy.c kernel/mm/mm.c \
  -o /tmp/naseed-remote-task-read
/tmp/naseed-remote-task-read
sh scripts/check-m28-timer.sh
sh scripts/check-m28-port-timers.sh

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
grep -Fq '#define NAS_TASK_LIMIT 48' kernel/task.h
# Standard C init PID1 gets a real Linux fork-exec-wait oracle even
# before downloading BusyBox or booting any guest.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m13-init.c -o /tmp/naseed-m13-init
[ "$(/tmp/naseed-m13-init --linux-oracle)" = m13-init-ok ]
grep -Fq 'nas_task_adopt_orphans(child->pid)' kernel/task/sched.c
# Standard C session/group ABI must pass Linux before QEMU kernel acceptance.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m14-groups.c -o /tmp/naseed-m14-groups
[ "$(/tmp/naseed-m14-groups)" = m14-groups-ok ]
grep -Fq 'BUSYBOX_EXTRA_DIR' scripts/stage-busybox-rootfs.sh
# Independent Linux signal oracle: genuine fork, pipe, SIGKILL and waitpid.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m15-kill.c -o /tmp/naseed-m15-kill
[ "$(/tmp/naseed-m15-kill)" = m15-kill-ok ]
# Native Linux oracle: real STOP/CONT job groups and waitpid events.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m16-stop.c -o /tmp/naseed-m16-stop
[ "$(/tmp/naseed-m16-stop)" = m16-stop-ok ]
# Host Linux PTY oracle; CI image has util-linux script.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m17-tty.c -o /tmp/naseed-m17-tty
script -q -e -c /tmp/naseed-m17-tty /dev/null | tr -d '\r' \
    | grep -Fxq m17-tty-ok
# The real BusyBox PID1 launcher must also work with a genuine Linux PTY.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m18-ash-init.c -o /tmp/naseed-m18-ash-init
script -q -e -c '/tmp/naseed-m18-ash-init --linux-pty-oracle' /dev/null \
    | tr -d '\r' | grep -Fxq m18-linux-pty-ok

# M21 Linux ABI oracle: native static ELF with a real controlling PTY.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m21-tty-sched.c -o /tmp/naseed-m21-tty-sched
script -q -e -c '/tmp/naseed-m21-tty-sched --linux-pty-oracle' /dev/null \
    | tr -d '\r' | grep -Fxq m21-linux-pty-ok

# M22 Linux-standard pollfd oracle under a real controlling PTY.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m22-poll.c -o /tmp/naseed-m22-poll
script -q -e -c '/tmp/naseed-m22-poll --linux-pty-oracle' /dev/null \
    | tr -d '\r' | grep -Fxq m22-linux-pty-ok

# M23 standard Linux terminal/PGID ABI check; real UART group signaling
# is exercised exclusively in the full three-ISA QEMU acceptance.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m23-signals.c -o /tmp/naseed-m23-signals
script -q -e -c '/tmp/naseed-m23-signals --linux-pty-oracle' /dev/null \
    | tr -d '\r' | grep -Fxq m23-linux-pty-ok

# M24 native POSIX signal oracle: blocked pending / ignored / default.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m24-signals.c -o /tmp/naseed-m24-signals
/tmp/naseed-m24-signals --linux-oracle | grep -Fxq m24-signals-ok

# M25 standard Linux controlling PTY / TOSTOP / foreground interface.
cc -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    tests/m25-tty-jobs.c -o /tmp/naseed-m25-tty-jobs
script -q -e -c '/tmp/naseed-m25-tty-jobs --linux-pty-oracle' /dev/null \
    | tr -d '\r' | grep -Fxq m25-linux-pty-ok

# Compare our pinned Linux x86-64 syscall numbers against the host UAPI.
python3 scripts/check-syscall-nr.py
python3 scripts/check-syscall-errno.py

cc -std=c11 -Wall -Wextra -Werror \
    scripts/test-anon-regions.c kernel/mm/anon_regions.c \
    -o /tmp/naseed-anon-regions
/tmp/naseed-anon-regions
sh scripts/check-full-kernel-sources.sh
cc -std=c11 -O2 -Wall -Wextra -Werror \
    tests/test-tty-ldisc.c kernel/drivers/tty_ldisc.c -o /tmp/naseed-tty-ldisc
/tmp/naseed-tty-ldisc
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

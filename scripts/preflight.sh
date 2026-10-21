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
if grep -Eq 'tmp_files\\[|tmp_slot_owner\\[' kernel/m1.c; then
    echo '[PREFLIGHT] /tmp backend storage leaked into syscall layer' >&2
    exit 1
fi
if grep -Eq 'user_byte\\(|user_write_bytes\\(|resolve_path\\(' kernel/tmpfs.c; then
    echo '[PREFLIGHT] /tmp backend uses user-copy/syscall pathname code' >&2
    exit 1
fi

# M7.1: one owner for open-description reference/position tables.
if grep -Eq '^(uint16_t nas_file_refcount|uint64_t nas_file_status_flags|uint64_t nas_boot_file_offsets)' kernel/task.c kernel/m1.c; then
    echo '[PREFLIGHT] open-file-description storage escaped fd.c' >&2
    exit 1
fi
if grep -Eq 'pipe_release_description|tmp_release_slot|tmp_bind_slot' kernel/fd.c; then
    echo '[PREFLIGHT] generic fd.c acquired a backend dependency' >&2
    exit 1
fi

# Kernel pathname logic must never reach into raw user memory or FD
# tables. Only m1.c performs the syscall argument copying.
if grep -Eq 'user_byte\\(|user_write_bytes\\(|nas_task_fd_|descriptors\\[' kernel/path.c; then
    echo '[PREFLIGHT] path.c reached into syscall user/FD layer' >&2
    exit 1
fi

# M7.3: bounded pipe data and ownership must not move back into m1.c.
# Backend must not depend on user-copy, task switching or signal delivery.
if grep -Eq 'pipe_table\\[|pipe_slot_owner\\[|struct nas_pipe[[:space:]]*\\{' kernel/m1.c; then
    echo '[PREFLIGHT] pipe backing leaked back into syscall layer' >&2
    exit 1
fi
if grep -Eq 'user_byte\\(|user_read_bytes\\(|user_write_bytes\\(|task_schedule\\(|signal_actions\\[' kernel/pipe.c; then
    echo '[PREFLIGHT] pipe backend acquired syscall/task/user-copy dependencies' >&2
    exit 1
fi

# M7.4: a boot *image* selector, not an ELF loader, syscall table,
# or a second CPIO filesystem; keep these dependencies outside boot_image.c.
if grep -Eq 'user_byte\\(|user_write_bytes\\(|syscall_dispatch\\(|elf_load\\(|user_map\\(|alloc_user_root\\(' kernel/boot_image.c; then
    echo '[PREFLIGHT] boot_image.c acquired syscall/ELF VM responsibilities' >&2
    exit 1
fi
if grep -Eq 'bootfs_mount\\(|nas_user_module\\(' kernel/m1.c; then
    echo '[PREFLIGHT] Limine/CPIO boot selection leaked into m1.c' >&2
    exit 1
fi

# Compare our pinned Linux x86-64 syscall numbers against the host UAPI.
python3 scripts/check-syscall-nr.py
python3 scripts/check-syscall-errno.py

make -j2 kernel
test -s build/kernel.elf
readelf -h build/kernel.elf > /dev/null
printf '%s\n' '[PREFLIGHT] diff / shell syntax / kernel compile+link / ELF: PASS'

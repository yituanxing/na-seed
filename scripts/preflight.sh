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

# Compare our pinned Linux x86-64 syscall numbers against the host UAPI.
python3 scripts/check-syscall-nr.py
python3 scripts/check-syscall-errno.py

make -j2 kernel
test -s build/kernel.elf
readelf -h build/kernel.elf > /dev/null
printf '%s\n' '[PREFLIGHT] diff / shell syntax / kernel compile+link / ELF: PASS'

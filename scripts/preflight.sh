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

# Compare our pinned Linux x86-64 syscall numbers against the host UAPI.
python3 scripts/check-syscall-nr.py

make -j2 kernel
test -s build/kernel.elf
readelf -h build/kernel.elf > /dev/null
printf '%s\n' '[PREFLIGHT] diff / shell syntax / kernel compile+link / ELF: PASS'

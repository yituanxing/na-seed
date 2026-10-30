#!/bin/sh
# Two *real* boots per native ISA: original signed Vim Ex disk edits and
# actual physical UART keystrokes through unchanged Vim normal/insert mode.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
NAS_ALPINE_PROFILE=vim sh rootfs/alpine/build.sh "$port"
work="build/external/m53-vim/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m53-vim-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m53-vim.clean.log"
test -s "$log"
for mark in M53_ORIGINAL_SIGNED_VIM_PASS \
            M53_VIM_REAL_EX_FILE_SAVE_PASS \
            M53_VIM_REOPEN_WRITE_PASS M53_VIM_EX_SUITE_PASS; do
    grep -Fxq "$mark" "$log" || {
        echo "[M53 FAIL] missing real Vim Ex marker $mark $port" >&2
        tail -120 "$log" >&2
        exit 1
    }
done
echo "[M53 EX PASS] $port original signed Vim actually saved and reopened tmpfs"
img=$(sh scripts/m53-vim-boot.sh "$port")
python3 scripts/m53-vim-serial.py "$img" "$port"
echo "[M53 PASS] $port Vim original ELF, real Ex writes and physical UART editor"

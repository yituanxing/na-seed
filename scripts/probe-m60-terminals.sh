#!/bin/sh
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m60-prebuilt.sh "$port"
work="build/external/m60-terminal/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_MANIFEST=scripts/m60-terminal-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=768M
export NAS_ACCEPTANCE_DISK_MB=512
export NAS_SYSCALL_TRACE=${NAS_SYSCALL_TRACE:-0}
sh "$runner"
log="$logs/m60-terminal.clean.log"
test -s "$log"
for stage in \
    M60_ORIGINAL_LESS_PASS \
    M60_LESS_FILE_FILTER_PASS \
    M60_LESS_STDIN_STREAM_PASS \
    M60_ORIGINAL_TMUX_PASS \
    M60_TMUX_REAL_PTY_CHILD_PASS \
    M60_FULL_PASS; do
    if ! grep -Fxq "$stage" "$log"; then
        echo "[M60 FAIL] $port missing $stage" >&2
        tail -160 "$log" >&2
        exit 1
    fi
done
echo "[M60 PASS] $port original less + tmux with real PTY child"

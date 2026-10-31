#!/bin/sh
# M141 acceptance: real unchanged Alpine programs in native NaSeed system QEMU.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m141-prebuilt.sh "$port"
work="build/external/m141-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export NAS_ACCEPTANCE_DISK_MB=768
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m141-batch-cases.tsv

log="$logs/m141-batch.clean.log"
if ! sh "$runner"; then
    echo "[M141 NATIVE QEMU FAIL] $port" >&2
    if test -s "$log"; then
        grep -aE '^(M141_[A-Z0-9_]+_(PASS|FAIL)|\[M141 )' "$log" >&2 || true
        # The last serial lines can omit early failing cases (DWARF tooling
        # runs before process/network checks). Preserve each guest-reported
        # error tail independent of how many kernel trace lines follow it.
        awk '
            /^M141_[A-Z0-9_]+_FAIL$/ { failed=1; print; next }
            /^M141_[A-Z0-9_]+_(PASS|FAIL)$/ { failed=0; next }
            failed && $0 !~ /^\[/ { print; next }
            failed && /^\[(MISS|VM REJECT|FS REJECT|NET|SOCKET|IPC)\]/ { print }
        ' "$log" >&2 || true
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[M141 NATIVE QEMU FAIL] missing serial $port" >&2; exit 1; }

count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M141_*_PASS) ;; *) echo "[M141 BAD MARKER] $marker" >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo "[M141 DUPLICATE MARKER] $marker" >&2; exit 2 ;; esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || true)
    if [ "$found" -ne 1 ]; then
        echo "[M141 NATIVE QEMU FAIL] $port $marker appears $found times" >&2
        tail -1500 "$log" >&2
        exit 1
    fi
    if [ "$marker" = M141_BATCH_PASS ]; then
        summary=$((summary + 1))
    else
        count=$((count + 1))
    fi
done < scripts/m141-expected-markers.txt

[ "$count" -ge 25 ] && [ "$summary" -eq 1 ] || {
    echo "[M141 NATIVE QEMU FAIL] $port expected >=25 behavior checks, found $count" >&2
    exit 1
}
echo "[M141 NATIVE QEMU PASS] $port 68 distinct signed ELFs staged, $count semantic checks"

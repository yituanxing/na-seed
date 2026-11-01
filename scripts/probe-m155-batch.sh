#!/bin/sh
# Real native QEMU executes thirty offline filesystem/partition operations
# with original Alpine v3.23 signed ELF images on each chosen architecture.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m155-prebuilt.sh "$port"
work="build/external/m155-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=800
export BUSYBOX_PARITY_MANIFEST=scripts/m155-batch-cases.tsv
log="$logs/m155-batch.clean.log"
if ! sh "$runner"; then
    echo "[M155 NATIVE QEMU FAIL] $port 33 signed ELFs, 30 semantic fixtures" >&2
    if test -s "$log"; then
        grep -aE '^(M155_[A-Z0-9_]+_(PASS|FAIL)|\[M155 )' "$log" >&2 || :
        tail -2500 "$log" >&2
    fi
    exit 1
fi
test -s "$log" || {
    echo "[M155 NATIVE QEMU FAIL] missing serial $port" >&2; exit 1;
}
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in M155_*_PASS) ;; *) exit 2 ;; esac
    case "$seen" in *" $marker "*)
        echo "[M155 DUPLICATE MARKER] $marker" >&2; exit 2 ;;
    esac
    seen="$seen$marker "
    found=$(grep -Fxc "$marker" "$log" || :)
    [ "$found" -eq 1 ] || {
        echo "[M155 NATIVE QEMU FAIL] $port $marker appears $found times" >&2
        exit 1
    }
    if [ "$marker" = M155_BATCH_PASS ]; then summary=$((summary+1));
    else count=$((count+1)); fi
done < scripts/m155-expected-markers.txt
[ "$count" -eq 30 ] && [ "$summary" -eq 1 ] || {
    echo "[M155 NATIVE QEMU FAIL] $port expected 30 operations + summary" >&2
    exit 1
}
# The 30 real operations cover 25 different ELF identities; eight tools
# with unsafe/no-mount semantics are separately launched only to prove
# the original ELF and userspace loader. Those are NOT counted as operations.
starts=$(grep -Fc '[M155 ELF STARTUP ONLY]' "$log" || :)
[ "$starts" -eq 8 ] || {
    echo "[M155 NATIVE QEMU FAIL] $port only $starts/8 bounded startup-only ELF probes" >&2
    exit 1
}
execs=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M155 NATIVE QEMU FAIL] $port no true kernel [EXEC] of /$elf" >&2
        exit 1
    }
    execs=$((execs+1))
done < scripts/m155-original-elfs.txt
[ "$execs" -eq 33 ] || {
    echo "[M155 NATIVE QEMU FAIL] $port executed $execs/33 manifest ELFs" >&2
    exit 1
}
echo "[M155 NATIVE QEMU PASS] $port 33 globally new ELF EXEC (25 functional identities + 8 startup), 30 actual filesystem checks"

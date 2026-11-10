#!/bin/sh
# One genuine system-QEMU guest per ISA. The M178 physical async disk now
# holds an MBR ext4 partition. A kernel filesystem worker (the only context
# where the initial block adapter can safely park) drives REAL VFS/lwext4
# inode read/write/fsync/reopen through IRQ/deferred BlockRequest completion.
# The independent host reads actual ext4 bytes back after QEMU termination.
# This deliberately does NOT claim asynchronous user syscalls: that is the
# distinct future ordinary-task metadata-broker phase.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

export NAS_M179_EXT4_PARTITION=1 NAS_ENABLE_M179_FS_IRQ=1 NAS_ENABLE_LWEXT4=1
sh scripts/probe-m178-blk-irq.sh "$port"

log="$logs/m174-batch.clean.log"
test -s "$log" || {
    echo "[M179 FS IRQ FAIL] $port no actual guest serial" >&2
    exit 1
}
marker='[M179 FS IRQ WORKER PASS]'
[ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
    echo "[M179 FS IRQ FAIL] $port kernel worker did not verify real VFS/lwext4 seed/write/fsync/reopen" >&2
    grep -aE 'M179|VIRTIO-BLK-ASYNC|LWEXT4' "$log" >&2 || :
    exit 1
}
# The counters are the *delta for filesystem worker I/O*, not the earlier
# independent M178 low-sector fixture stats. A copied M178 PASS marker, a
# synchronous polling BlockDevice, or completion without a sleeping kernel
# worker cannot pass this gate.
awk '
    /^\[M179 FS IRQ WORKER STATS\] / {
        records++
        for (i=6; i<=NF; i++) {
            if (split($i, pair, "=") != 2 || pair[2] !~ /^[0-9]+$/) bad=1
            value[pair[1]]=pair[2]+0
            seen[pair[1]]++
        }
    }
    END {
        if (records!=1 || bad) exit 1
        split("request_submitted request_completed irq_ack deferred fallback_poll waited", keys, " ")
        for (i=1; i<=6; i++) if (seen[keys[i]]!=1) exit 1
        if (value["request_submitted"]<1 ||
            value["request_completed"]!=value["request_submitted"] ||
            value["irq_ack"]<1 || value["deferred"]<1 ||
            value["fallback_poll"]!=0 || value["waited"]<1) exit 1
    }
' "$log" || {
    echo "[M179 FS IRQ FAIL] $port worker had no real block request/IRQ/deferred sleep counters" >&2
    grep -aE 'M179 FS IRQ' "$log" >&2 || :
    exit 1
}
disk="$PWD/build/m178/async-blk/$port/independent-virtio-blk.img"
test -s "$disk"
python3 scripts/m179-ext4-fixture.py verify "$disk"
echo "[M179 REAL EXT4 IRQ NATIVE PASS] $port kernel kthread park/resume through actual VFS/lwext4/MBR physical disk, host-observed 12KiB persisted, M178 concurrent DMA IRQ plus original 20 signed Alpine/Ncat/GPU/QMP oracles; ordinary user syscalls deferred"
# The already staged M174 signed CPIO also drives a separate *desktop mode*
# acceptance with the original dialog ELF and physical keyboard/QMP frames.
# Do not reuse the ext4-block workload flags or disk for that second boot:
# the physical persisted M179/M180 disk above remains an independent oracle.
(
    unset NAS_M179_EXT4_PARTITION NAS_ENABLE_M179_FS_IRQ
    unset NAS_ENABLE_M180_FS_USER_IRQ NAS_M180_FS_USER_STAGE
    unset NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
    unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_ENABLE_LWEXT4
    sh scripts/probe-m187-vt100.sh "$port"
)

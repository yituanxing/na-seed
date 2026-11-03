#!/bin/sh
# M180: one genuine system-QEMU guest per ISA. Retain every M174-M179
# independent native oracle, then require a separately linked ring3 Linux
# ELF to open/read/create/write/fsync/close/reopen the M179 /async ext4
# partition through the kernel-worker IRQ broker. Guest markers alone CANNOT
# pass: the host must independently inspect QEMU's actual physical raw disk,
# verify the exact new file bytes and run e2fsck after clean shutdown.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

# The kernel option is distinct from the rootfs launcher option. If a native
# build has not enabled/implemented the M180 user broker, the test fails at
# runtime rather than accepting the old M179 kernel-only write marker.
export NAS_ENABLE_M180_FS_USER_IRQ=1 NAS_M180_FS_USER_STAGE=1
sh scripts/probe-m179-fs-irq.sh "$port"

log="$logs/m174-batch.clean.log"
test -s "$log" || {
    echo "[M180 FS USER FAIL] $port missing genuine guest serial" >&2
    exit 1
}
for marker in '[M180 USER FS SYSCALL PASS]' \
              '[M180 USER FS LAUNCH PASS]'; do
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M180 FS USER FAIL] $port missing unique real ring3 marker: $marker" >&2
        grep -aE 'M180|LWEXT4|VIRTIO-BLK-ASYNC' "$log" >&2 || :
        exit 1
    }
done
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/naseed-m180-fs-user' "$log" || {
    echo "[M180 FS USER FAIL] $port separately linked ring3 ELF not execed" >&2
    exit 1
}

# These MUST be interval deltas for *actual* M180 user filesystem requests,
# not M178's low-sector or M179's pre-existing in-kernel VFS tests. IRQ ACK
# and deferred completion must cause a worker sleep/wakeup and a user-Task
# park/resume. All entries are mandatory and must be observed, not zero-filled.
awk '
    /^\[M180 FS USER IRQ STATS\] / {
        records++
        for (i=6; i<=NF; i++) {
            if (split($i, pair, "=")!=2 || pair[2]!~/^[0-9]+$/) bad=1
            value[pair[1]]=pair[2]+0
            count[pair[1]]++
        }
    }
    END {
        if (records!=1 || bad) exit 1
        split("request_submitted request_completed irq_ack deferred fallback_poll worker_waited task_parked task_woken broker_submitted broker_completed", required, " ")
        for (i=1;i<=10;i++) if (count[required[i]]!=1) exit 1
        if (value["request_submitted"]<1 ||
            value["request_completed"]!=value["request_submitted"] ||
            value["irq_ack"]<1 || value["deferred"]<1 ||
            value["fallback_poll"]!=0 || value["worker_waited"]<1 ||
            value["task_parked"]<1 ||
            value["task_woken"]!=value["task_parked"] ||
            value["broker_submitted"]<1 ||
            value["broker_completed"]!=value["broker_submitted"])
            exit 1
    }
' "$log" || {
    echo "[M180 FS USER FAIL] $port user→broker→worker→hardware IRQ deltas missing/inconsistent" >&2
    grep -aF '[M180 FS USER IRQ STATS]' "$log" >&2 || :
    exit 1
}

disk="$PWD/build/m178/async-blk/$port/independent-virtio-blk.img"
test -s "$disk" || {
    echo "[M180 FS USER FAIL] $port separate real QEMU raw disk absent" >&2
    exit 1
}
python3 scripts/m180-ext4-fixture.py verify "$disk"
echo "[M180 REAL EXT4 USER IRQ NATIVE PASS] $port ordinary static Linux ELF openat/read/write/fsync/close/reopen over worker-owned ext4 + genuine block IRQ/deferred/wake/no polling + post-QEMU host-observed 12KiB physical persistence/e2fsck; M174-M179 signed Alpine/Ncat/GPU/QMP/physical disk oracles retained"

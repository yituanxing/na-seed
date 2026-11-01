#!/bin/sh
# Verify eight different original signed Alpine utilities through their
# real filesystem, process, UNIX-socket and packet-parser side effects.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: sh scripts/probe-lwip-system-cohort-smoke.sh' >&2; exit 2; }
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_SYSCALL_TRACE=1
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_ACCEPTANCE_QEMU_RAM=1536M
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/lwip-system-cohort-cases.tsv

sh scripts/stage-m141-prebuilt.sh amd64
root=build/external/m141-batch/amd64
test -s "$root/rootfs.cpio"
test -s "$root/m141-original-signed-elfs.sha256"
test -s "$root/rootfs/bin/busybox"
test -s "$root/rootfs/tests/m141-system-cohort-run.sh"
for program in \
    usr/bin/tcpdump usr/bin/ssh-agent usr/bin/ssh-add \
    bin/ionice bin/pidwait usr/bin/exch sbin/mkfs.bfs sbin/fdisk; do
    test -s "$root/rootfs/$program" || {
        echo "[LWIP SYSTEM COHORT FAIL] missing original ELF $program" >&2; exit 1;
    }
done
export NAS_ACCEPTANCE_CPIO="$root/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$root/rootfs/bin/busybox"

sh scripts/probe-x86-busybox-parity.sh
clean=build/busybox-source-parity-x86/m141-batch.clean.log
test -s "$clean"
grep -Fq '[VIRTIO-NET] real modern PCI devices=1' "$clean" || {
    echo '[LWIP SYSTEM COHORT FAIL] no actual VirtIO PCI NIC' >&2; exit 1;
}
for program in TCPDUMP SSH_AGENT SSH_ADD IONICE PIDWAIT EXCH MKFS_BFS FDISK; do
    grep -Fxq "M141_SIGNED_SYSTEM_${program}_PASS" "$clean" || {
        echo "[LWIP SYSTEM COHORT FAIL] $program missing original-program proof" >&2;
        exit 1;
    }
done
for elf in usr/bin/tcpdump usr/bin/ssh-agent usr/bin/ssh-add \
           bin/ionice bin/pidwait usr/bin/exch sbin/mkfs.bfs sbin/fdisk; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$clean" || {
        echo "[LWIP SYSTEM COHORT FAIL] no original ELF exec: $elf" >&2;
        exit 1;
    }
done
grep -Fxq M141_SIGNED_SYSTEM_COHORT_PASS "$clean" || {
    echo '[LWIP SYSTEM COHORT FAIL] incomplete original ELF batch' >&2; exit 1;
}
echo '[LWIP SYSTEM COHORT QEMU PASS] eight signed Alpine ELFs: tcpdump/ssh-agent/ssh-add/ionice/pidwait/exch/mkfs.bfs/fdisk'

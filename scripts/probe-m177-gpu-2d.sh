#!/bin/sh
# M177: one real system-QEMU guest per ISA, with a VirtIO-GPU 2D scanout.
# Reuse M176's signed Alpine software/network/IRQ/device acceptance and its
# five independent QMP physical frames. There is NO firmware ramfb device.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

export NAS_QEMU_VIRTIO_GPU=1 NAS_ENABLE_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
sh scripts/probe-m176-net-irq.sh "$port"

log="$logs/m174-batch.clean.log"
test -s "$log"
transport=MMIO
[ "$port" != amd64 ] || transport=PCI
grep -Fqx "[VIRTIO-GPU] real modern $transport devices=1" "$log" || {
    echo "[M177 GPU FAIL] $port missing genuine bound $transport graphics device" >&2
    exit 1
}
grep -Fqx '[M177 VIRTIO GPU 2D PASS]' "$log" || {
    echo "[M177 GPU FAIL] $port no 2D scanout/completion proof" >&2
    grep -a 'M177\|GPU' "$log" >&2 || :
    exit 1
}
awk '
    /^\[M177 VIRTIO GPU 2D STATS\] / {
        records++
        for (i=6;i<=NF;i++) {
            split($i, pair, "=")
            if (length(pair[1]) && pair[2]~/^[0-9]+$/)
                stats[pair[1]]=pair[2]+0
        }
    }
    END {
        # M181 is IRQ driven: a completed fifth flush may be followed by a
        # sixth frame in flight when this one-shot stats line is emitted.
        # With exactly one reusable control request, at most one command and
        # at most one completed transfer awaiting flush are legitimate.
        if (records!=1 || stats["backing_pages"]!=469 ||
            stats["submitted"]<14 || stats["completed"]<14 ||
            stats["completed"]>stats["submitted"] ||
            stats["submitted"]-stats["completed"]>1 ||
            stats["transfers"]<5 || stats["flushes"]<5 ||
            stats["flushes"]>stats["transfers"] ||
            stats["transfers"]-stats["flushes"]>1 ||
            stats["irq_ack"]<1)
            exit 1
    }
' "$log" || {
    echo "[M177 GPU FAIL] $port missing/invalid actual QEMU GPU resource, DMA, fenced completions and IRQ stats" >&2
    grep -a 'M177 VIRTIO GPU' "$log" >&2 || :
    exit 1
}
echo "[M177 GPU 2D NATIVE PASS] $port physical VirtIO-GPU ($transport), 469-page DMA-backed real 800x600 scanout, fenced 2D transfer/flush, IRQ, original 20 signed Alpine ELFs, Ncat HTTP, 5 changing QMP frames"

#!/bin/sh
# M178: one genuine system-QEMU guest per ISA, simultaneously proving the
# existing signed Alpine desktop/network/input/GPU workload AND independent
# asynchronous VirtIO-BLK requests on a separate writable raw disk.
# Guest-only PASS text is insufficient: host verifies the physical disk bytes
# after QEMU exits. Neither initramfs nor the firmware ESP can satisfy this.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

work="build/m178/async-blk/$port"
mkdir -p "$work"
disk="$PWD/$work/independent-virtio-blk.img"
# A repeated local run must NEVER inherit last run's successful guest writes.
truncate -s 0 "$disk"
truncate -s 16M "$disk"
python3 - "$disk" prepare <<'PY'
import os
import sys

image, operation = sys.argv[1:]

def pattern(letter, label):
    salt = 7 if letter == "A" else 19
    data = bytearray((i * 13 + salt) & 255 for i in range(512))
    tag = label.encode("ascii")
    data[:len(tag) + 1] = tag + b"\x00"
    return bytes(data)

with open(image, "r+b") as disk:
    assert os.fstat(disk.fileno()).st_size == 16 * 1024 * 1024
    if operation == "prepare":
        for letter, sector in (("A", 16), ("B", 17)):
            disk.seek(sector * 512)
            disk.write(pattern(letter, "M178_HOST_SEED_" + letter))
    elif operation == "verify":
        for letter, sector, prefix in (("A", 16, "M178_HOST_SEED_"),
                                       ("B", 17, "M178_HOST_SEED_"),
                                       ("A", 32, "M178_GUEST_WRITE_"),
                                       ("B", 33, "M178_GUEST_WRITE_")):
            disk.seek(sector * 512)
            actual = disk.read(512)
            expected = pattern(letter, prefix + letter)
            if actual != expected:
                offset = next((i for i, (x, y) in enumerate(zip(actual, expected))
                               if x != y), "short")
                raise SystemExit(f"[M178 DISK FAIL] sector={sector} "
                                 f"byte={offset} actual={actual[:24]!r}")
        print("[M178 HOST DISK PASS] separate 16MiB QEMU raw VirtIO-BLK: "
              "two original host-seeded sectors preserved, two 512-byte "
              "guest-written sectors independently verified after FLUSH")
    else:
        raise SystemExit(2)
PY

# The M179 opt-in extension uses the same genuinely attached second disk,
# but restricts its ext4 MBR partition to LBA>=4096. Earlier M178 oracle
# sectors 16/17/32/33 remain outside the partition and independently tested.
if [ "${NAS_M179_EXT4_PARTITION:-0}" = 1 ]; then
    python3 scripts/m179-ext4-fixture.py prepare "$disk"
fi

# The pre-existing M177 probe boots once and runs the M174-M176 authentic
# software, network-wire, input-IRQ and five physical GPU-frame oracles.
# The async block drive is an additional physical QEMU device, not a module.
export NAS_ENABLE_VIRTIO_BLK_ASYNC=1 NAS_QEMU_ASYNC_BLK=1
export NAS_QEMU_ASYNC_BLK_IMAGE="$disk"
sh scripts/probe-m177-gpu-2d.sh "$port"

log="$logs/m174-batch.clean.log"
test -s "$log" || {
    echo "[M178 BLK FAIL] $port guest serial missing" >&2
    exit 1
}
transport=MMIO
[ "$port" != amd64 ] || transport=PCI
[ "$(grep -Fxc "[VIRTIO-BLK-ASYNC] real modern $transport devices=1" "$log" || :)" -eq 1 ] || {
    echo "[M178 BLK FAIL] $port no independent physical $transport asynchronous disk" >&2
    grep -aE 'VIRTIO-BLK|M178' "$log" >&2 || :
    exit 1
}
[ "$(grep -Fxc '[M178 ASYNC BLK NATIVE PASS]' "$log" || :)" -eq 1 ] || {
    echo "[M178 BLK FAIL] $port missing exactly-one guest async I/O verdict" >&2
    grep -a 'M178' "$log" >&2 || :
    exit 1
}

# Strict, unambiguous kernel counters: require two actually concurrent DMA
# requests, complete tag bookkeeping, hardware IRQ ACK and deferred drain.
awk '
    /^\[M178 ASYNC BLK STATS\] / {
        records++
        for (i=5; i<=NF; i++) {
            if (split($i, pair, "=") != 2 || pair[2] !~ /^[0-9]+$/) bad=1
            value[pair[1]]=pair[2]+0
            count[pair[1]]++
        }
    }
    END {
        if (records!=1 || bad) exit 1
        split("submitted completed max_inflight irq_ack deferred fallback_poll verified_reads verified_writes flushes", required, " ")
        for (i=1; i<=9; i++) if (count[required[i]] != 1) exit 1
        if (value["submitted"]<7 ||
            value["completed"]!=value["submitted"] ||
            value["max_inflight"]<2 || value["irq_ack"]<1 ||
            value["deferred"]<1 || value["fallback_poll"]!=0 ||
            value["verified_reads"]<4 || value["verified_writes"]<2 ||
            value["flushes"]<1) exit 1
    }
' "$log" || {
    echo "[M178 BLK FAIL] $port bad concurrent-request/tag/IRQ/deferred/no-poll counters" >&2
    grep -a 'M178 ASYNC BLK' "$log" >&2 || :
    exit 1
}

# QEMU has exited and closed the independent raw disk. Verify physical bytes,
# not the guest's report of its own memcpy/readback tests.
python3 - "$disk" verify <<'PY'
import os
import sys

image, operation = sys.argv[1:]
assert operation == "verify"

def pattern(letter, label):
    salt = 7 if letter == "A" else 19
    data = bytearray((i * 13 + salt) & 255 for i in range(512))
    tag = label.encode("ascii")
    data[:len(tag) + 1] = tag + b"\x00"
    return bytes(data)

with open(image, "rb") as disk:
    assert os.fstat(disk.fileno()).st_size == 16 * 1024 * 1024
    for letter, sector, prefix in (("A", 16, "M178_HOST_SEED_"),
                                   ("B", 17, "M178_HOST_SEED_"),
                                   ("A", 32, "M178_GUEST_WRITE_"),
                                   ("B", 33, "M178_GUEST_WRITE_")):
        disk.seek(sector * 512)
        actual = disk.read(512)
        expected = pattern(letter, prefix + letter)
        if actual != expected:
            offset = next((i for i, (x, y) in enumerate(zip(actual, expected))
                           if x != y), "short")
            raise SystemExit(f"[M178 DISK FAIL] sector={sector} "
                             f"byte={offset} actual={actual[:24]!r}")
print("[M178 HOST DISK PASS] separate 16MiB QEMU raw VirtIO-BLK: "
      "host seed preserved; two full 512-byte guest writes persisted")
PY
echo "[M178 ASYNC BLK NATIVE PASS] $port actual independent $transport disk, 2+ concurrent requests, IRQ/deferred zero poll, 2 seed reads, 2 durable write+flush+readbacks; M177 original 20 signed Alpine, Ncat and 5 QMP GPU frames preserved"

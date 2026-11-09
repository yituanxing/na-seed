#!/bin/sh
# M241: one native x86-64 QEMU boot of the original signed Alpine root, then
# request a real /sbin/poweroff through the authenticated guest serial shell.
# Unlike M193 this must leave QEMU through the kernel's deferred PID1/ext4
# completion.  No host-killed QEMU, parity timeout or clean host-only test can
# count as success.
set -eu
[ "$#" -eq 1 ] && [ "$1" = amd64 ] || {
    echo 'usage: sh scripts/probe-m241-alpine-poweroff.sh amd64' >&2
    exit 2
}
port=$1
. scripts/native-qemu-target.sh

for program in sudo docker readelf cpio sha256sum python3 openssl; do
    command -v "$program" >/dev/null || {
        echo "[M241 POWER OFF FAIL] missing $program" >&2
        exit 1
    }
done

work="build/external/m191-alpine-rootfs/$port"
root="$PWD/$work/rootfs"
baseline="$PWD/$work/alpine-root.img"
rescue="$PWD/$work/smoke/rescue/rootfs.cpio"
busybox="$PWD/$work/smoke/elf/busybox"
loader="$PWD/$work/smoke/elf/ld-musl-x86_64.so.1"

# The signed M191 staging path already verifies upstream Alpine's tarball,
# APK signatures, original ELF, MBR and physical ext4 readback.  Reuse it when
# explicitly requested and present; a fresh CI job stages exactly once.
if [ "${NAS_M241_REUSE_STAGED_ROOT:-0}" = 1 ]; then
    test -s "$baseline" && test -d "$root" || {
        echo '[M241 POWER OFF FAIL] requested absent signed Alpine staging' >&2
        exit 1
    }
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
else
    sh scripts/m191-stage-alpine-rootfs.sh "$port"
fi

mkdir -p "$PWD/$work/smoke/elf"
sudo python3 scripts/m191-export-alpine-elf.py "$port" "$root" "$PWD/$work/smoke/elf"
test -s "$busybox" && test -s "$loader"
readelf -h "$busybox" | grep -Fq 'Advanced Micro Devices X86-64'
readelf -h "$busybox" | grep -Eq 'Type:[[:space:]]+DYN'
readelf -l "$busybox" | grep -Fq '[Requesting program interpreter: /lib/ld-musl-x86_64.so.1]'
readelf -h "$loader" | grep -Fq 'Advanced Micro Devices X86-64'
sha256sum "$busybox" "$loader" > "$PWD/$work/smoke/original-alpine-elfs.sha256"
sh scripts/stage-alpine-native-rootfs.sh "$busybox" "$loader" "$PWD/$work/smoke/rescue"
test -s "$rescue"
if cpio -it --quiet < "$rescue" | grep -Eq '(^|/)etc/alpine-release$'; then
    echo '[M241 POWER OFF FAIL] rescue CPIO unexpectedly contains official root proof' >&2
    exit 1
fi

# The original upstream root and M191 baseline are never changed for login.
# CI-only password and securetty changes live on an independent copy whose
# ELF files are verified unchanged before it is written to disposable ext4.
overlay=$(mktemp -d "$PWD/$work/m241-auth-overlay.XXXXXX")
sudo cp -a "$root/." "$overlay/"
sudo python3 scripts/m193-alpine-login-overlay.py "$root" "$overlay"
disk="$PWD/$work/m241-poweroff-disposable.img"
sudo python3 scripts/m191-alpine-root-disk.py prepare "$port" "$overlay" "$disk"
sudo chown "$(id -u):$(id -g)" "$disk"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ACCEPTANCE_CPIO="$rescue"
export BUSYBOX_PARITY_BINARY="$busybox"
# The original x86 QEMU runner already owns the m193-batch /sbin/init Limine
# manifest, startup budget and exit-0 debug port. Reuse that one case only;
# do not run the independent M191 baseline or the old M193 host-kill proof.
export BUSYBOX_PARITY_CASE=m193-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m193-login-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_APP_BATCH_QEMU_TIMEOUT="${NAS_M241_QEMU_TIMEOUT:-180}"

# Always compile the changed kernel. A cached M191 .elf would test OLD PID1
# semantics and produce a misleading guest result.
. scripts/native-source-qemu.sh
nas_source_build_kernel "${NAS_M241_BUILD_TAG:-m241-poweroff}"
test -s "$NAS_ACCEPTANCE_KERNEL"

fifo="$PWD/$work/m241-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
runner_log="$PWD/$work/m241-poweroff-runner.log"
log="$PWD/$logs/m193-batch.serial.log"
python3 scripts/m241-alpine-poweroff-uart.py "$runner" "$log" "$runner_log" "$fifo"

# Only reached when original PID1 signal + final poweroff(2), actual deferred
# upstream ext4 unmount, and native QEMU exit all succeeded. Recheck the same
# mutable guest disk after QEMU exit; no synthetic fsck or external kill PASS.
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$overlay" "$disk"
python3 scripts/m241-verify-poweroff-disk.py "$disk"
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$baseline"
(
    cd "$PWD"
    sha256sum -c "$work/smoke/original-alpine-elfs.sha256"
)
cp "$log" "$PWD/$work/m241-poweroff.serial.log"
echo '[M241 ORIGINAL ALPINE POWER OFF NATIVE PASS] x86-64 original /sbin/init + authenticated /sbin/poweroff -> true PID1 shutdown, ext4 durable clean unmount, natural QEMU exit and physical disk readback'

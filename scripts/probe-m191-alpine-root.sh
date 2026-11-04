#!/bin/sh
# M191: one genuine system-QEMU boot of original Alpine/musl BusyBox from
# physical ext4 `/` on each ISA.  Keep every historical parity exit oracle;
# this independent wrapper additionally proves the mounted filesystem owner.
set -eu
[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/probe-m191-alpine-root.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
case "$port" in
    amd64) alpine_arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) alpine_arch=riscv64; machine='RISC-V' ;;
    arm64) alpine_arch=aarch64; machine='AArch64' ;;
    *) echo "[M191 QEMU FAIL] unsupported ISA: $port" >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh

for program in readelf cpio sha256sum grep; do
    command -v "$program" >/dev/null || {
        echo "[M191 QEMU FAIL] missing $program" >&2
        exit 1
    }
done

work="build/external/m191-alpine-rootfs/$port"
root="$work/rootfs"
disk="$PWD/$work/alpine-root.img"
# Never silently reuse a different disk: host staging validates the complete
# official minirootfs, signed APK metadata, partition and ext4 contents.
if [ "${NAS_M191_REUSE_STAGED_ROOT:-0}" = 1 ]; then
    test -s "$disk" && test -d "$root" || {
        echo '[M191 QEMU FAIL] requested reuse but signed Alpine root is absent' >&2
        exit 1
    }
    sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$disk"
else
    sh scripts/m191-stage-alpine-rootfs.sh "$port"
fi

mkdir -p "$work/smoke/elf"
# Alpine can contain a guest-absolute /bin or /lib symlink.  Resolve inside
# its own root before exporting original bytes for the rescue module.
# The exact root-owned Alpine tree intentionally contains mode 4711/0700
# privileged helpers such as bbsuid. Never weaken their original modes or
# chown the rootfs to satisfy a host smoke: inspect it as root instead.
sudo python3 scripts/m191-export-alpine-elf.py "$port" "$root" "$work/smoke/elf"
busybox="$work/smoke/elf/busybox"
loader="$work/smoke/elf/ld-musl-$alpine_arch.so.1"
test -f "$busybox" && test -s "$busybox" && test -s "$loader" || {
    echo '[M191 QEMU FAIL] original Alpine BusyBox/musl is missing' >&2
    exit 1
}
readelf -h "$busybox" | grep -Fq "$machine"
readelf -h "$busybox" | grep -Eq 'Type:[[:space:]]+DYN'
readelf -l "$busybox" | grep -Fq "[Requesting program interpreter: /lib/ld-musl-$alpine_arch.so.1]"
readelf -h "$loader" | grep -Fq "$machine"
sha256sum "$busybox" "$loader" > "$work/smoke/original-alpine-elfs.sha256"

# M191 selects its PID1 from ext4 AFTER indexing this independently staged
# rescue archive.  It must be executable if root selection fails, but the
# root's distinctive Alpine release file deliberately is NOT in this CPIO.
rescue="$work/smoke/rescue"
sh scripts/stage-alpine-native-rootfs.sh "$busybox" "$loader" "$rescue"
test -s "$rescue/rootfs.cpio"
if cpio -it --quiet < "$rescue/rootfs.cpio" | \
    grep -Eq '(^|/)etc/alpine-release$'; then
    echo '[M191 QEMU FAIL] rescue CPIO unexpectedly contains Alpine root evidence' >&2
    exit 1
fi

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1
export NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ACCEPTANCE_CPIO="$PWD/$rescue/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$PWD/$busybox"
# The canonical m1??-batch name reuses the existing larger native-QEMU
# startup budget without changing any runner's exit-0 PASS conditions.
export BUSYBOX_PARITY_CASE=m191-batch BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m191-rootfs-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=1024M

# The multi-ISA runners share the same core and exact CPIO/argv semantics,
# but the ISA builds do NOT share a build directory.  Build explicitly: both
# port matrix scripts skip rebuilding when a kernel.elf already exists.
if [ "$port" = amd64 ]; then
    . scripts/native-source-qemu.sh
    nas_source_build_kernel m191-root
else
    case "$port" in
        riscv64) sh scripts/build-riscv64-full.sh ;;
        arm64) sh scripts/build-aarch64-full.sh ;;
    esac
fi

if ! sh "$runner"; then
    echo "[M191 QEMU FAIL] $port original Alpine BusyBox from real ext4 root" >&2
    test ! -s "$logs/m191-batch.clean.log" || \
        tail -150 "$logs/m191-batch.clean.log" >&2
    exit 1
fi

log="$logs/m191-batch.clean.log"
test -s "$log" || {
    echo "[M191 QEMU FAIL] $port missing physical QEMU serial log" >&2
    exit 1
}
for exact in \
    '[FS] CPIO newc read-only archive mounted' \
    '[M191 ROOT] physical Alpine ext4 registered at /' \
    '[LWEXT4 GUEST] real GPT/MBR partition ext4 disk mounted' \
    '[M191 ROOT] original Alpine PID1 from mounted ext4: /bin/busybox' \
    '[LWEXT4 GUEST] clean upstream unmount and durable flush' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0' \
    '3.23.7'; do
    if ! grep -Fxq "$exact" "$log"; then
        echo "[M191 QEMU FAIL] $port missing real guest evidence: $exact" >&2
        tail -150 "$log" >&2
        exit 1
    fi
done
if grep -Fq '[FAIL]' "$log"; then
    echo "[M191 QEMU FAIL] $port guest signalled failure" >&2
    tail -150 "$log" >&2
    exit 1
fi

# Verify the guest really left a clean physical ext4 volume, with its Alpine
# package bytes unchanged.  This is a host readback AFTER actual QEMU exit.
sudo python3 scripts/m191-alpine-root-disk.py verify "$port" "$root" "$disk"
(
    cd "$PWD"
    sha256sum -c "$work/smoke/original-alpine-elfs.sha256"
)
echo "[M191 ALPINE ROOT NATIVE PASS] $port real QEMU ext4 /, original signed Alpine BusyBox + musl ELF, Alpine base/OpenRC root, clean PID1 exit and host ext4 readback"

# `/sbin/init` is a separate, intentionally non-exiting profile.  It needs
# independently witnessed console/service readiness and fatal/early-exit
# monitoring; the exit-0 parity runner above must never be weakened for it.

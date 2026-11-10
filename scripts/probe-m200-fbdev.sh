#!/bin/sh
# One physical VirtIO-GPU MAP_SHARED framebuffer witness per native ISA.
# No Alpine package graph, OpenRC desktop, network or persistent disk: a
# single static C ELF runs directly from a tiny genuine Limine CPIO.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m200-fbdev.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) cc=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64) cc=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64) cc=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) echo "[M200 FAIL] unsupported native port: $port" >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh
work="build/external/m200-fbdev/$port"
mkdir -p "$work/rootfs/bin" "$work/rootfs/dev" "$work/rootfs/tmp" "$logs"
guest="$work/naseed-fb-mmap.elf"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    scripts/m200-fbdev-mmap.c -o "$guest"
readelf -h "$guest" | grep -Fq "$machine"
if readelf -l "$guest" | grep -Fq INTERP; then
    echo "[M200 FAIL] $port guest ELF must be static (no PT_INTERP)" >&2
    exit 1
fi
cp "$guest" "$work/rootfs/bin/naseed-fb-mmap"
chmod 755 "$work/rootfs/bin/naseed-fb-mmap"
chmod 1777 "$work/rootfs/tmp"
(
    cd "$work/rootfs"
    printf '%s\n' . bin bin/naseed-fb-mmap dev tmp |
        cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"

# The canonical parity runners accept a non-BusyBox NAS_ACCEPTANCE_CPIO.
# Their source-binary input is unused in cpio mode; provide this same exact
# static client ELF instead of downloading a completely unrelated package.
export BUSYBOX_PARITY_BINARY="$PWD/$guest"
export BUSYBOX_PARITY_MANIFEST=scripts/m200-fbdev-cases.tsv
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_CPIO="$PWD/$work/rootfs.cpio"
export NAS_ENABLE_VIRTIO_GPU=1 NAS_QEMU_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
export NAS_QEMU_INPUT_OPTIN=0 NAS_ACCEPTANCE_QEMU_RAM=512M
export NAS_APP_BATCH_QEMU_TIMEOUT=90
export NAS_QEMU_QMP_SOCKET="$PWD/$work/m200.qmp"

# Runtime flags are NOT make dependencies. An old full-kernel ELF may exist
# from a headless/M199 profile without GPU support: always rebuild exactly
# this native kernel under the M200 GPU feature gate before the runner.
case "$port" in
    amd64) make -B kernel ;;
    riscv64) sh scripts/build-riscv64-full.sh ;;
    arm64) sh scripts/build-aarch64-full.sh ;;
esac

serial="$logs/m200-fbdev.serial.log"
clean="$logs/m200-fbdev.clean.log"
oracle="$work/m200-qmp-check.log"
screen="$work/m200-qmp-screen.ppm"
rm -f "$NAS_QEMU_QMP_SOCKET" "$screen" "$oracle"
: > "$serial"
python3 scripts/m200-qmp-verify.py --port "$port" \
    --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
    --screenshot "$screen" --timeout 90 > "$oracle" 2>&1 &
watcher=$!
cleanup() {
    if [ -n "${watcher:-}" ]; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
    fi
    # Keep native guest logs on either success or failure for exact-head CI
    # diagnostics, even when QEMU exits before the QMP image is available.
    if [ -f "$serial" ]; then
        cp "$serial" "$work/m200-guest.serial.log"
    fi
    if [ -f "$clean" ]; then
        cp "$clean" "$work/m200-guest.clean.log"
    fi
}
trap cleanup EXIT HUP INT TERM

if ! sh "$runner"; then
    echo "[M200 NATIVE FAIL] $port real guest mmap case failed" >&2
    test ! -f "$clean" || tail -120 "$clean" >&2
    test ! -f "$oracle" || tail -60 "$oracle" >&2
    exit 1
fi
if ! wait "$watcher"; then
    watcher=
    echo "[M200 QMP FAIL] $port physical screenshot check failed" >&2
    tail -80 "$oracle" >&2
    exit 1
fi
watcher=
grep -Fxq "[M200 QMP PHYSICAL PIXEL PASS] $port 800x600 VirtIO-GPU four exact RGB samples from MAP_SHARED forked guest bytes" "$oracle"
grep -Fxq M200_FBIOGET_AND_SHARED_MMAP_PASS "$clean"
grep -Fxq M200_DEVICE_FORK_AND_FD_CLOSE_PASS "$clean"
grep -Fxq M200_SCREEN_DRAWN "$clean"
grep -Fxq M200_FB_MMAP_GUEST_PASS "$clean"
grep -Fq '[PASS] cmdline userspace exit 0' "$clean"
test -s "$screen"
echo "[M200 NATIVE PHYSICAL MMAP PASS] $port static guest, fd-close/fork/real shared VMA, exact four QEMU GPU pixels"

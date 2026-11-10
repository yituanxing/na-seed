#!/bin/sh
# Exactly one original Alpine system-QEMU/OpenRC boot per native ISA. Its
# original signed fbi displays two original PNG files under a real Linux VT;
# independent host QMP injects hardware keyboard Space/q and checks GPU RGB.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m202-alpine-interactive-fbi.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
sh scripts/stage-m202-alpine-interactive-fbi.sh "$port"
work="$PWD/build/external/m202-alpine-interactive-fbi/$port"
disk="$work/m202-original-interactive-fbi.img"
rescue="$PWD/build/external/m191-alpine-rootfs/$port/smoke/rescue/rootfs.cpio"
busybox="$PWD/build/external/m191-alpine-rootfs/$port/smoke/elf/busybox"
test -s "$disk" && test -s "$rescue" && test -s "$busybox"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_ENABLE_VIRTIO_GPU=1 NAS_QEMU_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
export NAS_ENABLE_VIRTIO_INPUT=1 NAS_QEMU_INPUT_OPTIN=1
export NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m202-fbi BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m202-fbi-cases.tsv
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M202_DIAG_SECONDS:-280}
# AF_UNIX sun_path is at most 107 pathname bytes on Linux. CI's checkout
# prefix already consumes most of that budget; use one short per-ISA socket
# rather than embedding the long signed-Alpine artifact directory.
export NAS_QEMU_QMP_SOCKET="$PWD/build/m202-$port.qmp"
if [ "${#NAS_QEMU_QMP_SOCKET}" -ge 108 ]; then
    echo '[M202 QMP FAIL] UNIX socket pathname exceeds Linux sun_path' >&2
    exit 1
fi

# No stale headless kernel; build once with both real hardware classes.
case "$port" in
    amd64)
        make -B kernel
        export NAS_ACCEPTANCE_KERNEL="$PWD/build/kernel.elf" ;;
    riscv64) sh scripts/build-riscv64-full.sh ;;
    arm64) sh scripts/build-aarch64-full.sh ;;
esac

fifo="$work/m202-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/m202-fbi.serial.log"
clean="$PWD/$logs/m202-fbi.clean.log"
runner_log="$work/m202-native-runner.log"
qmp_log="$work/m202-original-fbi-qmp.log"
first="$work/m202-first-physical-gpu.ppm"
second="$work/m202-second-physical-gpu.ppm"
success="$work/m202-physical-qmp-success"
failure="$work/m202-physical-qmp-failure"
rm -f "$NAS_QEMU_QMP_SOCKET" "$first" "$second" "$success" "$failure" "$qmp_log" "$fifo"
mkdir -p "$PWD/$logs"
: > "$serial"
python3 scripts/m202-qmp-alpine-interactive-fbi.py \
    --port "$port" --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
    --first "$first" --second "$second" \
    --success-file "$success" --failure-file "$failure" \
    --timeout "${NAS_M202_QMP_SECONDS:-265}" > "$qmp_log" 2>&1 &
watcher=$!
cleanup() {
    if [ -n "${watcher:-}" ]; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
    fi
}
trap cleanup EXIT HUP INT TERM

if ! python3 scripts/m202-alpine-uart.py "$runner" "$serial" \
        "$runner_log" "$fifo" "$success" "$failure"; then
    echo "[M202 NATIVE FAIL] $port original fbi interactive session failed" >&2
    test ! -s "$qmp_log" || tail -90 "$qmp_log" >&2
    test ! -s "$runner_log" || tail -90 "$runner_log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    watcher=
    echo "[M202 PHYSICAL QMP FAIL] $port real hardware input/frames" >&2
    tail -90 "$qmp_log" >&2
    exit 1
fi
watcher=

test -s "$first" && test -s "$second" && test -s "$success" && test -s "$clean"
grep -Fxq M202_APK_VT_RC=0 "$clean"
grep -Fxq M202_FBI_START "$clean"
grep -Fxq M202_FBI_EXEC_RC=0 "$clean"
grep -Fxq M202_VT_RESTORED_RC=0 "$clean"
grep -Fq '[M191 ROOT] physical Alpine ext4 registered at /' "$clean"
grep -Fq '[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init' "$clean"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/fbi' "$clean"
grep -Fq "[M202 QMP ORIGINAL INTERACTIVE FBI PASS] $port" "$qmp_log"
grep -Fxq M202_ORIGINAL_INTERACTIVE_FBI_QMP_PASS "$success"
if grep -Fq '[FAIL]' "$clean"; then
    echo '[M202 NATIVE FAIL] actual QEMU boot/kernel failure' >&2
    exit 1
fi
echo "[M202 REAL ALPINE INTERACTIVE FBI PASS] $port original signed fbi on virtual tty, two exact independent QMP screen images, real VirtIO keyboard Space+q, Alpine session restored"

#!/bin/sh
# Real Alpine 3.23, original signed fbida-fbi APK, one physical QEMU per ISA.
# No M197 full-network suite, M191 preparatory QEMU, M198 desktop or guest
# replacement fbi. QMP checks independent 800x600 output while fbi runs.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m201-alpine-fbi.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
sh scripts/stage-m201-alpine-fbi.sh "$port"
work="$PWD/build/external/m201-alpine-fbi/$port"
disk="$work/m201-original-fbi.img"
rescue="$PWD/build/external/m191-alpine-rootfs/$port/smoke/rescue/rootfs.cpio"
busybox="$PWD/build/external/m191-alpine-rootfs/$port/smoke/elf/busybox"
test -s "$disk" && test -s "$rescue" && test -s "$busybox"

# The physical disk, real original Alpine PID1 and GPU framebuffer are all
# mandatory. The rescue CPIO is never permitted to substitute for /sbin/init.
export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_ENABLE_VIRTIO_GPU=1 NAS_QEMU_VIRTIO_GPU=1 NAS_QEMU_RAMFB=0
export NAS_QEMU_INPUT_OPTIN=0 NAS_ACCEPTANCE_QEMU_RAM=1024M
export NAS_VIRTIO_BLK_IMAGE="$disk" NAS_ACCEPTANCE_CPIO="$rescue"
export BUSYBOX_PARITY_BINARY="$busybox"
export BUSYBOX_PARITY_CASE=m201-fbi BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m201-fbi-cases.tsv
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M201_DIAG_SECONDS:-240}
export NAS_QEMU_QMP_SOCKET="$work/m201-fbi.qmp"

# Feature flags are not make dependencies: never reuse a headless kernel.
# Only one feature-correct kernel build + one native original-Alpine QEMU boot.
case "$port" in
    amd64)
        make -B kernel
        export NAS_ACCEPTANCE_KERNEL="$PWD/build/kernel.elf" ;;
    riscv64) sh scripts/build-riscv64-full.sh ;;
    arm64) sh scripts/build-aarch64-full.sh ;;
esac

fifo="$work/m201-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
serial="$PWD/$logs/m201-fbi.serial.log"
clean="$PWD/$logs/m201-fbi.clean.log"
runner_log="$work/m201-native-runner.log"
qmp_log="$work/m201-original-fbi-qmp.log"
screen="$work/m201-original-fbi-screen.ppm"
success="$work/m201-physical-qmp-success"
failure="$work/m201-physical-qmp-failure"
rm -f "$NAS_QEMU_QMP_SOCKET" "$screen" "$success" "$failure" "$qmp_log"
mkdir -p "$PWD/$logs"
: > "$serial"
python3 scripts/m201-qmp-alpine-fbi.py \
    --port "$port" --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
    --screenshot "$screen" --success-file "$success" \
    --failure-file "$failure" \
    --timeout "${NAS_M201_QMP_SECONDS:-235}" > "$qmp_log" 2>&1 &
watcher=$!
cleanup() {
    if [ -n "${watcher:-}" ]; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
    fi
}
trap cleanup EXIT HUP INT TERM

if ! python3 scripts/m201-alpine-uart.py "$runner" "$serial" \
        "$runner_log" "$fifo" "$success" "$failure"; then
    echo "[M201 NATIVE FAIL] $port original fbi not successfully run" >&2
    test ! -s "$qmp_log" || tail -65 "$qmp_log" >&2
    test ! -s "$runner_log" || tail -65 "$runner_log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    watcher=
    echo "[M201 QMP FAIL] $port original Alpine image not physically displayed" >&2
    tail -85 "$qmp_log" >&2
    exit 1
fi
watcher=

test -s "$screen" && test -s "$success"
test -s "$clean"
grep -Fxq 'M201_APK_FBI_RC=0' "$clean"
grep -Fxq 'M201_FBI_START' "$clean"
grep -Fxq 'M201_FBI_EXEC_RC=0' "$clean"
grep -Fxq 'M201_FBI_FINAL_RC=0' "$clean"
grep -Fq '[M191 ROOT] physical Alpine ext4 registered at /' "$clean"
grep -Fq '[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init' "$clean"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/fbi' "$clean"
grep -Fq "[M201 QMP PHYSICAL PIXEL PASS] $port" "$qmp_log"
grep -Fxq 'M201_ORIGINAL_FBI_QMP_PHYSICAL_PASS' "$success"
if grep -Fq '[FAIL]' "$clean"; then
    echo '[M201 NATIVE FAIL] real guest kernel/boot failure' >&2
    exit 1
fi
echo "[M201 REAL SIGNED ALPINE FBI PASS] $port original Alpine PID1/OpenRC/login "\
     'and official unmodified fbi ELF, actual app exit 0, independent QMP four exact hardware RGB pixels'

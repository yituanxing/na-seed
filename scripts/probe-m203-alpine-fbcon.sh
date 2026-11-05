#!/bin/sh
# Native three-ISA M203: original Alpine graphical getty/login/ash on VT1,
# signed interactive fbi KD_GRAPHICS/VT text restore, physical GPU+QMP keys.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: probe-m203-alpine-fbcon.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
sh scripts/stage-m203-alpine-fbcon.sh "$port"
work="$PWD/build/external/m203-alpine-fbcon/$port"
disk="$work/m203-original-alpine-console.img"
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
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M203_DIAG_SECONDS:-390}
export NAS_QEMU_QMP_SOCKET="$PWD/build/m203-$port.qmp"
if [ "${#NAS_QEMU_QMP_SOCKET}" -ge 108 ]; then
    echo '[M203 QMP FAIL] AF_UNIX socket pathname exceeds Linux sun_path' >&2
    exit 1
fi

case "$port" in
    amd64)
        make -B kernel
        export NAS_ACCEPTANCE_KERNEL="$PWD/build/kernel.elf" ;;
    riscv64) sh scripts/build-riscv64-full.sh ;;
    arm64) sh scripts/build-aarch64-full.sh ;;
esac

fifo="$work/m203-serial-input.fifo"
export NAS_QEMU_SERIAL_FIFO="$fifo"
# The shared native runner names its UART after BUSYBOX_PARITY_CASE, not
# this enclosing M203 probe. Watching a different file silently times out.
serial="$PWD/$logs/$BUSYBOX_PARITY_CASE.serial.log"
clean="$PWD/$logs/m203-fbcon.clean.log"
runner_log="$work/m203-native-runner.log"
qmp_log="$work/m203-original-fbcon-qmp.log"
success="$work/m203-physical-qmp-success"
failure="$work/m203-physical-qmp-failure"
outdir="$work/physical-gpu"
rm -f "$NAS_QEMU_QMP_SOCKET" "$success" "$failure" "$qmp_log" "$fifo"
mkdir -p "$PWD/$logs" "$outdir"
: > "$serial"
python3 scripts/m203-qmp-alpine-fbcon.py \
    --port "$port" --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
    --outdir "$outdir" --success-file "$success" --failure-file "$failure" \
    --timeout "${NAS_M203_QMP_SECONDS:-375}" > "$qmp_log" 2>&1 &
watcher=$!
cleanup() {
    if [ -n "${watcher:-}" ]; then
        kill "$watcher" 2>/dev/null || :
        wait "$watcher" 2>/dev/null || :
    fi
}
trap cleanup EXIT HUP INT TERM

if ! python3 scripts/m203-alpine-uart.py "$runner" "$serial" \
        "$runner_log" "$fifo" "$success" "$failure"; then
    echo "[M203 NATIVE FAIL] $port original Alpine graphical VT/fbi" >&2
    test ! -s "$qmp_log" || tail -110 "$qmp_log" >&2
    test ! -s "$runner_log" || tail -90 "$runner_log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    watcher=
    echo "[M203 PHYSICAL QMP FAIL] $port GPU glyph/keyboard/fbi" >&2
    tail -110 "$qmp_log" >&2
    exit 1
fi
watcher=

test -s "$success" && test -s "$clean"
for marker in M203_TTY1_GETTY_RC=0 M203_TYPED_FILE_RC=0 M203_RESTORED_FILE_RC=0; do
    grep -Fxq "$marker" "$clean"
done
grep -Fq '[M191 ROOT] original Alpine PID1 from mounted ext4: /sbin/init' "$clean"
for app in /sbin/getty /bin/login /bin/sh /usr/bin/fbi; do
    grep -Fq "[EXEC] replaced process with lwext4 ELF: $app" "$clean"
done
grep -Fq "[M203 ORIGINAL ALPINE FBICON PHYSICAL PASS] $port" "$qmp_log"
grep -Fxq M203_FBICON_PHYSICAL_QMP_PASS "$success"
if grep -Fq '[FAIL]' "$clean"; then
    echo '[M203 NATIVE FAIL] guest kernel reported failure' >&2
    exit 1
fi
echo "[M203 REAL ALPINE FRAMEBUFFER CONSOLE PASS] $port original tty1 getty/login/ash, physical glyphs and keyboard, two genuine signed fbi images, KD_GRAPHICS to KD_TEXT, ext4 durable roundtrip"

#!/bin/sh
# Three ISA native-QEMU real hardware pixels + original Alpine PNG roundtrip.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
. scripts/native-qemu-target.sh
sh scripts/stage-m161-prebuilt.sh "$port"
work="build/external/m161-batch/$port"
mkdir -p "$work"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m161-batch-cases.tsv
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export NAS_QEMU_RAMFB=1
export NAS_QEMU_QMP_SOCKET="$work/m161.qmp"
log="$logs/m161-batch.clean.log"
serial="$logs/m161-batch.serial.log"
screen="$work/m161-qmp-screen.ppm"
qmp_log="$work/m161-qmp-check.log"
python3 scripts/m161-qmp-verify.py \
    --socket "$NAS_QEMU_QMP_SOCKET" --serial "$serial" \
    --screenshot "$screen" --timeout 700 > "$qmp_log" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M161 QEMU FAIL] $port guest framebuffer program" >&2
    test ! -s "$log" || tail -200 "$log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M161 QMP FAIL] $port physical screen assertion" >&2
    test ! -s "$qmp_log" || tail -90 "$qmp_log" >&2
    exit 1
fi
grep -Fxq M161_FRAMEBUFFER_PASS "$log"
grep -Fxq M161_FBGRAB_READ_PNG_PASS "$log"
# A packed BGRA strip is valid only for this *observed*, not assumed,
# framebuffer contract on the requested native QEMU firmware.
grep -aE '^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null || {
    echo "[M161 MODE FAIL] $port not a real 32-bit RGB/BGRA framebuffer" >&2
    exit 1
}
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/magick' "$log"
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/fbgrab' "$log"
grep -Fq '[M161 QMP DISPLAY PASS]' "$qmp_log"
echo "[M161 NATIVE DISPLAY PASS] $port original PNG capture and visible QEMU hardware pixels"

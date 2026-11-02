#!/bin/sh
# Real UART -> shared N_TTY -> POSIX read -> firmware FB, and independent
# QEMU screen captures before/after typed input on ALL THREE native ISAs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
export NAS_QEMU_RAMFB=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m164-batch-cases.tsv
sh scripts/stage-m164-prebuilt.sh "$port"
work="build/external/m164-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m164.qmp"
export NAS_QEMU_SERIAL_FIFO="$work/m164-input.fifo"
test -p "$NAS_QEMU_SERIAL_FIFO" || mkfifo "$NAS_QEMU_SERIAL_FIFO"
log="$logs/m164-batch.clean.log"
serial="$logs/m164-batch.serial.log"
oracle_log="$work/m164-serial-qmp-check.log"
# Never consume a prior run's READY token if this test is rerun locally.
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 scripts/m164-serial-qmp-verify.py \
    --fifo "$NAS_QEMU_SERIAL_FIFO" --serial "$serial" \
    --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" --timeout 700 \
    > "$oracle_log" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M164 NATIVE FAIL] $port actual user-driven display" >&2
    test ! -s "$log" || tail -200 "$log" >&2
    test ! -s "$oracle_log" || tail -80 "$oracle_log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M164 QMP FAIL] $port serial keyboard or physical pixels" >&2
    tail -120 "$oracle_log" >&2
    exit 1
fi
grep -Fq '[M164 SERIAL->QMP PASS]' "$oracle_log"
grep -aE '^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M164 MARKER FAIL] $port $marker" >&2; exit 1;
    }
done < scripts/m164-expected-markers.txt
for executable in fbgrab magick; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$executable" "$log" || exit 1
done
for screenshot in first unchanged flipped; do
    test -s "$work/m164-$screenshot.ppm" || exit 1
done
echo "[M164 NATIVE INPUT/DISPLAY PASS] $port hardware UART -> original Linux graphics -> three real QMP screens"

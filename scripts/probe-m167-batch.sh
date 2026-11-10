#!/bin/sh
# A genuine multi-process userspace desktop accepted against physical QEMU
# input and framebuffer, not an in-kernel compositor or serial-only mock.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
export NAS_QEMU_RAMFB=1
export NAS_QEMU_INPUT_OPTIN=1
export NAS_ENABLE_VIRTIO_INPUT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m167-batch-cases.tsv
sh scripts/stage-m167-prebuilt.sh "$port"
work="build/external/m167-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m167.qmp"
log="$logs/m167-batch.clean.log"
serial="$logs/m167-batch.serial.log"
oracle_log="$work/m167-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 scripts/m167-qmp-verify.py \
    --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
    --outdir "$work" --timeout 700 > "$oracle_log" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M167 NATIVE FAIL] $port userspace compositor / app IPC" >&2
    test ! -s "$log" || tail -240 "$log" >&2
    test ! -s "$oracle_log" || tail -100 "$oracle_log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M167 QMP FAIL] $port hardware input or screen oracle" >&2
    tail -120 "$oracle_log" >&2
    exit 1
fi
grep -Fq '[M167 QMP DESKTOP PASS]' "$oracle_log"
grep -aE '^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M167 MARKER FAIL] $port expected exactly once: $marker" >&2
        exit 1
    }
done < scripts/m167-expected-markers.txt
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/magick' "$log"
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/dialog' "$log"
for screenshot in initial image-focus image-drag terminal-focus terminal-key; do
    test -s "$work/m167-$screenshot.ppm" || exit 1
done
echo "[M167 NATIVE DESKTOP PASS] $port QEMU VirtIO input, separate image+terminal processes, Unix IPC, five physical framebuffer screenshots"

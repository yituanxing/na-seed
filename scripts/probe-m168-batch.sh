#!/bin/sh
# Three independent desktop clients on real QEMU VirtIO input and RAMFB.
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
export BUSYBOX_PARITY_MANIFEST=scripts/m168-batch-cases.tsv
sh scripts/stage-m168-prebuilt.sh "$port"
work="build/external/m168-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m168.qmp"
log="$logs/m168-batch.clean.log"
serial="$logs/m168-batch.serial.log"
oracle="$work/m168-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 scripts/m168-qmp-verify.py --serial "$serial" \
    --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" --timeout 700 \
    > "$oracle" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M168 NATIVE FAIL] $port" >&2
    test ! -s "$log" || tail -240 "$log" >&2
    test ! -s "$oracle" || tail -90 "$oracle" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M168 QMP FAIL] $port" >&2
    tail -110 "$oracle" >&2
    exit 1
fi
grep -Fq '[M168 QMP THREE CLIENTS PASS]' "$oracle"
grep -aE '^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M168 MARKER FAIL] $port expected exactly once: $marker" >&2
        exit 1
    }
done < scripts/m168-expected-markers.txt
for original in /usr/bin/magick /usr/bin/dialog /usr/bin/lynx /usr/bin/mc; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: $original" "$log"
done
for shot in initial image-focus image-drag browser-focus browser-key terminal-focus terminal-key; do
    test -s "$work/m168-$shot.ppm"
done
echo "[M168 NATIVE DESKTOP PASS] $port four native ELFs, seven true QEMU screen captures, signed Lynx/MC, PTY terminal glyphs"

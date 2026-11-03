#!/bin/sh
# One final-head native QEMU gate per ISA: actual typed PCI/MMIO VirtIO input
# plus a fifth externally launched LVGL client over real AF_UNIX/SCM_RIGHTS.
# Reuse M171's signed overlay, LVGL and QMP platform; do not replay every
# historical Alpine matrix or treat a serial PASS token as visual proof.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
export NAS_QEMU_RAMFB=1 NAS_QEMU_INPUT_OPTIN=1 NAS_ENABLE_VIRTIO_INPUT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m173-batch-cases.tsv
sh scripts/stage-m173-dynamic-client.sh "$port"
work="build/external/m168-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m173.qmp"
log="$logs/m173-batch.clean.log"
serial="$logs/m173-batch.serial.log"
oracle="$work/m173-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 scripts/m173-qmp-verify.py --serial "$serial" \
  --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" --timeout 700 \
  > "$oracle" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M173 NATIVE FAIL] $port" >&2
    test ! -s "$log" || tail -220 "$log" >&2
    test ! -s "$oracle" || tail -120 "$oracle" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M173 QMP FAIL] $port" >&2
    tail -130 "$oracle" >&2
    exit 1
fi
grep -Fq '[M173 QMP DYNAMIC PASS]' "$oracle"
grep -aE '^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M173 MARKER FAIL] $port expected once: $marker" >&2
        exit 1
    }
done < scripts/m173-expected-markers.txt
for original in /usr/bin/magick /usr/bin/lynx /usr/bin/mc; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: $original" "$log"
done
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/gui' "$log" || {
    echo "[M173 EXEC FAIL] $port BusyBox did not launch fifth GUI ELF" >&2
    exit 1
}
for frame in four five clicked closed shell-after; do
    test -s "$work/m173-$frame.ppm" || exit 1
done
echo "[M173 NATIVE PASS] $port real PTY BusyBox launches second upstream LVGL; five independent QMP frames, click, close, surviving shell"

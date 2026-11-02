#!/bin/sh
# Real QEMU input hardware -> one shared Linux-compatible evdev path ->
# original signed Alpine evtest -> user's framebuffer program -> actual QMP
# screenshots, for x86_64, RV64, and ARM64 native guest execution.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
# Opt-in only; every historical headless/serial QEMU workload is unchanged.
export NAS_QEMU_RAMFB=1
export NAS_QEMU_INPUT_OPTIN=1
export NAS_ENABLE_VIRTIO_INPUT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m166-batch-cases.tsv
sh scripts/stage-m166-prebuilt.sh "$port"
work="build/external/m166-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m166.qmp"
log="$logs/m166-batch.clean.log"
serial="$logs/m166-batch.serial.log"
oracle_log="$work/m166-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 scripts/m166-qmp-verify.py \
    --serial "$serial" --qmp "$NAS_QEMU_QMP_SOCKET" \
    --outdir "$work" --timeout 700 > "$oracle_log" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M166 NATIVE FAIL] $port physical keyboard/tablet/mouse -> evdev" >&2
    test ! -s "$log" || tail -240 "$log" >&2
    test ! -s "$oracle_log" || tail -100 "$oracle_log" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M166 QMP FAIL] $port actual keyboard and mouse or framebuffer" >&2
    tail -120 "$oracle_log" >&2
    exit 1
fi
grep -Fq '[M166 INPUT->QMP PASS]' "$oracle_log"
grep -aE '^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M166 MARKER FAIL] $port expected exactly one: $marker" >&2
        exit 1
    }
done < scripts/m166-expected-markers.txt
test "$(grep -Fc '[EXEC] replaced process with CPIO ELF: /usr/bin/evtest' "$log" || :)" -eq 2
for exe in fbgrab magick; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$exe" "$log" || exit 1
done
for screenshot in initial key abs click rel; do
    test -s "$work/m166-$screenshot.ppm" || exit 1
done
echo "[M166 NATIVE INPUT/DISPLAY PASS] $port QEMU VirtIO keyboard+tablet+mouse, signed evtest x2, signed fbgrab/magick, 5 genuine QMP framebuffer screenshots"

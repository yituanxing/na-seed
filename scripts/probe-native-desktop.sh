#!/bin/sh
# Native desktop QEMU: one runner, six independent guest and physical-display gates.
set -eu
[ "$#" -eq 2 ] || exit 2
profile=$1
port=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
export NAS_QEMU_RAMFB=1 NAS_QEMU_INPUT_OPTIN=1 NAS_ENABLE_VIRTIO_INPUT=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700 BUSYBOX_PARITY_EXPECTED_COUNT=1
BUSYBOX_PARITY_MANIFEST=$(sh scripts/acceptance/batch-manifest.sh "$profile")
export BUSYBOX_PARITY_MANIFEST
case "$profile" in
    m166) sh scripts/stage-alpine-display.sh "$port" m166-batch; work="build/external/m166-batch/$port" ;;
    m167) sh scripts/stage-alpine-display.sh "$port" m167-batch; work="build/external/m167-batch/$port" ;;
    m168) sh scripts/stage-alpine-display.sh "$port" m168-batch; work="build/external/m168-batch/$port" ;;
    m170) sh scripts/stage-alpine-display.sh "$port" m168-batch; work="build/external/m168-batch/$port" ;;
    m171) sh scripts/stage-m171-lvgl.sh "$port"; work="build/external/m168-batch/$port" ;;
    m173) sh scripts/stage-m173-dynamic-client.sh "$port"; work="build/external/m168-batch/$port" ;;
    *) exit 2 ;;
esac
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/$profile.qmp"
log="$logs/$profile-batch.clean.log"
serial="$logs/$profile-batch.serial.log"
oracle="$work/$profile-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 "scripts/$profile-qmp-verify.py" --serial "$serial" \
    --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" --timeout 700 > "$oracle" 2>&1 &
watcher=$!
case "$profile" in
    m166)
        fail=' physical keyboard/tablet/mouse -> evdev'; qmp_fail=' actual keyboard and mouse or framebuffer'; serial_tail=240; oracle_tail=100; qmp_tail=120
        framebuffer='^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$'; marker_message='expected exactly one'
        shots='initial key abs click rel'; executables='fbgrab magick'
        success="[M166 NATIVE INPUT/DISPLAY PASS] $port QEMU VirtIO keyboard+tablet+mouse, signed evtest x2, signed fbgrab/magick, 5 genuine QMP framebuffer screenshots"
        qmp_marker='[M166 INPUT->QMP PASS]'
        ;;
    m167)
        fail=' userspace compositor / app IPC'; qmp_fail=' hardware input or screen oracle'; serial_tail=240; oracle_tail=100; qmp_tail=120
        framebuffer='^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$'; marker_message='expected exactly once'
        shots='initial image-focus image-drag terminal-focus terminal-key'; executables='magick dialog'
        success="[M167 NATIVE DESKTOP PASS] $port QEMU VirtIO input, separate image+terminal processes, Unix IPC, five physical framebuffer screenshots"
        qmp_marker='[M167 QMP DESKTOP PASS]'
        ;;
    m168)
        fail=''; qmp_fail=''; serial_tail=240; oracle_tail=90; qmp_tail=110
        framebuffer='^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$'; marker_message='expected exactly once'
        shots='initial idle image-focus image-drag browser-focus browser-key terminal-focus terminal-key'; executables='magick dialog lynx mc'
        success="[M168 NATIVE DESKTOP PASS] $port four native ELFs, eight true QEMU screen captures including >30s idle, signed Lynx/MC, PTY terminal glyphs"
        qmp_marker='[M168 QMP THREE CLIENTS PASS]'
        ;;
    m170)
        fail=''; qmp_fail=''; serial_tail=240; oracle_tail=100; qmp_tail=100
        framebuffer='^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$'; marker_message='expected exactly once'
        shots='initial browser-closed terminal-first terminal-second terminal-closed'; executables='magick lynx mc'
        success="[M170 NATIVE PASS] $port persistent real PTY commands, five hardware screenshots, independent window closes"
        qmp_marker='[M170 QMP PERSISTENT SHELL PASS]'
        ;;
    m171)
        fail=''; qmp_fail=''; serial_tail=200; oracle_tail=110; qmp_tail=100
        framebuffer='^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$'; marker_message='expected exactly once'
        shots='initial clicked closed'; executables='magick lynx mc'
        success="[M171 NATIVE PASS] $port real pinned LVGL + three QMP screenshots + physical button and client lifecycle"
        qmp_marker='[M171 QMP LVGL PASS]'
        ;;
    m173)
        fail=''; qmp_fail=''; serial_tail=220; oracle_tail=120; qmp_tail=130
        framebuffer='^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$'; marker_message='expected once'
        shots='four five clicked closed shell-after'; executables='magick lynx mc'
        success="[M173 NATIVE PASS] $port real PTY BusyBox launches second upstream LVGL; five independent QMP frames, click, close, surviving shell"
        qmp_marker='[M173 QMP DYNAMIC PASS]'
        ;;
esac
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[$label NATIVE FAIL] $port$fail" >&2
    test ! -s "$log" || tail -n "$serial_tail" "$log" >&2
    test ! -s "$oracle" || tail -n "$oracle_tail" "$oracle" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[$label QMP FAIL] $port$qmp_fail" >&2
    tail -n "$qmp_tail" "$oracle" >&2
    exit 1
fi
grep -Fq "$qmp_marker" "$oracle"
if [ "$profile" = m168 ]; then grep -Fq '[M169 IDLE WAKE PASS]' "$oracle"; fi
grep -aE "$framebuffer" "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[$label MARKER FAIL] $port $marker_message: $marker" >&2
        exit 1
    }
done < "scripts/$profile-expected-markers.txt"
if [ "$profile" = m166 ]; then
    test "$(grep -Fc '[EXEC] replaced process with CPIO ELF: /usr/bin/evtest' "$log" || :)" -eq 2
fi
for executable in $executables; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: /usr/bin/$executable" "$log"
done
if [ "$profile" = m173 ]; then
    grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/gui' "$log" || {
        echo "[M173 EXEC FAIL] $port BusyBox did not launch fifth GUI ELF" >&2
        exit 1
    }
fi
for shot in $shots; do test -s "$work/$profile-$shot.ppm"; done
if [ "$profile" = m171 ]; then
    grep -Fq '[M171 HOST LVGL PASS]' build/external/m171-lvgl/amd64/host-lvgl.log 2>/dev/null || test "$port" != amd64
fi
echo "$success"

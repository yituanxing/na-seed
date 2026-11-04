#!/bin/sh
# Additional native QMP mode on EXACTLY the already-staged M174 signed CPIO.
# No new installation profile, separate software pool, or second rootfs.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
export NAS_QEMU_INPUT_OPTIN=1 NAS_ENABLE_VIRTIO_INPUT=1
if [ "${NAS_QEMU_VIRTIO_GPU:-0}" = 1 ]; then
    export NAS_QEMU_RAMFB=0 NAS_ENABLE_VIRTIO_GPU=1
else
    export NAS_QEMU_RAMFB=1
fi
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=850
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m187-vt100-batch-cases.tsv
work="build/external/m174-software-batch/$port"
test -s "$work/rootfs.cpio" || {
    echo '[M187 VT FAIL] stage the ONE M174 signed CPIO before QMP' >&2
    exit 1
}
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m187-vt100.qmp"
log="$logs/m187-vt100.clean.log"
serial="$logs/m187-vt100.serial.log"
oracle="$work/m187-vt100-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 scripts/m187-qmp-verify.py --serial "$serial" \
    --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" --timeout 850 --m188 --m189 \
    > "$oracle" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M187 VT NATIVE FAIL] $port" >&2
    test ! -s "$log" || tail -180 "$log" >&2
    test ! -s "$oracle" || tail -120 "$oracle" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M187 VT QMP FAIL] $port" >&2
    tail -130 "$oracle" >&2
    exit 1
fi
grep -Fq '[M187 QMP VT100 PASS]' "$oracle"
grep -Fq '[M188 QMP REAL INPUT PASS]' "$oracle"
grep -Fq '[M189 QMP MULTI EDITOR PASS]' "$oracle"
for marker in M174_DEVFS_PSEUDO_PASS M174_DEVFS_HARDWARE_PTY_PASS \
              M174_DEVFS_SOFTWARE_INTEGRATION_PASS \
              M187_PTY_SHELL_RESUMED_AFTER_TUI M187_DESKTOP_PASS \
              M188_LVGL_TIMER_PASS M188_LVGL_SLIDER_DRAG_PASS \
              M188_LVGL_SCROLL_PASS M188_VIM_SAVED_FUNCTION_PASS \
              M189_NANO_SAVED_FUNCTION_PASS \
              M189_PTY_SHELL_RESUMED_AFTER_EDITORS; do
    grep -Fxq "$marker" "$log" || {
        echo "[M187 VT MARKER FAIL] $port $marker" >&2;exit 1
    }
done
grep -Fq 'naseed.argv=/usr/bin/naseed-m174-devfs-launch m187' "$log"
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/dialog' "$log"
for frame in initial focused dialog arrow shell-resumed; do
    test -s "$work/m187-$frame.ppm"
done
for frame in lvgl-time-initial lvgl-time-progress slider-held \
             slider-dragged scroll-before scroll-after before-vim \
             vim-visible vim-inserted vim-shell-verified; do
    test -s "$work/m188-$frame.ppm"
done
for frame in before-nano nano-visible after-byte-check before-vim-reopen \
             vim-reopened session-finished; do
    test -s "$work/m189-$frame.ppm"
done
echo "[M189 MULTI EDITOR NATIVE PASS] $port original signed Vim/Nano/Vim, exact file bytes, distinct PID/wait4 and restored shell"
echo "[M187 VT NATIVE PASS] $port same M174 signed guest: original dialog TUI, physical keyboard/VT/QMP and resumed shell"
echo "[M188 REAL INPUT NATIVE PASS] $port same M174 signed guest: real LVGL monotonic timer, captured slider drag, wheel scroll, original Vim edit/save, shell readback"

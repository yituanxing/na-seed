#!/bin/sh
# One real QEMU guest per ISA: devfs+FileOps+PTY, twenty signed Alpine ELF
# entries, two independent program semantics, and five physical QMP frames.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh
export NAS_QEMU_INPUT_OPTIN=1 NAS_ENABLE_VIRTIO_INPUT=1
if [ "${NAS_QEMU_VIRTIO_GPU:-0}" = 1 ]; then
    # M177 verifies the actual 2D scanout rather than firmware ramfb. The
    # same original desktop and QMP five-frame oracle remain authoritative.
    export NAS_QEMU_RAMFB=0 NAS_ENABLE_VIRTIO_GPU=1
else
    export NAS_QEMU_RAMFB=1
fi
export NAS_ACCEPTANCE_QEMU_RAM=2048M NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_APP_BATCH_QEMU_TIMEOUT:-850}
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m174-batch-cases.tsv
sh scripts/stage-m174-devfs-software-gui.sh "$port"
work="build/external/m174-software-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_QEMU_QMP_SOCKET="$work/m174.qmp"
log="$logs/m174-batch.clean.log"
serial="$logs/m174-batch.serial.log"
oracle="$work/m174-qmp-check.log"
mkdir -p "$logs"
truncate -s 0 "$serial"
python3 "${NAS_QMP_VERIFY_SCRIPT:-scripts/m173-qmp-verify.py}" --serial "$serial" \
    --qmp "$NAS_QEMU_QMP_SOCKET" --outdir "$work" --timeout 850 \
    > "$oracle" 2>&1 &
watcher=$!
if ! sh "$runner"; then
    kill "$watcher" 2>/dev/null || :
    wait "$watcher" 2>/dev/null || :
    echo "[M174 NATIVE FAIL] $port" >&2
    test ! -s "$log" || tail -220 "$log" >&2
    test ! -s "$oracle" || tail -120 "$oracle" >&2
    exit 1
fi
if ! wait "$watcher"; then
    echo "[M174 QMP FAIL] $port" >&2
    tail -130 "$oracle" >&2
    exit 1
fi
grep -Fq '[M173 QMP DYNAMIC PASS]' "$oracle" || {
    echo "[M174 QMP ORACLE FAIL] $port missing strict physical-frame verdict" >&2
    exit 1
}
if [ "${NAS_QEMU_VIRTIO_GPU:-0}" = 1 ]; then
    grep -aEq '^\[VIRTIO-GPU\] real modern (PCI|MMIO) devices=1$' "$log" || {
        echo "[M177 GPU FAIL] $port real PCI/MMIO GPU did not bind" >&2
        exit 1
    }
else
    grep -aE '^\[FB\] firmware framebuffer ready 800x600 pitch=3200 bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null || {
        echo "[M174 FRAMEBUFFER FAIL] $port unexpected firmware framebuffer geometry" >&2
        exit 1
    }
fi
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M174 MARKER FAIL] $port expected exactly once: $marker" >&2
        exit 1
    }
done < scripts/m174-expected-markers.txt
# An entrypoint check cannot be satisfied by BusyBox printing a diagnostic:
# require a real kernel exec into EACH distinct original program ELF.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M174 ELF FAIL] $port original guest entry absent: /$elf" >&2
        exit 1
    }
done < scripts/m174-software-original-elfs.txt
# M187 is an independent additive signed-ELF cohort in the SAME CPIO/guest.
# Each name must have passed native ELF+SHA host checks, and kernel execve
# must now prove a real replacement with that distinct ELF. This is loader
# ENTRY acceptance only, not unsupported device/namespace semantics.
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M187 ELF FAIL] $port new signed guest entry absent: /$elf" >&2
        exit 1
    }
done < scripts/m187-original-elfs.txt
for original in /usr/bin/magick /usr/bin/lynx /usr/bin/mc; do
    grep -Fq "[EXEC] replaced process with CPIO ELF: $original" "$log" || {
        echo "[M174 DESKTOP ELF FAIL] $port original program did not exec: $original" >&2
        exit 1
    }
done
# The launcher is PID1 selected directly by the bootstrap ELF loader, not a
# process created through execve(). The kernel logs its initial ELF selection
# and command line; the devfs and desktop markers prove it ran to completion.
grep -Fq '[M5] CPIO executable selected: /usr/bin/naseed-m174-devfs-launch' "$log" || {
    echo "[M174 PID1 FAIL] $port bootstrap did not select the mounted-devfs launcher" >&2
    exit 1
}
grep -Fq 'naseed.argv=/usr/bin/naseed-m174-devfs-launch m174' "$log" || {
    echo "[M174 PID1 FAIL] $port launcher command line not observed" >&2
    exit 1
}
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/gui' "$log" || {
    echo "[M174 LVGL FAIL] $port external GUI alias did not exec" >&2
    exit 1
}
for frame in four five clicked closed shell-after; do
    test -s "$work/m173-$frame.ppm" || {
        echo "[M174 FRAME FAIL] $port missing QMP screenshot: $frame" >&2
        exit 1
    }
done
echo "[M174 NATIVE PASS] $port true /dev VFS + char OpenFile + 20 original signed Alpine ELF entries + real dbus/proc semantics + 5 QMP physical frames"
echo "[M187 SIGNED SOFTWARE PASS] $port +26 independently native Alpine ELF entries; sar/sadf roundtrip + inotify create/modify guest functions"

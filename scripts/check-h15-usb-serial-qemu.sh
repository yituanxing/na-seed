#!/bin/sh
# H15 true QEMU xHCI FT232BM USB serial -> original Alpine BusyBox
# /dev/ttyUSB0 read/write, independently verified random host socket nonce.
set -eu
out=build/h15-qemu-only
image="$out/naseed.iso"
serial_log="$out/h15-qemu-uefi-serial.log"
clean="$out/h15-qemu-uefi-clean.log"
oracle_log="$out/h15-ftdi-oracle.log"
ftdi_sock="$PWD/$out/h15-ftdi.sock"
for path in "$image" "$out/h15-alpine.ext4" \
            "$out/h15-media-before.sha256" \
            "$out/h15-original-alpine-elfs.sha256"; do
    test -s "$path" && [ ! -L "$path" ] || {
        echo "[H15 QEMU FAIL] missing/symlinked original media $path" >&2
        exit 2
    }
done
[ ! -e "$ftdi_sock" ] && [ ! -L "$ftdi_sock" ] &&
[ ! -L "$serial_log" ] && [ ! -L "$oracle_log" ] &&
[ -z "${NAS_VIRTIO_BLK_IMAGE:-}" ] &&
[ -z "${NAS_QEMU_ASYNC_BLK_IMAGE:-}" ] &&
[ "${NAS_QEMU_INPUT_OPTIN:-0}" = 0 ] || {
    echo '[H15 QEMU FAIL] stale socket/log or ambient VirtIO fallback' >&2
    exit 2
}
(cd "$out" && sha256sum -c h15-media-before.sha256)
sha256sum -c "$out/h15-original-alpine-elfs.sha256"
qemu_pid=''
oracle_pid=''
watchdog_pid=''
trap 'for child in "$watchdog_pid" "$oracle_pid" "$qemu_pid"; do if [ -n "$child" ]; then kill -TERM "$child" 2>/dev/null || :; fi; done' EXIT
python3 scripts/h15-ftdi-socket-oracle.py \
    --socket "$ftdi_sock" --serial "$serial_log" --timeout 280 \
    > "$oracle_log" 2>&1 &
oracle_pid=$!
rc=0
NAS_QEMU_H15_USB_SERIAL_USERLAND=1 NAS_QEMU_H15_FTDI_SOCKET="$ftdi_sock" \
    timeout --signal=TERM --kill-after=3s 350s \
    sh scripts/run.sh "$image" > "$serial_log" 2>&1 &
qemu_pid=$!
(
    while :; do
        if grep -Eq '^\[(H15 FTDI FAIL|H15 USB SERIAL FAIL|FAIL|FAULT|EXEC REJECT|ELF REJECT)' "$serial_log" ||
           grep -Fq '[H15 FTDI HOST RANDOM NONCE FAIL]' "$oracle_log"; then
            kill -TERM "$qemu_pid" 2>/dev/null || :
            exit 0
        fi
        sleep 0.3
    done
) &
watchdog_pid=$!
wait "$qemu_pid" || rc=$?
kill -TERM "$watchdog_pid" 2>/dev/null || :
wait "$watchdog_pid" 2>/dev/null || :
watchdog_pid=''
qemu_pid=''
tr -d '\r' < "$serial_log" > "$clean"
if [ "$rc" -ne 1 ]; then
    kill -TERM "$oracle_pid" 2>/dev/null || :
    wait "$oracle_pid" 2>/dev/null || :
    oracle_pid=''
    echo "[H15 QEMU FAIL] original guest PID1 did not exit 0; QEMU rc 1 expected, rc=$rc" >&2
    tail -150 "$clean" >&2
    tail -80 "$oracle_log" >&2
    exit 1
fi
oracle_rc=0
wait "$oracle_pid" || oracle_rc=$?
oracle_pid=''
[ "$oracle_rc" -eq 0 ] &&
grep -Fq '[H15 FTDI HOST RANDOM NONCE ROUNDTRIP PASS]' "$oracle_log" || {
    echo '[H15 QEMU FAIL] independent host socket nonce oracle rejected physical USB exchange' >&2
    tail -100 "$oracle_log" >&2
    tail -150 "$clean" >&2
    exit 1
}
for required in \
    '[H15 FTDI BAR QUALIFIED]' \
    '[H15 USB SERIAL ATTACHED] /dev/ttyUSB0 FT232BM BUS_USB' \
    '[H15 FTDI BOOT CONTINUE]' \
    '[H8 NVME ALPINE ROOT MOUNTED] / read-only source=nvme0' \
    '[EXEC] replaced process with lwext4 ELF: /bin/busybox' \
    'H15_ALPINE_BUSYBOX_NVME_PASS' \
    'H15_ALPINE_LUA54_NVME_PASS' \
    'H15_ALPINE_JQ_NVME_PASS' \
    'H15_ALPINE_SQLITE_NVME_PASS' \
    'H15_ALPINE_READONLY_NVME_PASS' \
    'H15_ORIGINAL_ALPINE_DYNAMIC_ELFS_NVME_PASS' \
    'H15_FTDI_ORIGINAL_BUSYBOX_STTY_PASS' \
    'H15_USB_SERIAL_READY_FOR_HOST' \
    'H15_ORIGINAL_ALPINE_BUSYBOX_USB_SERIAL_NONCE_RETURNED' \
    '[H15 FTDI QUIESCED RELEASED]' \
    '[H15 FTDI USERLAND SHUTDOWN]' \
    '[EXIT] user status=0' \
    '[PASS] cmdline userspace exit 0'; do
    grep -Fq "$required" "$clean" || {
        echo "[H15 QEMU FAIL] missing real FTDI/original Alpine evidence: $required" >&2
        tail -170 "$clean" >&2
        exit 1
    }
done
if grep -Eq '\[FAIL\]|\[FAULT\]|\[H15 FTDI FAIL|\[H15 USB SERIAL FAIL|H15_FTDI_GUEST_FAIL_' "$clean" ||
   grep -Eq '\[VIRTIO-INPUT\]|\[VIRTIO-BLK\]|\[H9 AHCI|\[H11 USB BLOCK' "$clean"; then
    echo '[H15 QEMU FAIL] fatal or unauthorized fallback' >&2
    exit 1
fi
(cd "$out" && sha256sum -c h15-media-before.sha256)
sha256sum -c "$out/h15-original-alpine-elfs.sha256"
echo '[H15 REAL FTDI USB SERIAL ALPINE BUSYBOX PASS] actual QEMU FT232BM 0403:6001 physical USB bulk serial, original signed Alpine BusyBox stty/head/printf, unpredictable host socket nonce, PID1=0, DMA/BAR released, immutable media SHA'
tail -120 "$clean"
tail -35 "$oracle_log"

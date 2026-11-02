#!/bin/sh
# Native x86_64/RV64/ARM64: verified TLS PNG through VirtIO/lwIP to real
# Linux image executables, writable firmware FB and independent QMP display.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) nic=PCI ;;
    riscv64|arm64) nic=MMIO ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh
export NAS_ENABLE_LWIP=1
export NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1
export NAS_QEMU_RAMFB=1
export NAS_ACCEPTANCE_QEMU_RAM=2048M
export NAS_ACCEPTANCE_DISK_MB=1024
export NAS_APP_BATCH_QEMU_TIMEOUT=700
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export BUSYBOX_PARITY_MANIFEST=scripts/m162-batch-cases.tsv
unset NAS_SYSCALL_TRACE
sh scripts/stage-m162-prebuilt.sh "$port"
work="build/external/m162-batch/$port"
root="$work/rootfs"
export BUSYBOX_PARITY_BINARY="$root/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export NAS_VIRTIO_NET_PCAP="$work/virtio-m162-display.pcap"
export NAS_QEMU_QMP_SOCKET="$work/m162.qmp"
log="$logs/m162-batch.clean.log"
serial="$logs/m162-batch.serial.log"
qmp_log="$work/m162-qmp-check.log"
screen="$work/m162-qmp-screen.ppm"

# Real private key exists only on the host, and original M156 TLS code
# supplies the same independent CA/identity/wire request oracle as M160.
python3 -u scripts/m160-image-wire-server.py \
    "$work/host-tls.crt" "$work/host-tls.key" \
    "$work/host-m162-display.png" > "$work/host-m162-wire.log" 2>&1 &
host_pid=$!
trap 'kill "$host_pid" 2>/dev/null || :; wait "$host_pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5 6 7 8; do
    if grep -Fq '[M160 IMAGE HOST READY]' "$work/host-m162-wire.log"; then
        ready=true
        break
    fi
    sleep 1
done
[ "$ready" = true ] || {
    echo '[M162 HOST FAIL] authentic TLS image server did not start' >&2
    tail -90 "$work/host-m162-wire.log" >&2
    exit 1
}

python3 scripts/m161-qmp-verify.py --socket "$NAS_QEMU_QMP_SOCKET" \
    --serial "$serial" --screenshot "$screen" --timeout 700 \
    --trigger M162_FRAME_VISIBLE --label M162 > "$qmp_log" 2>&1 &
qmp_pid=$!
if ! sh "$runner"; then
    kill "$qmp_pid" 2>/dev/null || :
    wait "$qmp_pid" 2>/dev/null || :
    echo "[M162 QEMU FAIL] $port network image to real firmware framebuffer" >&2
    test ! -s "$log" || tail -220 "$log" >&2
    test ! -s "$qmp_log" || tail -50 "$qmp_log" >&2
    exit 1
fi
if ! wait "$qmp_pid"; then
    echo "[M162 DISPLAY FAIL] $port independent QEMU screenshot" >&2
    tail -90 "$qmp_log" >&2
    exit 1
fi

grep -Fq "[VIRTIO-NET] real modern $nic devices=1" "$log"
grep -Fq '[LWIP DHCP BOUND] addr=10.0.2.' "$log"
grep -aE '^\[FB\] firmware framebuffer ready [0-9]+x[0-9]+ pitch=[0-9]+ bpp=32 r8@16 g8@8 b8@0$' "$log" >/dev/null
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    [ "$(grep -Fxc "$marker" "$log" || :)" -eq 1 ] || {
        echo "[M162 GUEST FAIL] missing or duplicate $marker" >&2; exit 1;
    }
done < scripts/m162-expected-markers.txt
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    grep -Fq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
        echo "[M162 EXEC FAIL] $port /$elf" >&2; exit 1;
    }
done < scripts/m162-reused-elfs.txt
grep -Fq '[M162 QMP DISPLAY PASS]' "$qmp_log"
payload_bytes=$(wc -c < "$work/host-m162-display.png" | tr -d ' ')
for path in m162-display.png m162-display-wget.png; do
    [ "$(grep -Ec "^\\[M156 HTTPS HOST GET\\] /$path bytes=$payload_bytes tls=TLSv1\\.[23]$" "$work/host-m162-wire.log" || :)" -eq 1 ] || {
        echo "[M162 TLS FAIL] incorrect verified GET count for $path" >&2
        exit 1
    }
done
[ "$(grep -Fc '[M156 HTTPS HOST GET]' "$work/host-m162-wire.log" || :)" -eq 2 ]
echo "[M162 NATIVE DISPLAY/TLS PASS] $port verified HTTPS PNG -> 7 signed Alpine programs -> actual $nic framebuffer pixels"

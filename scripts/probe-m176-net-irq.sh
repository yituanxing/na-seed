#!/bin/sh
# M176: ONE QEMU guest per ISA reuses the whole M175 input+GUI and M174
# 20-original-ELF workload, adds two real NIC IRQ consumers and an original
# Alpine Ncat HTTP round-trip through QEMU slirp with packet-capture oracle.
# Success requires guest ISR/worker counters AND independent host wire bytes.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
. scripts/native-qemu-target.sh

work="build/external/m174-software-batch/$port"
mkdir -p "$work"
server_log="$work/m176-host-http.log"
pcap="$work/m176-wire.pcap"
rm -f "$server_log" "$pcap"
python3 scripts/m176-http-server.py 15416 > "$server_log" 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || :; wait "$server_pid" 2>/dev/null || :' EXIT HUP INT TERM
host_ready=0
for attempt in 1 2 3 4 5 6 7 8 9 10; do
    if curl --noproxy '*' -fsS --max-time 2 \
        http://127.0.0.1:15416/healthz -o /dev/null; then
        host_ready=1
        break
    fi
    sleep 1
done
[ "$host_ready" -eq 1 ] || {
    echo "[M176 NET RX IRQ FAIL] $port host HTTP oracle unavailable" >&2
    exit 1
}

# M174 builds and boots exactly once. M175 adds 12+12 physical input bursts,
# same five QMP frames. M176 only adds two real NET devices, signed original
# Ncat + its dependency overlay and one extra HTTP transaction before GUI.
export NAS_M176_NET_STAGE=1
export NAS_ENABLE_LWIP=1 NAS_ENABLE_VIRTIO_NET=1
export NAS_VIRTIO_NET_USER=1 NAS_VIRTIO_NET_SECOND_USER=1
export NAS_VIRTIO_NET_PCAP="$pcap"
NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_APP_BATCH_QEMU_TIMEOUT:-420}
export NAS_APP_BATCH_QEMU_TIMEOUT
sh scripts/probe-m175-irq.sh "$port"

log="$logs/m174-batch.clean.log"
test -s "$log" && test -s "$pcap" && test -s "$server_log" || {
    echo "[M176 NET RX IRQ FAIL] $port missing guest, PCAP or host HTTP log" >&2
    exit 1
}
case "$port" in
    amd64) transport=PCI ;;
    *) transport=MMIO ;;
esac
grep -Fq "[VIRTIO-NET] real modern $transport devices=2" "$log" || {
    echo "[M176 NET RX IRQ FAIL] $port did not enumerate two physical NICs" >&2
    exit 1
}
for device in eth0 eth1; do
    grep -Fq "[LWIP] native $device attached; DHCP started" "$log" || {
        echo "[M176 NET RX IRQ FAIL] $port $device missing attached netif" >&2
        exit 1
    }
done
for subnet in 10.0.2. 10.0.3.; do
    grep -Fq "[LWIP DHCP BOUND] addr=$subnet" "$log" || {
        echo "[M176 NET RX IRQ FAIL] $port no independent $subnet DHCP lease" >&2
        exit 1
    }
done

# Guest executed the original, APK-signed ELF, sent exact request and parsed
# response. Host independently observed the live HTTP GET, QEMU independently
# captured both wire directions. A serial marker alone can never pass.
grep -Fq '[EXEC] replaced process with CPIO ELF: /usr/bin/ncat' "$log" || {
    echo "[M176 NET RX IRQ FAIL] $port original Ncat ELF not execed" >&2
    exit 1
}
grep -Eq '^M176_ORIGINAL_ALPINE_NCAT_HTTP_PASS bytes=[1-9][0-9]*$' "$log" || {
    echo "[M176 NET RX IRQ FAIL] $port original Ncat did not parse host reply" >&2
    exit 1
}
[ "$(grep -Fc '[M176 HOST REAL NCAT REQUEST] /m176-ncat-irq' "$server_log" || :)" -eq 1 ] || {
    echo "[M176 NET RX IRQ FAIL] $port host saw no unique authentic Ncat request" >&2
    exit 1
}
for needle in 'GET /m176-ncat-irq HTTP/1.0' 'M176_REAL_NCAT_HTTP_RESPONSE'; do
    grep -aFq "$needle" "$pcap" || {
        echo "[M176 NET RX IRQ FAIL] $port PCAP missing real TCP payload: $needle" >&2
        exit 1
    }
done

python3 scripts/check-m176-net-irq-stats.py "$log" || {
    echo "[M176 NET RX IRQ FAIL] $port invalid actual IRQ/worker/RX/fallback statistics" >&2
    grep -a 'M176 NET RX IRQ' "$log" >&2 || :
    exit 1
}
echo "[M176 NET RX IRQ NATIVE PASS] $port 2 genuine routed NICs, dual DHCP, signed Alpine Ncat HTTP request+reply in QEMU PCAP, RX IRQ ACK/deferred>0 fallback=0; preserved M175 GUI and 20 original ELFs"

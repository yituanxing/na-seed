#!/bin/sh
# Two unchanged signed Alpine ELFs in ONE guest boot: Nmap TCP connect scan,
# then iperf3's real TCP control/data protocol to a host-only iperf3 server.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
LANG=C
export PATH LC_ALL LANG
busy() { /bin/busybox "$@"; }
work=/tmp/m141-iperf3-wire
busy mkdir -p "$work"
HOME="$work"
export HOME

for exe in /usr/bin/nmap /usr/bin/iperf3; do
    [ -x "$exe" ] || exit 1
    resolved=$(busy readlink -f "$exe") || exit 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) exit 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || exit 1
done

# -sT means ordinary TCP connect(), not raw-packet capture or synthetic
# scan output. --unprivileged avoids requiring unavailable raw-socket APIs.
scanned=false
for attempt in 1 2 3 4; do
    busy timeout 12 /usr/bin/nmap -4 -n --unprivileged -sT -Pn \
      -p 15416 --open 10.0.2.2 > "$work/scan" 2> "$work/scan.err" || :
    if busy grep -Eq '15416/tcp[[:space:]]+open' "$work/scan"; then
        scanned=true
        break
    fi
    busy sleep 1
done
if [ "$scanned" != true ]; then
    busy printf '[M141 IPERF WIRE DIAG] original Nmap scan failed\n'
    busy head -n 18 "$work/scan" || :
    busy head -n 18 "$work/scan.err" || :
    exit 1
fi
busy printf 'M141_SIGNED_NMAP_WIRE_PASS\n'

# iperf3 opens a control TCP connection and an independent data TCP stream.
# A one-second, 512-byte-block transfer is enough to prove nonzero genuine
# host-side bytes while keeping the first OS network benchmark bounded.
transferred=false
for attempt in 1 2 3; do
    busy timeout 25 /usr/bin/iperf3 -4 -c 10.0.2.2 -p 15416 \
      -t 1 -l 512 -P 1 -J > "$work/client.json" \
      2> "$work/iperf.err" || :
    if busy grep -Eq '"bytes"[[:space:]]*:[[:space:]]*[1-9]' \
         "$work/client.json" &&
       busy grep -Eq '"bits_per_second"[[:space:]]*:[[:space:]]*[1-9]' \
         "$work/client.json"; then
        transferred=true
        break
    fi
    busy sleep 1
done
if [ "$transferred" != true ]; then
    busy printf '[M141 IPERF WIRE DIAG] original iperf3 transfer failed\n'
    busy head -n 24 "$work/client.json" || :
    busy head -n 24 "$work/iperf.err" || :
    exit 1
fi
busy printf 'M141_SIGNED_IPERF3_WIRE_PASS\n'

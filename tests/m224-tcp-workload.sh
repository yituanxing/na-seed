#!/bin/sh
# Exact signed Alpine nmap/iperf3/ncat against independent host TCP sockets.
# A command's exit code or a synthetic guest-only listener is not acceptance:
# physical ext4, phase-specific host logs and TWO Ethernet PCAPs are required.
set -eu
[ "$#" -eq 3 ] && [ "$1" = --exercise ] || exit 2
case "${NAS_M224_BOOT_PHASE:-}" in
    first|reopen) phase=$NAS_M224_BOOT_PHASE ;;
    *) echo '[M224 TCP FAIL] missing real native QEMU boot phase' >&2; exit 1 ;;
esac
case_id=$2
elf=$3
busy=/bin/busybox
test -x "/$elf" || { echo '[M224 TCP FAIL] original signed ELF missing' >&2; exit 1; }
magic=$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')
[ "$magic" = 7f454c46 ] || { echo '[M224 TCP FAIL] not a native ELF' >&2; exit 1; }
target="/root/m224-work/$case_id.$phase"
test ! -e "$target" || { echo '[M224 TCP FAIL] stale phase target' >&2; exit 1; }
mkdir -p "$target"

case "$case_id:$elf" in
    real-host-nmap-connect:usr/bin/nmap)
        # -sT is an actual connect(2) scan, requiring neither raw sockets nor
        # a synthetic output. OPEN and CLOSED must be independently observed;
        # bounded retries mirror historical M141 native TCP scan flakiness.
        nmap_done=0
        nmap_attempt=0
        while [ "$nmap_attempt" -lt 3 ]; do
            nmap_attempt=$((nmap_attempt+1))
            if $busy timeout 20 /usr/bin/nmap -4 -sT -Pn -n --unprivileged \
                    --max-retries 1 -p 15441,15442 10.0.2.2 \
                    > "$target/nmap.txt" 2> "$target/nmap.err" &&
               $busy grep -Eq '^15441/tcp[[:space:]]+open[[:space:]]' "$target/nmap.txt" &&
               $busy grep -Eq '^15442/tcp[[:space:]]+closed[[:space:]]' "$target/nmap.txt"; then
                nmap_done=1
                break
            fi
            $busy sleep 1
        done
        [ "$nmap_done" -eq 1 ] || {
            echo '[M224 TCP FAIL] nmap did not discriminate host open/closed TCP ports' >&2
            $busy tail -n 6 "$target/nmap.txt" >&2 || :
            $busy tail -n 4 "$target/nmap.err" >&2 || :
            exit 1
        }
        ;;
    real-host-iperf3-throughput:usr/bin/iperf3)
        # The native original client must SEND real payload to the external
        # host iperf3 -s. Host independently validates received >0 bytes.
        iperf_done=0
        iperf_attempt=0
        while [ "$iperf_attempt" -lt 3 ]; do
            iperf_attempt=$((iperf_attempt+1))
            if $busy timeout 30 /usr/bin/iperf3 -4 -c 10.0.2.2 -p 15444 \
                    -t 1 -l 512 -P 1 -J \
                    > "$target/iperf3.json" 2> "$target/iperf3.err" &&
               $busy grep -q '"sum_sent"' "$target/iperf3.json" &&
               $busy grep -Eq '"bytes"[[:space:]]*:[[:space:]]*[1-9][0-9]*' \
                    "$target/iperf3.json"; then
                iperf_done=1
                break
            fi
            $busy sleep 1
        done
        [ "$iperf_done" -eq 1 ] || {
                echo '[M224 TCP FAIL] no nonzero iperf3 TCP transfer report' >&2
            $busy tail -n 4 "$target/iperf3.err" >&2 || :
                exit 1
            }
        ;;
    real-host-ncat-random:usr/bin/ncat)
        # The host verifies this boot-specific token before returning a NEW
        # host-only random binary witness; neither phase's bytes are seeded.
        if ! printf 'M224_NCAT_CLIENT_%s\n' "$phase" | \
                $busy timeout 20 /usr/bin/ncat -4 -w 10 10.0.2.2 15443 \
                > "$target/host-random.bin" 2> "$target/ncat.err"; then
            echo '[M224 TCP FAIL] original ncat duplex transaction failed' >&2
            $busy tail -n 4 "$target/ncat.err" >&2 || :
            exit 1
        fi
        bytes=$($busy wc -c < "$target/host-random.bin" | $busy tr -d ' \t\r\n')
        [ "$bytes" -eq 96 ] || {
            echo "[M224 TCP FAIL] expected 96 host-only random bytes, received $bytes" >&2
            exit 1
        }
        ;;
    *) echo "[M224 TCP FAIL] unrecognized original ELF or case: $case_id:$elf" >&2; exit 2 ;;
esac

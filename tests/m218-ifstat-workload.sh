#!/bin/sh
# M218: original signed Alpine iproute2 ifstat, real eth0 counter delta.
# --verify-samples is a host-fixture parser check, NEVER native guest proof.
set -eu

read_proc_eth0() {
    test -s "$1" || return 1
    awk '
        $1 == "eth0:" {
            seen++
            if (NF != 17) { bad = 1; next }
            for (i = 2; i <= 17; i++)
                if ($i !~ /^[0-9]+$/) bad = 1
            if (!bad) print $3, $11, $2, $10
        }
        END { if (seen != 1 || bad) exit 1 }
    ' "$1"
}

json_number() {
    file=$1 key=$2
    count=$(awk -v token="\"$key\"" '
        { n += gsub(token, "") } END { print n + 0 }
    ' "$file") || return 1
    test "$count" -eq 1 || return 1
    tr -d '\r\n' < "$file" |
        sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\\([0-9][0-9]*\\)[[:space:]]*[,}].*/\\1/p"
}

read_ifstat_eth0() {
    test -s "$1" || return 1
    # -a -s -z -j eth0 is an unscaled, read-only RTM_GETLINK snapshot.
    # Upstream iproute2 misc/ifstat.c load_info() explicitly assigns
    # info_source="kernel" before JSON output when no daemon is connected.
    # Reject empty/daemon/history provenance or a missing interface.
    test "$(awk '{ n += gsub(/"kernel"/, "") } END { print n + 0 }' "$1")" -eq 1 || return 1
    test "$(awk '{ n += gsub(/"eth0"/, "") } END { print n + 0 }' "$1")" -eq 1 || return 1
    rxp=$(json_number "$1" rx_packets) || return 1
    txp=$(json_number "$1" tx_packets) || return 1
    rxb=$(json_number "$1" rx_bytes) || return 1
    txb=$(json_number "$1" tx_bytes) || return 1
    for value in "$rxp" "$txp" "$rxb" "$txb"; do
        case "$value" in ''|*[!0-9]*) return 1 ;; esac
    done
    printf '%s %s %s %s\n' "$rxp" "$txp" "$rxb" "$txb"
}

verify_samples() {
    test "$#" -eq 5 || return 1
    proc_before=$1 if_before=$2 traffic=$3 if_after=$4 proc_after=$5
    # The same vetted fixture sends 40 real UDP/DNS datagrams on eth0.
    test -s "$traffic" &&
        grep -Fxq 'M205_REAL_UDP_SENT=40 dst=10.0.2.3:53' "$traffic" || return 1

    before_proc=$(read_proc_eth0 "$proc_before") || return 1
    before_if=$(read_ifstat_eth0 "$if_before") || return 1
    after_if=$(read_ifstat_eth0 "$if_after") || return 1
    after_proc=$(read_proc_eth0 "$proc_after") || return 1

    # Measurement order is proc-before < ifstat-before < 40 real sends <
    # ifstat-after < proc-after. Every corresponding byte/packet counter
    # must be bracketed by /proc/net/dev, with genuine ifstat TX growth.
    awk -v p0="$before_proc" -v i0="$before_if" \
        -v i1="$after_if" -v p1="$after_proc" '
        BEGIN {
            split(p0, a, " "); split(i0, b, " ")
            split(i1, c, " "); split(p1, d, " ")
            for (k = 1; k <= 4; k++) {
                if (a[k] !~ /^[0-9]+$/ || b[k] !~ /^[0-9]+$/ ||
                    c[k] !~ /^[0-9]+$/ || d[k] !~ /^[0-9]+$/ ||
                    a[k] + 0 > b[k] + 0 ||
                    b[k] + 0 > c[k] + 0 ||
                    c[k] + 0 > d[k] + 0) exit 1
            }
            # field 2 = TX packets, field 4 = TX bytes; the 40 sends must
            # be visible in *both* independent kernel read interfaces.
            if (c[2] - b[2] < 40 || d[2] - a[2] < 40 ||
                c[4] <= b[4] || d[4] <= a[4]) exit 1
            exit 0
        }
    '
}

if test "${1:-}" = --verify-samples; then
    test "$#" -eq 6 || exit 2
    if verify_samples "$2" "$3" "$4" "$5" "$6"; then
        echo '[M218 SAMPLE CONTRACT PASS] bounded positive eth0 delta and 40 real UDP sends'
        exit 0
    fi
    echo '[M218 SAMPLE CONTRACT FAIL] absent, stale, fake or inconsistent counters' >&2
    exit 1
fi

test "${1:-}" = --exercise && test "$#" -eq 3 &&
    test "$2" = live-interface-counter-delta && test "$3" = sbin/ifstat || {
        echo '[M218 ERROR] usage: --exercise live-interface-counter-delta sbin/ifstat' >&2
        exit 2
    }

export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C
test -x /sbin/ifstat && test -x /root/m205-fixtures/udp-traffic || {
    echo '[M218 FAIL] signed original ifstat or genuine UDP fixture missing' >&2
    exit 1
}
magic=$(/bin/busybox od -An -tx1 -N4 /sbin/ifstat 2>/dev/null |
    /bin/busybox tr -d ' \t\r\n')
test "$magic" = 7f454c46 || {
    echo '[M218 FAIL] original ifstat is not an ELF' >&2
    exit 1
}
target="/root/m218-work/ifstat.${NAS_M218_BOOT_PHASE:-manual}"
mkdir -p "$target" || exit 1

# On the first genuine Alpine PID1 boot, M197 UART already waited for DHCP.
# The independently reopened QEMU instead runs a compact BusyBox CPIO
# command immediately; on RV64/AArch64 it raced the actual lwIP DHCP ACK.
# A physical NIC alone is not proof that 10.0.2.3 is routed. Wait for the
# SAME DHCP-owned /proc signal as the original first-boot UART, never invent
# an address, install a synthetic route or relax the 40-packet oracle.
waited=0
until /bin/busybox grep -q '^nameserver 10\.0\.2\.3' \
        /proc/net/naseed-resolv.conf 2>/dev/null; do
    if [ "$waited" -ge 90 ]; then
        echo '[M218 DHCP ROUTE TIMEOUT] no genuine lease-backed 10.0.2.3 DNS route after 90s' >&2
        exit 1
    fi
    waited=$((waited+1))
    /bin/busybox sleep 1 || exit 1
done
echo "[M218 GENUINE DHCP ROUTE READY] waited=${waited}s"

cat /proc/net/dev > "$target/proc-before" || exit 1
/bin/busybox timeout 15 /sbin/ifstat -a -s -z -j eth0 \
    > "$target/ifstat-before" 2> "$target/ifstat-before.err" || {
        echo '[M218 FAIL] original ifstat baseline RTM_GETLINK snapshot' >&2
        /bin/busybox tail -n 3 "$target/ifstat-before.err" >&2
        exit 1
    }
/bin/busybox timeout 30 /root/m205-fixtures/udp-traffic \
    > "$target/traffic" 2> "$target/traffic.err" || {
        echo '[M218 FAIL] 40 real guest UDP/DNS packets' >&2
        /bin/busybox tail -n 3 "$target/traffic.err" >&2
        exit 1
    }
/bin/busybox timeout 15 /sbin/ifstat -a -s -z -j eth0 \
    > "$target/ifstat-after" 2> "$target/ifstat-after.err" || {
        echo '[M218 FAIL] original ifstat post-traffic RTM_GETLINK snapshot' >&2
        /bin/busybox tail -n 3 "$target/ifstat-after.err" >&2
        exit 1
    }
cat /proc/net/dev > "$target/proc-after" || exit 1

if verify_samples "$target/proc-before" "$target/ifstat-before" \
        "$target/traffic" "$target/ifstat-after" "$target/proc-after"; then
    echo '[M218 genuine guest semantic PASS] signed original ifstat sees 40 real eth0 UDP TX packets and monotone byte counters in RTM_GETLINK + /proc/net/dev'
    exit 0
fi
echo '[M218 genuine guest semantic FAIL] original ifstat counters differ from real eth0 device accounting' >&2
exit 1

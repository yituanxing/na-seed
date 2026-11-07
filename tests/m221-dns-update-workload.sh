#!/bin/sh
# ORIGINAL signed Alpine bind-tools: two independent nsupdate and host ELFs.
# Guest CLI status is only one gate; the separate host RFC2136 authoritative
# state and each QEMU's exact Ethernet PCAP decide genuine wire acceptance.
set -eu
test "${1:-}" = --exercise && test "$#" -eq 3 || exit 2
case "${NAS_M221_BOOT_PHASE:-}" in
    first) owner=m221-first.naseed.test.; address=192.0.2.221 ;;
    reopen) owner=m221-reopen.naseed.test.; address=192.0.2.222 ;;
    *) echo '[M221 ERROR] missing genuine QEMU boot phase' >&2; exit 2 ;;
esac
phase=$NAS_M221_BOOT_PHASE
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C

# M197 first OpenRC has DHCP before application tests. A separate CPIO
# reopening may reach an ELF before the real DHCP ACK/default route exists.
waited=0
until /bin/busybox grep -q '^nameserver 10\.0\.2\.3' \
        /proc/net/naseed-resolv.conf 2>/dev/null; do
    if [ "$waited" -ge 90 ]; then
        echo "[M221 DHCP FAIL] phase=$phase no lease-backed route in 90s" >&2
        exit 1
    fi
    waited=$((waited+1))
    /bin/busybox sleep 1
done
echo "[M221 GENUINE DHCP READY] phase=$phase waited=${waited}s"
work="/root/m221-work/$phase"
/bin/busybox mkdir -p "$work"

case "$2:$3" in
    rfc2136-authoritative-update:usr/bin/nsupdate)
        test -x /usr/bin/nsupdate || exit 1
        /bin/busybox printf '%s\n' \
          'server 10.0.2.2 15416' \
          'zone naseed.test.' \
          "update add $owner 60 A $address" \
          'send' > "$work/update.txt"
        # UDP UPDATE must get a real authoritative RFC2136 response.
        if ! /bin/busybox timeout 38 /usr/bin/nsupdate -4 -t 8 -u 3 -r 2 \
                "$work/update.txt" > "$work/nsupdate.out" 2> "$work/nsupdate.err"; then
            echo "[M221 NSUPDATE FAIL] phase=$phase original client cannot update authority" >&2
            /bin/busybox tail -n 4 "$work/nsupdate.err" >&2 || :
            exit 1
        fi
        echo "[M221 ORIGINAL NSUPDATE CLI ONLY] phase=$phase owner=$owner ip=$address; host ACK+PCAP still required"
        ;;
    rfc2136-update-readback:usr/bin/host)
        test -x /usr/bin/host || exit 1
        ok=0
        for attempt in 1 2 3; do
            /bin/busybox timeout 18 /usr/bin/host -4 -t A -p 15416 -R 1 -W 3 \
                "$owner" 10.0.2.2 > "$work/host.out" 2> "$work/host.err" || :
            if /bin/busybox grep -Fq "${owner%.}" "$work/host.out" &&
               /bin/busybox grep -Fq "$address" "$work/host.out"; then
                ok=1
                break
            fi
            /bin/busybox sleep 1
        done
        if [ "$ok" -ne 1 ]; then
            echo "[M221 HOST READBACK FAIL] phase=$phase original host could not read updated A" >&2
            /bin/busybox tail -n 5 "$work/host.out" >&2 || :
            /bin/busybox tail -n 5 "$work/host.err" >&2 || :
            exit 1
        fi
        echo "[M221 ORIGINAL HOST CLI ONLY] phase=$phase owner=$owner ip=$address; authority transaction+PCAP still required"
        ;;
    *) echo '[M221 ERROR] wrong original signed ELF/probe pair' >&2; exit 2 ;;
esac

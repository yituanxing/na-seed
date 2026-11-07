#!/bin/sh
# Original signed Alpine Nping, actual unprivileged UDP sent on real lwIP NIC.
# CLI exit/"SENT" text is only a precondition: host UDP + QEMU PCAP decide PASS.
set -eu
test "${1:-}" = --exercise && test "$#" -eq 3 &&
    test "$2" = original-udp-wire-delivery && test "$3" = usr/bin/nping || {
        echo '[M220 ERROR] expected --exercise original-udp-wire-delivery usr/bin/nping' >&2
        exit 2
    }
export PATH=/usr/sbin:/sbin:/usr/bin:/bin HOME=/root LC_ALL=C LANG=C
test -x /usr/bin/nping || {
    echo '[M220 FAIL] original signed /usr/bin/nping is absent' >&2
    exit 1
}
magic=$(/bin/busybox od -An -tx1 -N4 /usr/bin/nping 2>/dev/null |
    /bin/busybox tr -d ' \t\r\n')
test "$magic" = 7f454c46 || {
    echo '[M220 FAIL] installed Nping is not original ELF' >&2
    exit 1
}
apk info -e nmap-nping >/dev/null 2>&1 &&
    apk info -W /usr/bin/nping 2>/dev/null | /bin/busybox grep -Fq nmap-nping || {
        echo '[M220 FAIL] original signed Nping APK ownership absent' >&2
        exit 1
    }
case "${NAS_M220_BOOT_PHASE:-}" in
    first) payload=NASEED_NPING_FIRST ;;
    reopen) payload=NASEED_NPING_AGAIN ;;
    *) echo '[M220 FAIL] no genuine first/reopen phase' >&2; exit 1 ;;
esac
test "${#payload}" -eq 18 || exit 1

# Real DHCP ACK establishes the 10.0.2.x interface and default gateway.
# Independent second CPIO QEMU otherwise races route installation on RV/ARM.
waited=0
until /bin/busybox grep -q '^nameserver 10\.0\.2\.3' \
        /proc/net/naseed-resolv.conf 2>/dev/null; do
    if [ "$waited" -ge 90 ]; then
        echo '[M220 DHCP TIMEOUT] no lease-backed route after 90s' >&2
        exit 1
    fi
    waited=$((waited+1))
    /bin/busybox sleep 1
done
echo "[M220 GENUINE DHCP READY] phase=${NAS_M220_BOOT_PHASE} waited=${waited}s"

work="/root/m220-work/${NAS_M220_BOOT_PHASE}"
mkdir -p "$work" || exit 1
# Upstream --no-capture prematurely ends its async send in M142; intentionally
# omit it. --unprivileged uses UDP sockets, not unsupported raw SOCK_RAW.
rc=0
/bin/busybox timeout 25 /usr/bin/nping --unprivileged --udp -v3 -d5 \
    --data-string "$payload" -c 1 -p 15416 10.0.2.2 \
    > "$work/out" 2> "$work/err" || rc=$?
echo "[M220 ORIGINAL NPING EXIT] phase=${NAS_M220_BOOT_PHASE} rc=$rc"
/bin/busybox tail -n 8 "$work/out" || :
/bin/busybox tail -n 6 "$work/err" >&2 || :
test "$rc" -eq 0 &&
    /bin/busybox grep -Eq 'SENT .*UDP packet with 18 bytes' "$work/out" || {
        # In M142 Nping returned zero without a real datagram. Keep the
        # actual upstream nsock write/connect failure visible in the last
        # three UART diagnostic lines even if -d5 produced a long trace.
        /bin/busybox grep -iE 'nsock|write|connect|send|error|failed' \
            "$work/err" 2>/dev/null | /bin/busybox tail -n 1 |
            /bin/busybox cut -c 1-160 |
            while IFS= read -r diagnostic; do
                [ -n "$diagnostic" ] &&
                    printf '[M220 NPING SOCKET DIAG] %s\n' "$diagnostic" >&2
            done
        echo '[M220 FAIL] original Nping did not report native UDP send' >&2
        exit 1
    }
echo "[M220 ORIGINAL NPING CLI ONLY] phase=${NAS_M220_BOOT_PHASE} payload=$payload; independent host+PCAP required"

#!/bin/sh
# Original signed Alpine nsupdate + host (RFC 2136) and Nping UDP probes.
# This file is copied unchanged into the signed M141 APK CPIO overlay;
# no guest-side DNS responder, canned query output or fake syscall is used.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy() { /bin/busybox "$@"; }

[ "$#" -eq 1 ] || exit 2
case_id=$1
work=/tmp/m142-dns-update
busy mkdir -p "$work"

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
}

case "$case_id" in
  update-and-query)
    original_elf /usr/bin/nsupdate
    original_elf /usr/bin/host
    # Explicit zone and server prevent outside resolvers or SOA discovery.
    # One RFC 2136 UPDATE adds one exact A record to a host-owned zone.
    busy printf '%s\n' \
      'server 10.0.2.2 15416' \
      'zone naseed.test.' \
      'update add updated.naseed.test. 60 A 192.0.2.99' \
      'send' > "$work/update.txt"
    update_ready=false
    for attempt in 1 2 3 4 5 6; do
      if busy timeout 18 /usr/bin/nsupdate -4 -t 8 -u 3 -r 2 \
         "$work/update.txt" > "$work/update.out" 2> "$work/update.err"; then
        update_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$update_ready" = true ]
    busy printf 'M142_ORIGINAL_NSUPDATE_RFC2136_COMMAND_PASS\n'
    query_ready=false
    for attempt in 1 2 3 4 5 6; do
      busy timeout 9 /usr/bin/host -4 -t A -p 15416 -R 1 -W 3 \
        updated.naseed.test. 10.0.2.2 > "$work/host.out" 2> "$work/host.err" || :
      if busy grep -Fq 'updated.naseed.test' "$work/host.out" &&
         busy grep -Fq '192.0.2.99' "$work/host.out"; then
        query_ready=true
        break
      fi
      busy sleep 1
    done
    [ "$query_ready" = true ]
    busy printf 'M142_ORIGINAL_HOST_POST_UPDATE_QUERY_PASS\n' ;;
  nping-udp)
    original_elf /usr/bin/nping
    # Original Nping should report sending exactly one 18-byte UDP payload.
    # Its exit code alone was zero in a real QEMU run that sent NO datagram;
    # preserve its native diagnostics on the serial console to diagnose that
    # pre-send path (socket/setsockopt/bind/connect vs actual sendto).
    nping_status=0
    busy timeout 15 /usr/bin/nping --unprivileged --udp --no-capture -v3 \
      --data-string NASEED_NPING_PROBE -c 1 -p 15416 10.0.2.2 \
      > "$work/nping.out" 2> "$work/nping.err" || nping_status=$?
    busy printf '[M142 NPING EXIT] status=%s\n' "$nping_status"
    busy printf '[M142 NPING STDOUT BEGIN]\n'
    busy cat "$work/nping.out"
    busy printf '[M142 NPING STDOUT END]\n'
    busy printf '[M142 NPING STDERR BEGIN]\n'
    busy cat "$work/nping.err"
    busy printf '[M142 NPING STDERR END]\n'
    [ "$nping_status" -eq 0 ]
    # This is only CLI evidence; the independent host UDP oracle and QEMU
    # PCAP remain the final proof that a packet actually traversed the wire.
    busy grep -Eq 'SENT .*UDP packet with 18 bytes' "$work/nping.out"
    busy printf 'M142_ORIGINAL_NPING_UDP_COMMAND_PASS\n' ;;
  *)
    busy printf '[M142 UNKNOWN CASE] %s\n' "$case_id" >&2
    exit 2 ;;
esac

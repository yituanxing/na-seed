#!/bin/sh
# Original signed Alpine lighttpd service and HTTP clients, on M197's real ext4.
# First boot installs packages separately through the existing public-APK UART.
# Second boot MUST NOT start or repair the service: OpenRC owns auto-recovery.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
document=/var/www/m227/index.txt
config=/etc/lighttpd/lighttpd.conf
expected='M227_OPENRC_HTTP_DURABLE_2026'
fail() { echo "M227_ERROR $*" >&2; exit 1; }

check_signed() {
    for name in wget aria2 lighttpd lighttpd-openrc; do
        apk info -e "$name" >/dev/null 2>&1 || fail "missing signed package $name"
    done
    for program in /usr/bin/wget /usr/bin/aria2c /usr/sbin/lighttpd; do
        test -x "$program" || fail "missing original $program"
        magic="$($busy od -An -tx1 -N4 "$program" | $busy tr -d ' \t\r\n')"
        test "$magic" = 7f454c46 || fail "not original ELF $program"
    done
    test -f /etc/init.d/lighttpd || fail 'missing official lighttpd-openrc init'
    test -x /etc/init.d/lighttpd || fail 'official init not executable'
}

http_get() {
    # M223's historical Netconn release required a small idle gap; this is
    # guest 127.0.0.1 only, not evidence for host -> guest inbound networking.
    $busy sleep 3
    attempt=0
    matched=0
    while [ "$attempt" -lt 12 ]; do
        attempt=$((attempt+1))
        if /usr/bin/wget -q -t 1 -T 3 -O /tmp/m227-wget.txt \
                http://127.0.0.1:15432/index.txt &&
                $busy cmp -s "$document" /tmp/m227-wget.txt; then
            matched=1
            break
        fi
        $busy sleep 1
    done
    test "$matched" -eq 1 || fail 'original wget loopback HTTP mismatch'
    /usr/bin/aria2c --quiet=true --disable-ipv6=true \
        --max-tries=2 --connect-timeout=8 --timeout=12 --file-allocation=none \
        --dir=/tmp --out=m227-aria2.txt --allow-overwrite=true \
        http://127.0.0.1:15432/index.txt || fail 'original aria2c HTTP GET'
    $busy cmp -s "$document" /tmp/m227-aria2.txt || fail 'aria2c body mismatch'
    echo M227_HTTP_BOTH_ORIGINAL_CLIENTS_PASS
}

case "${1:-}" in
    --first)
        check_signed
        test ! -e /var/www/m227 || fail 'first-boot persistent HTTP preseeded'
        $busy mkdir -p /var/www/m227 /etc/lighttpd /var/log
        $busy printf '%s\n' "$expected" > "$document"
        $busy chmod 755 /var/www /var/www/m227
        $busy chmod 644 "$document"
        # Keep the authentic lighttpd-openrc service script untouched;
        # configure only original distro lighttpd's documented keys.
        $busy cat > "$config" <<'CONF'
server.modules = ( "mod_staticfile" )
server.document-root = "/var/www/m227"
server.bind = "127.0.0.1"
server.port = 15432
server.pid-file = "/run/lighttpd.pid"
mimetype.assign = ( ".txt" => "text/plain" )
CONF
        /usr/sbin/lighttpd -tt -f "$config" || fail 'signed server config invalid'
        rc-update add lighttpd default || fail 'original OpenRC registration'
        test -L /etc/runlevels/default/lighttpd || fail 'OpenRC link absent'
        rc-service lighttpd start || fail 'first OpenRC lighttpd start'
        rc-service lighttpd status || fail 'first OpenRC lighttpd status'
        http_get
        /bin/bash /root/m197-software-workload.sh --reopen || fail 'M197 SQLite/Git data'
        $busy sync
        echo M227_FIRST_OPENRC_HTTP_PASS
        ;;
    --reopen)
        check_signed
        test "$($busy cat "$document")" = "$expected" || fail 'ext4 document lost'
        test -L /etc/runlevels/default/lighttpd || fail 'rc-update link not durable'
        # Deliberately no rc-service start and no raw lighttpd invocation.
        rc-status | $busy grep -q 'Runlevel: default' || fail 'not default runlevel'
        rc-service lighttpd status || fail 'OpenRC did not auto-start service'
        test -s /run/lighttpd.pid &&
            $busy kill -0 "$($busy cat /run/lighttpd.pid)" ||
            fail 'new boot has no genuine lighttpd process'
        http_get
        /bin/bash /root/m197-software-workload.sh --reopen || fail 'SQLite/Git not durable'
        $busy sync
        echo M227_SECOND_OPENRC_AUTO_HTTP_PASS
        ;;
    *) fail 'usage: --first|--reopen' ;;
esac

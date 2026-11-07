#!/bin/sh
# Official signed Alpine memcached + original OpenRC. Unlike Valkey/Mosquitto,
# memcached is ephemeral: the host must see the FIRST key disappear on reboot.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
conf=/etc/conf.d/memcached
fail() { echo "M232_GUEST_FAIL $*" >&2; exit 1; }
case "${1:-}" in --first) phase=FIRST ;; --reopen) phase=REOPEN ;; *) fail phase ;; esac
for package in memcached memcached-openrc; do
    apk info -e "$package" >/dev/null 2>&1 || fail "missing signed $package"
done
test -x /usr/bin/memcached || fail 'original memcached ELF absent'
magic="$($busy od -An -tx1 -N4 /usr/bin/memcached | $busy tr -d ' \t\r\n')"
test "$magic" = 7f454c46 || fail 'original memcached is not an ELF'
test -x /etc/init.d/memcached && test -f "$conf" || fail 'original Alpine OpenRC absent'
case "$phase" in
    FIRST)
        /bin/sh /root/m227-alpine-openrc-workload.sh --first || fail 'M227 first boot'
        test ! -e /etc/runlevels/default/memcached || fail 'preenabled runlevel'
        # NaSeed's present DAC supports test-only root daemons, not a production
        # memcached deployment. Hostfwd binds strictly to host 127.0.0.1.
        $busy cat >> "$conf" <<'EOF'
# NaSeed M232 test-only, real original Alpine OpenRC configuration.
MEMCACHED_RUNAS="root"
LISTENON="0.0.0.0"
PORT="15448"
UDPPORT="0"
MEMUSAGE="8"
MAXCONN="64"
MISC_OPTS="-t 1"
EOF
        rc-update add memcached default || fail 'original default runlevel registration'
        test -L /etc/runlevels/default/memcached || fail 'OpenRC link absent'
        rc-service memcached start || fail 'original memcached OpenRC start'
        ;;
    REOPEN)
        /bin/sh /root/m227-alpine-openrc-workload.sh --reopen || fail 'M227 second boot'
        test -L /etc/runlevels/default/memcached || fail 'OpenRC runlevel link lost'
        ;;
esac
for line in 'MEMCACHED_RUNAS="root"' 'LISTENON="0.0.0.0"' \
        'PORT="15448"' 'UDPPORT="0"' 'MAXCONN="64"' 'MISC_OPTS="-t 1"'; do
    $busy grep -qx "$line" "$conf" || fail "original config changed: $line"
done
rc-service memcached status || fail 'original memcached not managed by OpenRC'
case "$phase" in
    FIRST) echo M232_FIRST_HOST_CACHE_READY ;;
    REOPEN) echo M232_REOPEN_HOST_CACHE_READY ;;
esac
$busy sleep 55
rc-service memcached status || fail 'OpenRC memcached died during host TCP'
case "$phase" in
    FIRST) echo M232_FIRST_HOST_CACHE_WINDOW_COMPLETE ;;
    REOPEN) echo M232_REOPEN_HOST_CACHE_WINDOW_COMPLETE ;;
esac

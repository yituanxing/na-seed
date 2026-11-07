#!/bin/sh
# M229: original signed Alpine Valkey/OpenRC, external authenticated RESP,
# and RDB persisted to the same physical ext4 across two genuine PID1 boots.
# The host alone issues the unpredictable RESP SET/GET/SAVE challenges. This
# guest script does not simulate a server, manufacture replies, or start the
# service on the second boot.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
auth=/root/m229-auth-token
cfg=/etc/valkey/m229.conf
rdb=/var/lib/valkey/m229.rdb
fail() { echo "M229_GUEST_FAIL $*" >&2; exit 1; }

case "${1:-}" in
    --first) phase=FIRST ;;
    --reopen) phase=REOPEN ;;
    *) fail 'expected --first or --reopen' ;;
esac

# The package manager's signed DB and the REAL original ELFs are required.
for package in valkey valkey-cli valkey-openrc; do
    apk info -e "$package" >/dev/null 2>&1 ||
        fail "missing official signed $package"
done
for program in /usr/bin/valkey-server /usr/bin/valkey-cli; do
    test -x "$program" || fail "missing original ELF $program"
    magic="$($busy od -An -tx1 -N4 "$program" | $busy tr -d ' \t\r\n')"
    test "$magic" = 7f454c46 || fail "not an original ELF $program"
done
test -x /etc/init.d/valkey || fail 'official Alpine Valkey OpenRC script absent'
test -f /etc/conf.d/valkey || fail 'official Alpine Valkey OpenRC conf absent'

# A different random 256-bit token is staged privately by the HOST for each
# disposable disk. The first and second QEMU use that same on-disk token;
# neither a guest UART command nor a saved evidence artifact contains it.
test -f "$auth" && test ! -L "$auth" ||
    fail 'host-staged private authentication token missing'
mode="$($busy stat -c %a "$auth")"
test "$mode" = 600 || fail 'host authentication token permissions are not 0600'
token="$($busy cat "$auth")"
test "${#token}" -eq 64 || fail 'host token must be 256-bit lowercase hex'
case "$token" in *[!0123456789abcdef]*|'')
    fail 'invalid host authentication token format' ;;
esac
umask 077

case "$phase" in
    FIRST)
        # Reuse M227's already proven ORIGINAL Alpine networking and OpenRC
        # service setup instead of re-implementing DHCP, init or lighttpd.
        /bin/sh /root/m227-alpine-openrc-workload.sh --first ||
            fail 'M227 original first PID1/networking/OpenRC baseline'
        test ! -e "$cfg" && test ! -e "$rdb" ||
            fail 'first boot started with preseeded Valkey config or database'
        test ! -e /etc/runlevels/default/valkey ||
            fail 'first boot already registered Valkey service'

        $busy mkdir -p /etc/valkey /var/lib/valkey
        $busy chown valkey:valkey /var/lib/valkey ||
            fail 'official Alpine valkey account missing'
        $busy chmod 0750 /var/lib/valkey
        # This is a separate test-only original daemon configuration. Keep
        # Alpine's packaged /etc/valkey/valkey.conf and init.d untouched.
        # Protected mode and a strong per-run password remain enabled even
        # though hostfwd listens on HOST 127.0.0.1 only.
        $busy cat > "$cfg" <<VALKEY_CONFIG
bind 0.0.0.0
protected-mode yes
port 15445
daemonize no
supervised no
dir /var/lib/valkey
dbfilename m229.rdb
rdbcompression no
save ""
appendonly no
requirepass $token
logfile ""
maxclients 8
VALKEY_CONFIG
        $busy chown root:valkey "$cfg"
        $busy chmod 0640 "$cfg"
        # The official init script respects this documented OpenRC conf.d
        # variable, and still owns daemon startup and its PID file.
        $busy printf '%s\n' 'cfgfile="/etc/valkey/m229.conf"' >> /etc/conf.d/valkey
        rc-update add valkey default || fail 'original Valkey runlevel registration'
        test -L /etc/runlevels/default/valkey ||
            fail 'official OpenRC runlevel link absent'
        rc-service valkey start || fail 'original Valkey OpenRC first start'
        ;;
    REOPEN)
        # Both services must have been started by Alpine's ORIGINAL boot and
        # default runlevels already. Never invoke rc-service start/restart.
        /bin/sh /root/m227-alpine-openrc-workload.sh --reopen ||
            fail 'M227 second original PID1/OpenRC automatic recovery'
        test -s "$rdb" || fail 'first-boot host SAVE absent from physical ext4'
        test -L /etc/runlevels/default/valkey ||
            fail 'original Valkey default runlevel symlink not durable'
        rc-status | $busy grep -q 'Runlevel: default' ||
            fail 'original Alpine PID1 not in default runlevel'
        ;;
esac

test -f "$cfg" && test ! -L "$cfg" || fail 'original Valkey config lost'
test "$($busy stat -c %a "$cfg")" = 640 ||
    fail 'original Valkey config permissions changed'
$busy grep -qx 'port 15445' "$cfg" || fail 'original Valkey port changed'
$busy grep -qx 'protected-mode yes' "$cfg" || fail 'Valkey protected mode disabled'
$busy grep -qx 'dbfilename m229.rdb' "$cfg" || fail 'RDB name changed'
$busy grep -qx 'rdbcompression no' "$cfg" || fail 'RDB compression oracle changed'
$busy grep -qx 'save ""' "$cfg" || fail 'host-triggered explicit SAVE disabled'
$busy grep -qx 'appendonly no' "$cfg" || fail 'RDB-only oracle configuration changed'
$busy grep -qx 'requirepass [0-9a-f]*' "$cfg" ||
    fail 'random authenticated RESP password lost'
rc-service valkey status || fail 'original OpenRC Valkey daemon not running'

# The guest's original CLI provides an independent authenticated liveness
# observation; the host must still prove *actual inbound* RESP and PCAP.
ready=0
attempt=0
while [ "$attempt" -lt 15 ]; do
    attempt=$((attempt+1))
    if VALKEYCLI_AUTH="$token" /usr/bin/valkey-cli -h 127.0.0.1 -p 15445 \
            PING 2>/dev/null | $busy grep -qx PONG; then
        ready=1
        break
    fi
    $busy sleep 1
done
test "$ready" -eq 1 || fail 'original authenticated Valkey CLI PING failed'

case "$phase" in
    FIRST) echo M229_FIRST_HOST_RESP_READY ;;
    REOPEN) echo M229_REOPEN_HOST_RESP_READY ;;
esac

# The independent host watcher must AUTH/PING and SET/GET/SAVE while this
# exact native QEMU is alive. No guest-side fabricated SET or SAVE is allowed.
$busy sleep 55
rc-service valkey status || fail 'original OpenRC service died during host RESP'
test -s "$rdb" || fail 'host-authenticated SAVE did not create an ext4 RDB'
case "$phase" in
    FIRST) echo M229_FIRST_HOST_RESP_WINDOW_COMPLETE ;;
    REOPEN) echo M229_REOPEN_HOST_RESP_WINDOW_COMPLETE ;;
esac

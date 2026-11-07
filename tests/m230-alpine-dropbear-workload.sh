#!/bin/sh
# M230: original signed Alpine Dropbear/OpenRC and independent host SSH.
# The first original PID1 boot registers and starts the ORIGINAL init script;
# the second original PID1 boot must recover the service without any manual
# restart.  A disposable host-generated client private key NEVER enters the
# guest image; only its public half is staged here before the first boot.
# This is an isolated root-only functional test, NOT a secure production SSH
# configuration: NaSeed does not yet provide complete non-root DAC semantics
# or a hardware-rooted entropy guarantee.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
pub=/root/m230-authorized-pubkey
authorized=/root/.ssh/authorized_keys
hostkey=/etc/dropbear/dropbear_ed25519_host_key
fingerprint=/root/m230-dropbear-hostkey.sha256
opts='DROPBEAR_OPTS="-s -g -j -k -m -p 0.0.0.0:15446 -r /etc/dropbear/dropbear_ed25519_host_key"'
fail() { echo "M230_GUEST_FAIL $*" >&2; exit 1; }

case "${1:-}" in
    --first) phase=FIRST ;;
    --reopen) phase=REOPEN ;;
    *) fail 'expected --first or --reopen' ;;
esac

for package in dropbear dropbear-openrc; do
    apk info -e "$package" >/dev/null 2>&1 ||
        fail "missing original signed Alpine $package"
done
for executable in /usr/sbin/dropbear /usr/bin/dropbearkey; do
    test -x "$executable" || fail "original executable absent: $executable"
    magic="$($busy od -An -tx1 -N4 "$executable" | $busy tr -d ' \t\r\n')"
    test "$magic" = 7f454c46 || fail "not original ELF: $executable"
done
test -x /etc/init.d/dropbear || fail 'original Alpine Dropbear OpenRC init absent'
test -f /etc/conf.d/dropbear || fail 'original Alpine Dropbear conf absent'

# Reject fake credentials, a guest-provided private key and multiline content.
# The untrusted host public key is data only; never evaluate or execute it.
test -f "$pub" && test ! -L "$pub" ||
    fail 'missing host-generated disposable Ed25519 public key'
test "$($busy wc -l < "$pub")" -eq 1 ||
    fail 'host public key must contain exactly one line'
read -r algorithm base64 rest < "$pub" ||
    fail 'cannot read host public key'
test "$algorithm" = ssh-ed25519 || fail 'expected an Ed25519 public key'
test "${#base64}" -ge 68 && test "${#base64}" -le 2048 ||
    fail 'invalid Ed25519 public key length'
case "$base64" in *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=]*)
    fail 'host public key has invalid base64' ;;
esac

case "$phase" in
    FIRST)
        # Reuse the established original public-APK/OpenRC networking and
        # durable lighttpd path, not a second hand-written init or DHCP path.
        /bin/sh /root/m227-alpine-openrc-workload.sh --first ||
            fail 'original M227 Alpine PID1/networking/OpenRC baseline'
        test ! -e "$authorized" && test ! -e "$fingerprint" ||
            fail 'first boot has a preseeded SSH identity or receipt'
        test ! -e /etc/runlevels/default/dropbear ||
            fail 'first boot already registered Dropbear OpenRC service'

        umask 077
        $busy mkdir -p /root/.ssh /etc/dropbear
        $busy chmod 0700 /root /root/.ssh
        # `restrict` enforces Dropbear's own no-PTY/no-forwarding key policy
        # in addition to disabling all passwords and TCP forwarding globally.
        $busy printf 'restrict ssh-ed25519 %s\n' "$base64" > "$authorized"
        $busy chmod 0600 "$authorized"

        # The packaged OpenRC script generates the original host keys, forks
        # `/usr/sbin/dropbear`, and supplies its own process supervision.
        # Preserve the distro script and only extend its intended conf.d.
        $busy printf '\n%s\n' "$opts" >> /etc/conf.d/dropbear
        rc-update add dropbear default || fail 'original Dropbear registration'
        test -L /etc/runlevels/default/dropbear ||
            fail 'original default runlevel link missing'
        rc-service dropbear start || fail 'original Dropbear OpenRC start'
        ;;
    REOPEN)
        # Never invoke rc-service start here. PID1's original Alpine OpenRC
        # default runlevel must recover the same SSH listener automatically.
        /bin/sh /root/m227-alpine-openrc-workload.sh --reopen ||
            fail 'original M227 Alpine second boot/OpenRC baseline'
        test -L /etc/runlevels/default/dropbear ||
            fail 'Dropbear OpenRC default runlevel registration not durable'
        test -s "$fingerprint" || fail 'first boot Dropbear identity absent'
        rc-status | $busy grep -q 'Runlevel: default' ||
            fail 'original OpenRC default runlevel not active'
        ;;
esac

$busy grep -qxF "$opts" /etc/conf.d/dropbear ||
    fail 'Dropbear root key-only / no-forwarding port configuration lost'
test -s "$authorized" && test ! -L "$authorized" ||
    fail 'host public key not persisted'
test "$($busy stat -c %a /root/.ssh)" = 700 ||
    fail 'root authorized_keys directory mode changed'
test "$($busy stat -c %a "$authorized")" = 600 ||
    fail 'root authorized_keys file mode changed'
$busy grep -qxF "restrict ssh-ed25519 $base64" "$authorized" ||
    fail 'persisted SSH authorized key differs from host public key'
rc-service dropbear status || fail 'original Alpine Dropbear not running'
test -s /var/run/dropbear.pid || fail 'original Dropbear PID file absent'
$busy kill -0 "$($busy cat /var/run/dropbear.pid)" ||
    fail 'no actual original Dropbear daemon process'
test -s "$hostkey" && test ! -L "$hostkey" ||
    fail 'original Alpine OpenRC did not generate host Ed25519 key'

actual="$($busy sha256sum "$hostkey" | $busy awk '{ print $1 }')"
test "${#actual}" -eq 64 || fail 'invalid original host key checksum'
case "$phase" in
    FIRST)
        $busy printf '%s\n' "$actual" > "$fingerprint"
        $busy chmod 0600 "$fingerprint"
        echo M230_FIRST_HOST_SSH_READY
        ;;
    REOPEN)
        test "$($busy cat "$fingerprint")" = "$actual" ||
            fail 'original Dropbear host key changed after genuine second boot'
        echo M230_REOPEN_HOST_SSH_READY
        ;;
esac

# The external host must now establish a genuine encrypted SSH session and
# execute its unpredictable nonce command. Guest readiness alone is NOT PASS.
$busy sleep 150
rc-service dropbear status || fail 'original Dropbear died during SSH window'
$busy kill -0 "$($busy cat /var/run/dropbear.pid)" ||
    fail 'original Dropbear exited during host SSH challenge'
case "$phase" in
    FIRST) echo M230_FIRST_HOST_SSH_WINDOW_COMPLETE ;;
    REOPEN) echo M230_REOPEN_HOST_SSH_WINDOW_COMPLETE ;;
esac

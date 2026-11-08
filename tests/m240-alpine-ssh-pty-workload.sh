#!/bin/sh
# M240: original signed Alpine Dropbear/OpenRC and independent host SSH PTY.
# Distinct from M230: that baseline MUST keep its `restrict`/no-pty key.
# One disposable host public key permits PTY while denying forwarding.
# Isolated root-only functional validation, NOT production SSH security.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
export PATH
busy=/bin/busybox
pub=/root/m240-authorized-pubkey
authorized=/root/.ssh/authorized_keys
hostkey=/etc/dropbear/dropbear_ed25519_host_key
fingerprint=/root/m240-dropbear-hostkey.sha256
opts='DROPBEAR_OPTS="-s -g -j -k -m -p 0.0.0.0:15446 -r /etc/dropbear/dropbear_ed25519_host_key"'
key_policy=no-port-forwarding,no-agent-forwarding,no-X11-forwarding
fail() { echo "M240_GUEST_FAIL $*" >&2; exit 1; }

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
    test -x "$executable" || fail "original signed executable missing: $executable"
    magic="$($busy od -An -tx1 -N4 "$executable" | $busy tr -d ' \t\r\n')"
    test "$magic" = 7f454c46 || fail "original executable is not ELF: $executable"
done
test -x /etc/init.d/dropbear && test -f /etc/conf.d/dropbear ||
    fail 'original Alpine OpenRC Dropbear scripts absent'
test -f "$pub" && test ! -L "$pub" ||
    fail 'host-generated disposable Ed25519 public key absent'
test "$($busy wc -l < "$pub")" -eq 1 || fail 'multiline host SSH public key'
read -r algorithm base64 rest < "$pub" || fail 'invalid host SSH key input'
test "$algorithm" = ssh-ed25519 || fail 'host key must be Ed25519'
test "${#base64}" -ge 68 && test "${#base64}" -le 2048 ||
    fail 'invalid host key length'
case "$base64" in
    *[!ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=]*)
        fail 'invalid host key base64' ;;
esac

case "$phase" in
    FIRST)
        /bin/sh /root/m227-alpine-openrc-workload.sh --first ||
            fail 'original M227 signed Alpine/OpenRC baseline failed'
        test ! -e "$authorized" && test ! -e "$fingerprint" ||
            fail 'first boot was preseeded with SSH identity'
        test ! -e /etc/runlevels/default/dropbear ||
            fail 'original Dropbear default runlevel preenabled'
        umask 077
        $busy mkdir -p /root/.ssh /etc/dropbear
        $busy chmod 0700 /root /root/.ssh
        # Dropbear has no OpenSSH-style `restrict,pty` override. Only this
        # M240 key permits PTY; forwarding/agent/X11 stay prohibited.
        $busy printf '%s ssh-ed25519 %s\n' "$key_policy" "$base64" > "$authorized"
        $busy chmod 0600 "$authorized"
        $busy printf '\n%s\n' "$opts" >> /etc/conf.d/dropbear
        rc-update add dropbear default || fail 'Dropbear OpenRC registration failed'
        test -L /etc/runlevels/default/dropbear ||
            fail 'original Dropbear OpenRC registration link missing'
        rc-service dropbear start || fail 'original Dropbear OpenRC start failed'
        ;;
    REOPEN)
        # Real second original PID1/OpenRC must restore Dropbear without
        # this workload ever invoking `rc-service dropbear start`.
        /bin/sh /root/m227-alpine-openrc-workload.sh --reopen ||
            fail 'original M227 PID1 second boot/OpenRC baseline failed'
        test -L /etc/runlevels/default/dropbear ||
            fail 'Dropbear default runlevel not persisted'
        test -s "$fingerprint" || fail 'first boot host key attestation absent'
        rc-status | $busy grep -q 'Runlevel: default' ||
            fail 'original OpenRC default runlevel not active'
        ;;
esac

$busy grep -qxF "$opts" /etc/conf.d/dropbear ||
    fail 'root key-only Dropbear policy not durable'
test -s "$authorized" && test ! -L "$authorized" ||
    fail 'guest authorized_keys missing or a symlink'
test "$($busy stat -c %a /root/.ssh)" = 700 || fail 'SSH directory mode changed'
test "$($busy stat -c %a "$authorized")" = 600 || fail 'SSH key file mode changed'
$busy grep -qxF "$key_policy ssh-ed25519 $base64" "$authorized" ||
    fail 'PTY-only test key or its forwarding restrictions changed'
rc-service dropbear status || fail 'original Dropbear not running'
test -s /var/run/dropbear.pid || fail 'original Dropbear pid absent'
$busy kill -0 "$($busy cat /var/run/dropbear.pid)" ||
    fail 'original Dropbear daemon process absent'
test -s "$hostkey" && test ! -L "$hostkey" ||
    fail 'original Alpine Dropbear host Ed25519 identity absent'

actual="$($busy sha256sum "$hostkey" | $busy awk '{ print $1 }')"
test "${#actual}" -eq 64 || fail 'invalid host key SHA256'
case "$phase" in
    FIRST)
        $busy printf '%s\n' "$actual" > "$fingerprint"
        $busy chmod 0600 "$fingerprint"
        echo M240_FIRST_HOST_SSH_PTY_READY
        ;;
    REOPEN)
        test "$($busy cat "$fingerprint")" = "$actual" ||
            fail 'original Dropbear host key changed after second PID1 boot'
        echo M240_REOPEN_HOST_SSH_PTY_READY
        ;;
esac

# Independent host must establish nine genuine -tt sessions, all with an
# actual guest /dev/pts/N. Guest readiness alone is NEVER final acceptance.
$busy sleep 300
rc-service dropbear status || fail 'Dropbear exited during PTY host validation'
$busy kill -0 "$($busy cat /var/run/dropbear.pid)" ||
    fail 'original Dropbear PID disappeared during SSH PTY sessions'
case "$phase" in
    FIRST) echo M240_FIRST_HOST_SSH_PTY_WINDOW_COMPLETE ;;
    REOPEN) echo M240_REOPEN_HOST_SSH_PTY_WINDOW_COMPLETE ;;
esac

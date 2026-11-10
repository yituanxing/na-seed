#!/usr/bin/dash
# Runlevel-0 inittab action is launched by original Debian PID 1, not a
# replacement init. The nonce is persisted on the REAL writable ext4 root;
# the signed coreutils sync and signed SysVinit halt are separate real ELF.
set -eu
exec </dev/console >/dev/console 2>&1
test -p /run/initctl || exit 92
test -f /var/tmp/m255-sysvinit/runlevel3 || exit 93
test ! -e /var/tmp/m255-sysvinit/m257-poweroff-nonce || exit 94
/usr/bin/cat /etc/m257-nonce-input > /var/tmp/m255-sysvinit/m257-poweroff-nonce
/usr/bin/printf '%s\n' '[M257 RUNLEVEL0] original SysVinit launched reviewed runlevel 0 action'
/usr/bin/sync
/usr/bin/printf '%s\n' '[M257 SYNC] original Debian sync completed for physical ext4 nonce'
exec /usr/sbin/halt -p -f

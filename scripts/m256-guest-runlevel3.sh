#!/usr/bin/dash
set -eu
exec </dev/console >/dev/console 2>&1
test -p /run/initctl
test -f /var/tmp/m255-sysvinit/respawn
/usr/bin/printf '%s\n' M256-RUNLEVEL3-GUEST > /var/tmp/m255-sysvinit/runlevel3
/usr/bin/printf '%s\n' '[M256 RUNLEVEL] original Debian init executed runlevel 3 inittab action'

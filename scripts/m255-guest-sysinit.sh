#!/usr/bin/dash
# Executed by ORIGINAL Debian sysvinit PID1 according to /etc/inittab.
# The shell implements a service action, never pretends to be init itself.
set -eu
exec </dev/console >/dev/console 2>&1
/usr/bin/printf '%s\n' '[M255 SYSINIT] original Debian init launched inittab sysinit action'
/usr/bin/mkdir -p /var/tmp/m255-sysvinit
/usr/bin/printf '%s\n' M255-SYSINIT-GUEST > /var/tmp/m255-sysvinit/sysinit

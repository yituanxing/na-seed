#!/usr/bin/dash
# ORIGINAL Debian sysvinit :respawn action; intentionally exits once.  A second
# process with a new PID can only appear if the real PID1 reaps and respawns.
set -eu
exec </dev/console >/dev/console 2>&1
test -f /var/tmp/m255-sysvinit/sysinit || exit 51
if test ! -f /var/tmp/m255-sysvinit/first; then
    /usr/bin/printf '%s\n' "M255-SERVICE-FIRST-PID=$$"
    /usr/bin/printf '%s\n' M255-SERVICE-FIRST-GUEST > /var/tmp/m255-sysvinit/first
    exit 0
fi
test "$(/usr/bin/cat /var/tmp/m255-sysvinit/first)" = M255-SERVICE-FIRST-GUEST || exit 52
/usr/bin/printf '%s\n' "M255-SERVICE-RESPAWN-PID=$$"
/usr/bin/printf '%s\n' M255-SERVICE-RESPAWN-GUEST > /var/tmp/m255-sysvinit/respawn
# Keep the second service process alive; UART must observe this original
# signed glibc sleep ELF as a supervised grandchild, not only a shell echo.
exec /usr/bin/sleep 3600

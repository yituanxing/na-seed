#!/usr/bin/dash
# The original Debian sysvinit waits for sysinit to finish before actions
# in the default runlevel. Check actual guest-created service and FIFO first.
set -eu
exec </dev/console >/dev/console 2>&1
n=0
while [ ! -p /run/initctl ] || [ ! -f /var/tmp/m255-sysvinit/respawn ]; do
    n=$((n + 1))
    test "$n" -lt 30 || exit 88
    /usr/bin/sleep 1
done
# The respawn shell persists its marker just before execing original Debian
# sleep. Give that real ELF handoff a scheduling boundary before runlevel 3
# terminates all runlevel-2 services.
/usr/bin/sleep 1
/usr/bin/printf '%s\n' '[M256 INITCTL] original PID1 FIFO is an actual named pipe'
/usr/sbin/telinit 3
/usr/bin/printf '%s\n' '[M256 INITCTL] signed Debian telinit submitted runlevel 3'

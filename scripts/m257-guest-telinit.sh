#!/usr/bin/dash
# Genuine SysVinit runlevel-3 inittab action. Only the original signed
# /usr/sbin/telinit may request the transition to runlevel 0.
set -eu
exec </dev/console >/dev/console 2>&1
n=0
while [ ! -p /run/initctl ] || [ ! -f /var/tmp/m255-sysvinit/runlevel3 ]; do
    n=$((n + 1))
    if [ "$n" -ge 30 ]; then
        /usr/bin/printf '%s\n' '[M257 SHUTDOWN FAIL] missing original initctl or runlevel 3'
        exit 91
    fi
    /usr/bin/sleep 1
done
/usr/bin/sleep 1
/usr/bin/printf '%s\n' '[M257 INITCTL] real /run/initctl named FIFO ready'
# The switch can terminate its runlevel-3 caller. The actual PID1 runlevel-0
# action and signed ELF exec are the proof, not a post-telinit shell message.
exec /usr/sbin/telinit 0

#!/usr/bin/dash
set -eu
exec </dev/console >/dev/console 2>&1
/sbin/m256-mount-run
/etc/m255-guest-sysinit.sh

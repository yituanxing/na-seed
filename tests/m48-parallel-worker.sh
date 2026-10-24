#!/bin/sh
# Genuine GNU xargs -P2 starts two independent shell/exec children. If
# xargs serializes them, rendezvous fails instead of emitting false PASS.
set -eu
case "$1" in
  alpha) own=/tmp/m48-xargs-a peer=/tmp/m48-xargs-b ;;
  beta)  own=/tmp/m48-xargs-b peer=/tmp/m48-xargs-a ;;
  *) echo "[M48 FAIL] unexpected worker" >&2; exit 9 ;;
esac
/bin/busybox touch "$own"
n=0
until test -e "$peer"; do
  n=$((n+1))
  test "$n" -lt 20
  /bin/busybox sleep 1
done
# Both executable files were built by GNU GCC inside NaSeed.
/tmp/m48-alpha
/tmp/m48-beta
echo "M48_XARGS_WORKER_$1"

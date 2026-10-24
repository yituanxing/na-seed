#!/bin/sh
# Real GNU xargs -P4 must run all four shell workers concurrently.
set -eu
case "$1" in
  alpha|beta|gamma|delta) name=$1 ;;
  *) echo "[M49] bad xargs worker" >&2; exit 99 ;;
esac
/bin/busybox touch "/tmp/m49-xargs-$name-ready"
n=0
until test -e /tmp/m49-xargs-alpha-ready &&
      test -e /tmp/m49-xargs-beta-ready &&
      test -e /tmp/m49-xargs-gamma-ready &&
      test -e /tmp/m49-xargs-delta-ready; do
  n=$((n+1))
  test "$n" -lt 35
  /bin/busybox sleep 1
done
"/tmp/m49-$name"
echo "M49_XARGS_${name}_PASS"

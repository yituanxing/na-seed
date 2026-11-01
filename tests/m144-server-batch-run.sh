#!/bin/sh
# Independent cases: a failed server must not conceal the remaining results.
set -eu
dir=/tmp/m144; /bin/busybox mkdir -p "$dir"
ready=false
for attempt in 1 2 3 4 5 6 7 8 9 10; do
  if /usr/bin/curl -f -s -S --noproxy '*' --connect-timeout 4 \
     -o "$dir/health" http://10.0.2.2:15441/health &&
     /bin/busybox grep -Fxq M144_HOST_HTTP_READY "$dir/health"; then
    ready=true; break
  fi
  /bin/busybox sleep 1
done
[ "$ready" = true ] || { echo M144_HOST_NOT_READY; exit 1; }
failed=0
for name in ab nginx haproxy valkey; do
  if /bin/sh "/tests/m144-$name-run.sh"; then
    echo "M144_CASE_${name}_PASS"
  else
    echo "M144_CASE_${name}_FAIL"
    failed=$((failed+1))
  fi
done
echo "[M144 SUMMARY] failed=$failed total=4"
[ "$failed" -eq 0 ] || exit 1
echo M144_SERVER_COHORT_PASS

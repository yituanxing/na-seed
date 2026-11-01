#!/bin/sh
set -eu
dir=/tmp/m144/haproxy
/bin/busybox mkdir -p "$dir"
/bin/busybox cat > "$dir/haproxy.cfg" <<'EOF'
global
    maxconn 4
    nbthread 1
    noreuseport
defaults
    mode http
    timeout connect 5s
    timeout client 8s
    timeout server 8s
frontend guest-http
    bind 127.0.0.1:15444
    default_backend host-http
backend host-http
    server host1 10.0.2.2:15441
EOF
if ! /usr/sbin/haproxy -c -f "$dir/haproxy.cfg" > "$dir/check" 2>&1; then
  /bin/busybox sed -n "1,120p" "$dir/check"; exit 1
fi
/usr/sbin/haproxy -db -V -f "$dir/haproxy.cfg" > "$dir/output" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :' EXIT
/bin/busybox sleep 1
echo '[M144 HAPROXY DIAG] guest native process startup output:'
/bin/busybox sed -n '1,100p' "$dir/output" || :
if ! kill -0 "$pid" 2>/dev/null; then
  echo '[M144 HAPROXY DIAG] server exited before curl'
fi
if ! /usr/bin/curl -f -s -S --noproxy '*' --retry 3 --retry-delay 1 \
  --retry-connrefused -o "$dir/received" http://127.0.0.1:15444/haproxy; then
  /bin/busybox sed -n "1,160p" "$dir/output" || :; exit 1
fi
/bin/busybox grep -Fxq M144_HOST_HAPROXY_BACKEND_VALUE "$dir/received"
echo M144_SIGNED_HAPROXY_PASS

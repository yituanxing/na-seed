#!/bin/sh
set -eu
dir=/tmp/m144/nginx
/bin/busybox mkdir -p "$dir/site"
echo M144_REAL_NGINX_STATIC_RESPONSE > "$dir/site/item.txt"
/bin/busybox cat > "$dir/nginx.conf" <<EOF
worker_processes 1;
pid $dir/nginx.pid;
error_log $dir/nginx.err notice;
daemon off;
master_process off;
events { worker_connections 16; }
http { access_log off; default_type text/plain;
    server { listen 127.0.0.1:15443; root $dir/site; }
}
EOF
if ! /usr/sbin/nginx -t -p "$dir/" -c "$dir/nginx.conf" > "$dir/check" 2>&1; then
  /bin/busybox cat "$dir/check"; exit 1
fi
/usr/sbin/nginx -p "$dir/" -c "$dir/nginx.conf" > "$dir/output" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :' EXIT
/bin/busybox sleep 1
if ! /usr/bin/curl -f -s -S --noproxy '*' --retry 3 --retry-delay 1 \
  --retry-connrefused -o "$dir/received" http://127.0.0.1:15443/item.txt; then
  /bin/busybox cat "$dir/output" || :
  /bin/busybox cat "$dir/nginx.err" || :
  exit 1
fi
/bin/busybox cmp "$dir/site/item.txt" "$dir/received"
echo M144_SIGNED_NGINX_PASS

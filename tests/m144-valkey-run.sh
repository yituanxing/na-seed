#!/bin/sh
set -eu
dir=/tmp/m144/valkey
/bin/busybox mkdir -p "$dir"
/bin/busybox cat > "$dir/valkey.conf" <<EOF
bind 127.0.0.1
port 15445
daemonize no
maxclients 8
protected-mode no
save ""
appendonly no
dir $dir
logfile ""
EOF
/usr/bin/valkey-server "$dir/valkey.conf" > "$dir/server.log" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || :; wait "$pid" 2>/dev/null || :' EXIT
ready=false
for attempt in 1 2 3 4 5; do
  if /usr/bin/valkey-cli -h 127.0.0.1 -p 15445 ping > "$dir/ping" 2>&1 &&
    /bin/busybox grep -Fxq PONG "$dir/ping"; then ready=true; break; fi
  /bin/busybox sleep 1
done
if [ "$ready" != true ]; then
  /bin/busybox cat "$dir/server.log" || :; /bin/busybox cat "$dir/ping" || :; exit 1
fi
/usr/bin/valkey-cli -h 127.0.0.1 -p 15445 SET naseed M144_REAL_RESP_VALUE > "$dir/set"
/bin/busybox grep -Fxq OK "$dir/set"
/usr/bin/valkey-cli -h 127.0.0.1 -p 15445 GET naseed > "$dir/get"
/bin/busybox grep -Fxq M144_REAL_RESP_VALUE "$dir/get"
echo M144_SIGNED_VALKEY_SERVER_CLI_PASS

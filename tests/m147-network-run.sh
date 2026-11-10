#!/bin/sh
# One native-QEMU boot for 29 separately hashed Alpine networking ELF images.
# Version/help probes prove these original guest binaries actually enter execve;
# the three loopback cases separately demand meaningful protocol behavior.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
busy=/bin/busybox
work=/tmp/m147-network
"$busy" mkdir -p "$work"

failed=0
count=0
while IFS= read -r program; do
  case "$program" in ''|'#'*) continue ;; esac
  count=$((count+1))
  case "$program" in
    usr/sbin/bird|usr/sbin/birdc|usr/sbin/birdcl|usr/bin/chronyc|usr/sbin/chronyd|usr/sbin/fping)
      option=-v ;;
    usr/sbin/unbound) option=-V ;;
    usr/bin/stunnel) option=-version ;;
    usr/bin/websocat|usr/bin/whois|usr/sbin/darkstat|usr/sbin/dnsmasq|usr/sbin/mtr|usr/bin/h2load|usr/bin/nghttp|usr/bin/nghttpd|usr/bin/nghttpx)
      option=--version ;;
    *) option=-h ;;
  esac
  # BusyBox timeout makes a misbehaving program a bounded failing probe.
  # Help usage may legitimately exit nonzero; a missing executable, crash,
  # timeout, or empty output cannot be treated as a successful admission.
  rc=0
  "$busy" timeout 6 "/$program" "$option" > "$work/load-$count.log" 2>&1 || rc=$?
  case "$rc" in
    124|125|126|127|132|133|134|135|136|137|138|139|140|141|142|143)
      echo "[M147 LOAD FAIL] /$program exit=$rc"; failed=$((failed+1)) ;;
  esac
  if ! test -s "$work/load-$count.log"; then
    echo "[M147 LOAD FAIL] /$program empty output"; failed=$((failed+1))
  fi
done < /tests/m147-original-elfs.txt
echo "[M147 LOAD SUMMARY] count=$count failures=$failed"
[ "$count" -eq 29 ] && [ "$failed" -eq 0 ] || exit 1
echo M147_29_REAL_ELF_LOAD_PASS
protocol_failures=0

# M147 protocol 1: signed memcached server and a signed Alpine Ncat client
# must exchange a real ASCII storage/retrieval transaction over TCP loopback.
"$busy" mkdir -p "$work/memcached"
# Official memcached sets both RLIMIT_NOFILE values to -c at startup.
# NaSeed honestly enforces 64 FDs, so requesting 64 is a valid no-op;
# this is a one-client functional check, not a 64-peer concurrency claim.
/usr/bin/memcached -u root -l 127.0.0.1 -p 15471 -U 0 -m 8 -c 64 -t 1 -vv \
  > "$work/memcached/server.log" 2>&1 &
mem_pid=$!
mem_ok=false
for attempt in 1 2 3 4 5; do
  # Keep stdin open briefly after writing the whole ASCII transaction.  The
  # client must not signal EOF before memcached's worker has read the payload.
  client_rc=0
  { "$busy" printf 'set k 0 0 5\r\nhello\r\nget k\r\nquit\r\n'; "$busy" sleep 2; } |
    "$busy" timeout 9 /usr/bin/ncat -4 127.0.0.1 15471 \
      > "$work/memcached/reply" 2>&1 || client_rc=$?
  reply_bytes=$("$busy" wc -c < "$work/memcached/reply")
  echo "[M147 MEMCACHED ATTEMPT] $attempt client_rc=$client_rc reply_bytes=$reply_bytes"
  if [ "$client_rc" -eq 0 ] &&
     "$busy" grep -Fq 'STORED' "$work/memcached/reply" &&
     "$busy" grep -Fq 'VALUE k 0 5' "$work/memcached/reply" &&
     "$busy" grep -Fq 'hello' "$work/memcached/reply"; then
    mem_ok=true; break
  fi
  "$busy" sleep 1
done
kill "$mem_pid" 2>/dev/null || :
wait "$mem_pid" 2>/dev/null || :
if [ "$mem_ok" = true ]; then
  echo M147_REAL_MEMCACHED_TCP_PASS
else
  echo '[M147 MEMCACHED SERVER LOG BEGIN]'
  "$busy" cat "$work/memcached/server.log" || :
  echo '[M147 MEMCACHED SERVER LOG END]'
  echo '[M147 MEMCACHED CLIENT REPLY BEGIN]'
  "$busy" cat "$work/memcached/reply" || :
  echo '[M147 MEMCACHED CLIENT REPLY END]'
  "$busy" od -An -tx1 "$work/memcached/reply" || :
  echo M147_REAL_MEMCACHED_TCP_FAIL
  protocol_failures=$((protocol_failures+1))
fi
"$busy" sleep 2

# M147 protocol 2: the original MQTT broker, subscriber and publisher must
# agree on one exactly delivered payload; this is not a synthetic socket test.
"$busy" mkdir -p "$work/mqtt"
"$busy" cat > "$work/mqtt/mosquitto.conf" <<EOF
listener 15472 127.0.0.1
allow_anonymous true
user root
persistence false
log_dest stdout
EOF
/usr/sbin/mosquitto -c "$work/mqtt/mosquitto.conf" \
  > "$work/mqtt/broker.log" 2>&1 &
mqtt_pid=$!
"$busy" sleep 2
/usr/bin/mosquitto_sub -h 127.0.0.1 -p 15472 -t naseed/m147 \
  -C 1 -W 12 > "$work/mqtt/sub.out" 2> "$work/mqtt/sub.err" &
sub_pid=$!
"$busy" sleep 1
pub_ok=false
if /usr/bin/mosquitto_pub -h 127.0.0.1 -p 15472 -t naseed/m147 \
   -m M147_SIGNED_MQTT_PAYLOAD > "$work/mqtt/pub.log" 2>&1; then
  pub_ok=true
fi
sub_ok=false
if wait "$sub_pid"; then sub_ok=true; fi
kill "$mqtt_pid" 2>/dev/null || :
wait "$mqtt_pid" 2>/dev/null || :
if [ "$pub_ok" != true ] || [ "$sub_ok" != true ] ||
   ! "$busy" grep -Fxq M147_SIGNED_MQTT_PAYLOAD "$work/mqtt/sub.out"; then
  "$busy" cat "$work/mqtt/broker.log" "$work/mqtt/sub.err" "$work/mqtt/pub.log" || :
  echo M147_REAL_MQTT_PUBSUB_FAIL
  protocol_failures=$((protocol_failures+1))
else
  echo M147_REAL_MQTT_PUBSUB_PASS
fi
"$busy" sleep 2

# M147 protocol 3: original nghttpd serves a static file over HTTP/2 prior
# knowledge (h2c); original nghttp must retrieve byte-exact response content.
"$busy" mkdir -p "$work/http2/site"
"$busy" printf 'M147_SIGNED_HTTP2_PAYLOAD\n' > "$work/http2/site/index.txt"
/usr/bin/nghttpd --no-tls -d "$work/http2/site" 15473 \
  > "$work/http2/server.log" 2>&1 &
h2_pid=$!
http2_ok=false
for attempt in 1 2 3 4 5; do
  if /usr/bin/nghttp http://127.0.0.1:15473/index.txt \
      > "$work/http2/response" 2> "$work/http2/client.err" &&
      "$busy" grep -Fxq M147_SIGNED_HTTP2_PAYLOAD "$work/http2/response"; then
    http2_ok=true; break
  fi
  "$busy" sleep 1
done
kill "$h2_pid" 2>/dev/null || :
wait "$h2_pid" 2>/dev/null || :
if [ "$http2_ok" = true ]; then
  echo M147_REAL_HTTP2_H2C_PASS
else
  "$busy" cat "$work/http2/server.log" "$work/http2/client.err" "$work/http2/response" || :
  echo M147_REAL_HTTP2_H2C_FAIL
  protocol_failures=$((protocol_failures+1))
fi
if [ "$protocol_failures" -ne 0 ]; then
  echo "[M147 NETWORK FAIL] $protocol_failures/3 genuine protocols failed"
  echo M147_NETWORK_COHORT_FAIL
  exit 1
fi
echo M147_NETWORK_COHORT_PASS

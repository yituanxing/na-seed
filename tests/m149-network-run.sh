#!/bin/sh
# Real signed Net-SNMP/Knot/GnuTLS/Tinyproxy/LFTP/s6 ELF portfolio in one boot.
# A version/help launch is only an execve smoke; two independent protocols
# below enforce actual TCP/UDP application behavior instead of fake markers.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
busy=/bin/busybox
work=/tmp/m149-network
"$busy" mkdir -p "$work"

# Every target is invoked, capped independently at six seconds.  The host
# acceptance gate additionally requires the kernel's original CPIO ELF EXEC
# record for each of the 35 path identities.  Help is not a semantic PASS.
failed=0
count=0
while IFS= read -r program; do
  case "$program" in ''|'#'*) continue ;; esac
  count=$((count+1))
  case "$program" in
    usr/sbin/snmpd) option=-v ;;
    usr/sbin/snmptrapd|usr/bin/snmp*) option=-V ;;
    usr/sbin/knot*|usr/bin/kdig|usr/bin/khost|usr/sbin/keymgr|usr/sbin/kjournalprint|usr/bin/kzone*|usr/bin/knsec3hash|usr/bin/knsupdate|usr/sbin/kcatalogprint)
      option=-V ;;
    usr/bin/gnutls*|usr/bin/p11tool|usr/bin/ocsptool|usr/bin/lftp|usr/sbin/ethtool)
      option=--version ;;
    usr/bin/tinyproxy) option=-h ;;
    *) option=-h ;;
  esac
  rc=0
  "$busy" timeout 6 "/$program" "$option" > "$work/load-$count.log" 2>&1 || rc=$?
  case "$rc" in
    124|125|126|127|132|133|134|135|136|137|138|139|140|141|142|143)
      echo "[M149 LOAD FAIL] /$program exit=$rc"; failed=$((failed+1)) ;;
  esac
  if ! test -s "$work/load-$count.log"; then
    echo "[M149 LOAD FAIL] /$program empty version/help output"
    failed=$((failed+1))
  fi
done < /tests/m149-original-elfs.txt
echo "[M149 LOAD SUMMARY] count=$count failures=$failed"
protocol_failures=0
if [ "$count" -eq 35 ] && [ "$failed" -eq 0 ]; then
  echo M149_35_REAL_ELF_LOAD_PASS
else
  echo M149_35_REAL_ELF_LOAD_FAIL
fi

# Protocol 1: original Net-SNMP agent responds to two separate UDP requests
# from original snmpget/snmpwalk clients.  The OID is numeric to avoid
# conflating optional MIB text-install hooks with wire protocol correctness.
"$busy" mkdir -p "$work/snmp"
snmp_ok=false
# Net-SNMP's full Linux interface-mib initializer probes /proc/net and
# NETLINK_ROUTE, which need not exist in our current kernel. Try the documented
# -I module selection for just the genuine system MIB, with both legal IPv4
# endpoint spellings. Also retain the unmodified default-initializer mode as
# an independent control; record every failure, without synthetic responses.
for mode in minimal-loopback minimal-wildcard default-wildcard; do
  case "$mode" in
    *loopback) address=udp:127.0.0.1:15491 ;;
    *wildcard) address=udp:15491 ;;
  esac
  "$busy" cat > "$work/snmp/$mode.conf" <<EOF
agentAddress $address
rocommunity m149public 127.0.0.1
sysLocation NaSeed-M149
sysContact Kernel-Port
EOF
  case "$mode" in
    minimal-*)
      /usr/sbin/snmpd -f -Lo -C -I system_mib -Dtransport,udp \
        -c "$work/snmp/$mode.conf" > "$work/snmp/$mode.daemon.log" 2>&1 & ;;
    *)
      /usr/sbin/snmpd -f -Lo -C -Dtransport,udp \
        -c "$work/snmp/$mode.conf" > "$work/snmp/$mode.daemon.log" 2>&1 & ;;
  esac
  snmp_pid=$!
  for attempt in 1 2 3; do
    if /usr/bin/snmpget -v2c -c m149public -On -t 2 -r 0 \
         127.0.0.1:15491 .1.3.6.1.2.1.1.3.0 > "$work/snmp/$mode.get.out" 2>&1 &&
       "$busy" grep -Fq 'Timeticks' "$work/snmp/$mode.get.out" &&
       /usr/bin/snmpwalk -v2c -c m149public -On -t 2 -r 0 \
         127.0.0.1:15491 .1.3.6.1.2.1.1 > "$work/snmp/$mode.walk.out" 2>&1 &&
       "$busy" grep -Fq '.1.3.6.1.2.1.1.3.0' "$work/snmp/$mode.walk.out"; then
      snmp_ok=true; break
    fi
    "$busy" sleep 1
  done
  kill "$snmp_pid" 2>/dev/null || :
  wait "$snmp_pid" 2>/dev/null || :
  if [ "$snmp_ok" = true ]; then
    echo "[M149 SNMP MODE] $mode real UDP get+walk"; break
  fi
done
if [ "$snmp_ok" = true ]; then
  echo M149_REAL_SNMP_UDP_AGENT_PASS
else
  echo M149_REAL_SNMP_UDP_AGENT_FAIL
  protocol_failures=$((protocol_failures+1))
  for mode in minimal-loopback minimal-wildcard default-wildcard; do
    echo "[M149 SNMP DIAGNOSTIC] $mode"
    "$busy" cat "$work/snmp/$mode.daemon.log" "$work/snmp/$mode.get.out" \
                 "$work/snmp/$mode.walk.out" || :
  done
fi
"$busy" sleep 2

# Protocol 2: signed Tinyproxy genuinely forwards signed Curl's HTTP request
# from guest loopback to an *independent host* endpoint via VirtIO-Net usernet.
# The host oracle's request counter must independently corroborate this marker.
"$busy" mkdir -p "$work/proxy"
# Distinguish a broad Curl/VirtIO path regression from a Tinyproxy-specific
# read/worker problem.  A direct GET is diagnostic only and does not satisfy
# the proxy's separate real-wire PASS condition.
if /usr/bin/curl --noproxy '*' -fsS --connect-timeout 4 --max-time 10 \
     http://10.0.2.2:15449/direct > "$work/proxy/direct.reply" \
     2> "$work/proxy/direct.err" &&
   "$busy" grep -Fxq M149_REAL_DIRECT_HOST_PAYLOAD "$work/proxy/direct.reply"; then
  echo '[M149 DIAGNOSTIC] direct Curl/VirtIO host GET PASS'
else
  echo '[M149 DIAGNOSTIC] direct Curl/VirtIO host GET FAIL'
  "$busy" cat "$work/proxy/direct.err" || :
fi
"$busy" cat > "$work/proxy/tinyproxy.conf" <<EOF
User root
Group root
Port 15492
Listen 127.0.0.1
Timeout 20
MaxClients 8
LogFile "$work/proxy/access.log"
PidFile "$work/proxy/tinyproxy.pid"
Allow 127.0.0.1
DisableViaHeader Yes
EOF
/usr/bin/tinyproxy -d -c "$work/proxy/tinyproxy.conf" \
  > "$work/proxy/server.log" 2>&1 &
proxy_pid=$!
proxy_ok=false
for mode in http10 http11; do
  case "$mode" in
    http10) http_option=--http1.0 ;;
    http11) http_option=--http1.1 ;;
  esac
  if /usr/bin/curl "$http_option" --proxy http://127.0.0.1:15492 --noproxy '' \
       --trace-ascii "$work/proxy/$mode.curl.trace" \
       -fsS --connect-timeout 4 --max-time 10 \
       http://10.0.2.2:15449/proxy > "$work/proxy/$mode.reply" \
       2> "$work/proxy/$mode.curl.err" &&
     "$busy" grep -Fxq M149_REAL_TINYPROXY_HOST_PAYLOAD "$work/proxy/$mode.reply"; then
    proxy_ok=true
    echo "[M149 PROXY CURL MODE] $mode real host response"
    break
  fi
  "$busy" sleep 1
done
# Independent original Alpine Ncat transmits a literal HTTP/1.0 absolute-URI
# proxy request.  This does not substitute for the Curl gate above; it helps
# separate client sendmsg/writev behavior from Tinyproxy read/worker behavior.
raw_ok=false
for attempt in 1 2; do
  if "$busy" printf 'GET http://10.0.2.2:15449/proxy HTTP/1.0\r\nHost: 10.0.2.2:15449\r\nConnection: close\r\n\r\n' |
     /usr/bin/ncat -w 8 127.0.0.1 15492 > "$work/proxy/ncat.reply" \
       2> "$work/proxy/ncat.err" &&
     "$busy" grep -Fq M149_REAL_TINYPROXY_HOST_PAYLOAD "$work/proxy/ncat.reply"; then
    raw_ok=true; break
  fi
  "$busy" sleep 1
done
[ "$raw_ok" = true ] && echo '[M149 DIAGNOSTIC] Ncat raw HTTP proxy PASS' || \
  echo '[M149 DIAGNOSTIC] Ncat raw HTTP proxy FAIL'
kill "$proxy_pid" 2>/dev/null || :
wait "$proxy_pid" 2>/dev/null || :
if [ "$proxy_ok" = true ]; then
  echo M149_REAL_TINYPROXY_HTTP_FORWARD_PASS
else
  echo M149_REAL_TINYPROXY_HTTP_FORWARD_FAIL
  protocol_failures=$((protocol_failures+1))
  "$busy" cat "$work/proxy/server.log" "$work/proxy/access.log" \
               "$work/proxy/http10.curl.err" "$work/proxy/http10.curl.trace" \
               "$work/proxy/http11.curl.err" "$work/proxy/http11.curl.trace" \
               "$work/proxy/ncat.err" "$work/proxy/ncat.reply" || :
fi
echo "[M149 SUMMARY] elf_count=$count elf_failures=$failed protocol_failures=$protocol_failures"
[ "$count" -eq 35 ] && [ "$failed" -eq 0 ] && [ "$protocol_failures" -eq 0 ] || exit 1
echo M149_NETWORK_COHORT_PASS

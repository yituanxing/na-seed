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
    usr/sbin/snmp*|usr/bin/snmp*) option=-V ;;
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
"$busy" cat > "$work/snmp/snmpd.conf" <<EOF
agentAddress udp:127.0.0.1:15491
rocommunity m149public 127.0.0.1
sysLocation NaSeed-M149
sysContact Kernel-Port
EOF
/usr/sbin/snmpd -f -Lo -C -c "$work/snmp/snmpd.conf" \
  > "$work/snmp/daemon.log" 2>&1 &
snmp_pid=$!
snmp_ok=false
for attempt in 1 2 3 4 5; do
  if /usr/bin/snmpget -v2c -c m149public -On -t 2 -r 0 \
       127.0.0.1:15491 .1.3.6.1.2.1.1.3.0 > "$work/snmp/get.out" 2>&1 &&
     "$busy" grep -Fq 'Timeticks' "$work/snmp/get.out" &&
     /usr/bin/snmpwalk -v2c -c m149public -On -t 2 -r 0 \
       127.0.0.1:15491 .1.3.6.1.2.1.1 > "$work/snmp/walk.out" 2>&1 &&
     "$busy" grep -Fq '.1.3.6.1.2.1.1.3.0' "$work/snmp/walk.out"; then
    snmp_ok=true; break
  fi
  "$busy" sleep 1
done
kill "$snmp_pid" 2>/dev/null || :
wait "$snmp_pid" 2>/dev/null || :
if [ "$snmp_ok" = true ]; then
  echo M149_REAL_SNMP_UDP_AGENT_PASS
else
  echo M149_REAL_SNMP_UDP_AGENT_FAIL
  protocol_failures=$((protocol_failures+1))
  "$busy" cat "$work/snmp/daemon.log" "$work/snmp/get.out" "$work/snmp/walk.out" || :
fi
"$busy" sleep 2

# Protocol 2: signed Tinyproxy genuinely forwards signed Curl's HTTP request
# from guest loopback to an *independent host* endpoint via VirtIO-Net usernet.
# The host oracle's request counter must independently corroborate this marker.
"$busy" mkdir -p "$work/proxy"
"$busy" cat > "$work/proxy/tinyproxy.conf" <<EOF
User root
Group root
Port 15492
Listen 127.0.0.1
Timeout 20
MaxClients 8
StartServers 1
MinSpareServers 1
MaxSpareServers 2
LogFile "$work/proxy/access.log"
PidFile "$work/proxy/tinyproxy.pid"
Allow 127.0.0.1
DisableViaHeader Yes
EOF
/usr/bin/tinyproxy -d -c "$work/proxy/tinyproxy.conf" \
  > "$work/proxy/server.log" 2>&1 &
proxy_pid=$!
proxy_ok=false
for attempt in 1 2 3 4 5; do
  if /usr/bin/curl --proxy http://127.0.0.1:15492 --noproxy '' \
       -fsS --connect-timeout 4 --max-time 10 \
       http://10.0.2.2:15449/proxy > "$work/proxy/reply" 2> "$work/proxy/curl.err" &&
     "$busy" grep -Fxq M149_REAL_TINYPROXY_HOST_PAYLOAD "$work/proxy/reply"; then
    proxy_ok=true; break
  fi
  "$busy" sleep 1
done
kill "$proxy_pid" 2>/dev/null || :
wait "$proxy_pid" 2>/dev/null || :
if [ "$proxy_ok" = true ]; then
  echo M149_REAL_TINYPROXY_HTTP_FORWARD_PASS
else
  echo M149_REAL_TINYPROXY_HTTP_FORWARD_FAIL
  protocol_failures=$((protocol_failures+1))
  "$busy" cat "$work/proxy/server.log" "$work/proxy/access.log" \
               "$work/proxy/curl.err" "$work/proxy/reply" || :
fi
echo "[M149 SUMMARY] elf_count=$count elf_failures=$failed protocol_failures=$protocol_failures"
[ "$count" -eq 35 ] && [ "$failed" -eq 0 ] && [ "$protocol_failures" -eq 0 ] || exit 1
echo M149_NETWORK_COHORT_PASS

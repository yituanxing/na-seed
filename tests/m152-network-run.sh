#!/bin/sh
# M152: 35 original Alpine ELFs, real DNS forward and SMTP delivery on wire.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
busy=/bin/busybox
work=/tmp/m152-network
"$busy" mkdir -p "$work"

# Smoke tests enforce native exec coverage but are NEVER counted as protocol
# functionality. The host additionally checks kernel [EXEC] traces for each.
count=0
failed=0
while IFS= read -r program; do
  case "$program" in ''|'#'*) continue ;; esac
  count=$((count+1))
  case "$program" in
    usr/bin/ldns-version) option=none ;;
    usr/sbin/rsyslogd) option=-v ;;
    usr/sbin/syslog-ng|usr/sbin/syslog-ng-ctl|usr/bin/loggen|usr/bin/pdbtool)
      option=--version ;;
    usr/sbin/conntrackd|usr/sbin/conntrack|usr/sbin/nfct)
      option=-V ;;
    usr/bin/msmtp|usr/bin/msmtpd) option=--version ;;
    *) option=-h ;;
  esac
  rc=0
  if [ "$option" = none ]; then
    "$busy" timeout 6 "/$program" > "$work/load-$count.log" 2>&1 || rc=$?
  else
    "$busy" timeout 6 "/$program" "$option" > "$work/load-$count.log" 2>&1 || rc=$?
  fi
  case "$rc" in
    124|125|126|127|132|133|134|135|136|137|138|139|140|141|142|143)
      echo "[M152 LOAD FAIL] /$program exit=$rc"
      failed=$((failed+1)) ;;
  esac
  if ! test -s "$work/load-$count.log"; then
    echo "[M152 LOAD FAIL] /$program empty startup/help output"
    failed=$((failed+1))
  fi
done < /tests/m152-original-elfs.txt
echo "[M152 LOAD SUMMARY] count=$count failures=$failed"
if [ "$count" -eq 35 ] && [ "$failed" -eq 0 ]; then
  echo M152_35_REAL_ELF_LOAD_PASS
else
  echo M152_35_REAL_ELF_LOAD_FAIL
fi
semantic_failures=0

# Functional (non-network) LDNS zone parsing with real DNS resource records.
"$busy" cat > "$work/m152.zone" <<'EOF'
m152.example.test. 60 IN SOA ns.m152.example.test. admin.m152.example.test. 1 60 60 3600 60
m152.example.test. 60 IN NS ns.m152.example.test.
ns.m152.example.test. 60 IN A 192.0.2.152
www.m152.example.test. 60 IN A 192.0.2.153
EOF
if /usr/bin/ldns-read-zone "$work/m152.zone" > "$work/ldns-zone.out" 2> "$work/ldns-zone.err" &&
   "$busy" grep -Fq '192.0.2.153' "$work/ldns-zone.out"; then
  echo M152_REAL_LDNS_ZONE_PARSE_PASS
else
  echo M152_REAL_LDNS_ZONE_PARSE_FAIL
  semantic_failures=$((semantic_failures+1))
  "$busy" cat "$work/ldns-zone.err" "$work/ldns-zone.out" || :
fi

# Protocol 1: ORIGINAL dnsmasq server forwards an ORIGINAL drill client A
# request to a separately running HOST UDP authoritative oracle at 10.0.2.2.
# Require the exact returned A record, not merely a connected socket.
"$busy" mkdir -p "$work/dns"
/usr/sbin/dnsmasq --no-daemon --user=root --group=root \
  --no-hosts --no-resolv --port=15454 --listen-address=127.0.0.1 \
  --bind-interfaces '--server=10.0.2.2#15453' --log-queries \
  > "$work/dns/dnsmasq.log" 2>&1 &
dns_pid=$!
dns_ok=false
for attempt in 1 2 3; do
  if "$busy" timeout 10 /usr/bin/drill -p 15454 \
       m152.example.test @127.0.0.1 A \
       > "$work/dns/drill.out" 2> "$work/dns/drill.err" &&
     "$busy" grep -Fq '198.51.100.152' "$work/dns/drill.out"; then
    dns_ok=true
    break
  fi
  "$busy" sleep 1
done
kill "$dns_pid" 2>/dev/null || :
wait "$dns_pid" 2>/dev/null || :
if [ "$dns_ok" = true ]; then
  echo M152_REAL_DNS_FORWARD_PASS
else
  echo M152_REAL_DNS_FORWARD_FAIL
  semantic_failures=$((semantic_failures+1))
  "$busy" cat "$work/dns/dnsmasq.log" "$work/dns/drill.err" "$work/dns/drill.out" || :
fi

# Protocol 2: ORIGINAL msmtp completes an SMTP envelope and DATA transaction
# with an independent HOST service that verifies the exact email payload.
"$busy" mkdir -p "$work/smtp"
smtp_ok=false
for attempt in 1 2; do
  if "$busy" printf 'From: m152-sender@example.test\r\nTo: m152-recipient@example.test\r\nSubject: M152-SMTP-WIRE\r\n\r\nM152 real original Alpine SMTP client payload\r\n' |
     "$busy" timeout 16 /usr/bin/msmtp \
       --host=10.0.2.2 --port=15425 --tls=off --auth=off \
       --from=m152-sender@example.test m152-recipient@example.test \
       > "$work/smtp/msmtp.out" 2> "$work/smtp/msmtp.err"; then
    smtp_ok=true
    break
  fi
  "$busy" sleep 1
done
if [ "$smtp_ok" = true ]; then
  echo M152_REAL_SMTP_HOST_DELIVERY_PASS
else
  echo M152_REAL_SMTP_HOST_DELIVERY_FAIL
  semantic_failures=$((semantic_failures+1))
  "$busy" cat "$work/smtp/msmtp.err" "$work/smtp/msmtp.out" || :
fi

# Extra real diagnostic queries; report actual kernel behavior without
# substituting their output for the two independent wire-protocol gates.
if /sbin/ifconfig -a > "$work/ifconfig.out" 2> "$work/ifconfig.err" &&
   "$busy" grep -Fq 'lo' "$work/ifconfig.out"; then
  echo '[M152 DIAGNOSTIC] original ifconfig interface enumeration PASS'
else
  echo '[M152 DIAGNOSTIC] original ifconfig interface enumeration unsupported'
  "$busy" cat "$work/ifconfig.err" || :
fi
if /bin/netstat -an > "$work/netstat.out" 2> "$work/netstat.err"; then
  echo '[M152 DIAGNOSTIC] original netstat socket enumeration PASS'
else
  echo '[M152 DIAGNOSTIC] original netstat socket enumeration unsupported'
  "$busy" cat "$work/netstat.err" || :
fi

echo "[M152 SUMMARY] elf_count=$count elf_failures=$failed semantic_failures=$semantic_failures"
[ "$count" -eq 35 ] && [ "$failed" -eq 0 ] && [ "$semantic_failures" -eq 0 ] || exit 1
echo M152_NETWORK_COHORT_PASS

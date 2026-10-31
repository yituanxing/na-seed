#!/bin/sh
# M141 genuine Alpine BIND9 tools: independent, offline semantic probes.
# Called once per case by the M141 guest batch runner. No PASS token is
# printed here; the caller emits a token only after this script exits zero.
set -eu

[ "$#" -eq 1 ] || exit 2
case_id=$1
busy=/bin/busybox
work="/tmp/m141-dns-$case_id"
"$busy" mkdir -p "$work"

zone_fixture() {
    "$busy" printf '%s\n' \
        '$TTL 3600' \
        '@ IN SOA ns.example.test. hostmaster.example.test. 2026103101 3600 900 604800 60' \
        '@ IN NS ns.example.test.' \
        'ns IN A 192.0.2.53' \
        'www IN A 192.0.2.42' \
        'mail IN A 192.0.2.25' \
        '@ IN MX 10 mail.example.test.' > "$work/example.zone"
}

case "$case_id" in
    arpa_ipv4)
        /usr/bin/arpaname 192.0.2.42 > "$work/output"
        "$busy" grep -iFq '42.2.0.192.in-addr.arpa' "$work/output"
        ;;
    arpa_ipv6)
        /usr/bin/arpaname 2001:db8::1 > "$work/output"
        "$busy" grep -iFq '8.b.d.0.1.0.0.2.ip6.arpa' "$work/output"
        ;;
    zone_valid)
        zone_fixture
        /usr/bin/named-checkzone example.test "$work/example.zone" > "$work/output" 2>&1
        "$busy" grep -Fq 'loaded serial 2026103101' "$work/output"
        "$busy" grep -Fq 'OK' "$work/output"
        ;;
    zone_invalid)
        zone_fixture
        "$busy" printf '%s\n' 'www IN CNAME other.example.test.' >> "$work/example.zone"
        if /usr/bin/named-checkzone example.test "$work/example.zone" > "$work/output" 2>&1; then
            echo '[M141 DNS ERROR] invalid CNAME coexistence accepted' >&2
            exit 1
        fi
        test -s "$work/output"
        ;;
    zone_roundtrip)
        zone_fixture
        /usr/bin/named-compilezone -F text -o "$work/compiled.zone" \
            example.test "$work/example.zone" > "$work/compile.log" 2>&1
        test -s "$work/compiled.zone"
        "$busy" grep -Fq '192.0.2.42' "$work/compiled.zone"
        /usr/bin/named-checkzone example.test "$work/compiled.zone" > "$work/recheck.log" 2>&1
        "$busy" grep -Fq 'OK' "$work/recheck.log"
        ;;
    rr_a)
        "$busy" printf '%s\n' 'IN A 192.0.2.42' |
            /usr/bin/named-rrchecker -p > "$work/output"
        "$busy" grep -Fq '192.0.2.42' "$work/output"
        ;;
    rr_relative_cname)
        "$busy" printf '%s\n' 'IN CNAME target' |
            /usr/bin/named-rrchecker -p -o example.test > "$work/output"
        "$busy" grep -Fq 'target.example.test.' "$work/output"
        ;;
    rr_invalid)
        if "$busy" printf '%s\n' 'IN A 999.999.999.999' |
            /usr/bin/named-rrchecker > "$work/output" 2>&1; then
            echo '[M141 DNS ERROR] malformed IPv4 RDATA accepted' >&2
            exit 1
        fi
        ;;
    tsig_key)
        /usr/sbin/tsig-keygen -a hmac-sha256 m141-tsig > "$work/key.conf"
        "$busy" grep -Fq 'm141-tsig' "$work/key.conf"
        "$busy" grep -Fq 'hmac-sha256' "$work/key.conf"
        "$busy" grep -Fq 'secret' "$work/key.conf"
        ;;
    rndc_key)
        /usr/sbin/rndc-confgen -A hmac-sha256 -b 128 -k m141-rndc > "$work/rndc.conf"
        "$busy" grep -Fq 'm141-rndc' "$work/rndc.conf"
        "$busy" grep -Fq 'secret' "$work/rndc.conf"
        "$busy" grep -Fq 'hmac-sha256' "$work/rndc.conf"
        ;;
    *)
        echo "[M141 DNS ERROR] unknown case: $case_id" >&2
        exit 2
        ;;
esac

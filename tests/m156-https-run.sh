#!/bin/sh
# TLS is checked by the original Alpine clients against a real CA file and
# matching server SAN. A plaintext HTTP response cannot pass this test.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
work=/tmp/m156-https
ca=/tests/m156-https-ca.pem
/bin/busybox mkdir -p "$work"
test -s "$ca"

# QEMU slirp makes the independent host server available at 10.0.2.2.
# --resolve bypasses guest DNS for this fixed test name, but still checks the
# certificate's DNS SAN and complete TLS chain during the original curl call.
curl_ready=false
for attempt in 1 2 3 4 5 6 7 8; do
    if /usr/bin/curl --noproxy '*' --fail --silent --show-error \
        --cacert "$ca" --resolve m156.naseed.test:15443:10.0.2.2 \
        --connect-timeout 8 --max-time 15 \
        -o "$work/curl.txt" https://m156.naseed.test:15443/curl; then
        curl_ready=true
        break
    fi
    /bin/busybox printf '[M156 HTTPS DHCP WAIT] curl attempt=%s\n' "$attempt"
    /bin/busybox sleep 1
done
[ "$curl_ready" = true ]
/bin/busybox grep -Fxq M156_REAL_HTTPS_CURL_PAYLOAD "$work/curl.txt"
echo M156_REAL_CURL_VERIFIED_TLS_PASS

# The certificate also has an IP-address SAN. GNU wget must verify it
# independently, with no --no-check-certificate bypass.
/usr/bin/wget --quiet --tries=3 --timeout=12 --ca-certificate="$ca" \
    --output-document="$work/wget.txt" https://10.0.2.2:15443/wget
/bin/busybox grep -Fxq M156_REAL_HTTPS_WGET_PAYLOAD "$work/wget.txt"
echo M156_REAL_WGET_VERIFIED_TLS_PASS

echo M156_HTTPS_WIRE_PASS

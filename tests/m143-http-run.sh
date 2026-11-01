#!/bin/sh
# Original Wget/aria2c download over real VirtIO and lighttpd loopback server.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
LC_ALL=C
export PATH LC_ALL
work=/tmp/m143-http
/bin/busybox mkdir -p "$work/site"
# Stable byte-level payloads are checked against independent host HTTP logs.
for pair in wget:WGET aria2:ARIA2 curl:CURL; do
    name=${pair%%:*}
    tag=${pair#*:}
    case "$name" in
        wget)
            # DHCP ACK can precede ACD/ARP completion; retry only this
            # initial network-ready transition and keep the exact host oracle.
            ready=false
            for attempt in 1 2 3 4 5 6 7 8 9 10; do
                if /usr/bin/wget -q -t 1 -T 6 -O "$work/wget.txt" http://10.0.2.2:15431/wget; then
                    ready=true
                    break
                fi
                /bin/busybox printf '[M143 DHCP WAIT] wget attempt=%s\\n' "$attempt"
                /bin/busybox sleep 1
            done
            [ "$ready" = true ]
            ;;
        aria2)
            /usr/bin/aria2c --quiet=true --disable-ipv6=true --max-tries=3 \
                --connect-timeout=8 --timeout=12 --dir="$work" --out=aria2.txt \
                --allow-overwrite=true http://10.0.2.2:15431/aria2
            ;;
        curl)
            /usr/bin/curl --noproxy '*' --fail --silent --show-error \
                --retry 3 --retry-delay 1 -o "$work/curl.txt" \
                http://10.0.2.2:15431/curl
            ;;
    esac
    /bin/busybox grep -Fxq "M143_REAL_HTTP_${tag}_PAYLOAD" "$work/$name.txt"
    /bin/busybox printf 'M143_REAL_HTTP_%s_DOWNLOAD_PASS\n' "$tag"
done

# Reuse previously signed OpenSSL executable, but exercise its real digest API.
/usr/bin/openssl dgst -sha256 "$work/curl.txt" > "$work/openssl.digest"
/bin/busybox awk '{print $NF}' "$work/openssl.digest" > "$work/openssl.hash"
/bin/busybox sha256sum "$work/curl.txt" | /bin/busybox awk '{print $1}' > "$work/busybox.hash"
/bin/busybox cmp "$work/openssl.hash" "$work/busybox.hash"
echo M143_REAL_OPENSSL_SHA256_PASS

# Start an actual guest-native HTTP server, NOT a host-side stand-in.
/bin/busybox printf 'M143_REAL_LIGHTTPD_LOOPBACK_PAYLOAD\n' > "$work/site/index.txt"
/bin/busybox cat > "$work/lighttpd.conf" <<EOF
server.modules = ( "mod_staticfile" )
server.document-root = "$work/site"
server.bind = "127.0.0.1"
server.port = 15432
server.errorlog = "$work/lighttpd.err"
server.upload-dirs = ( "$work" )
mimetype.assign = ( ".txt" => "text/plain" )
EOF
/usr/sbin/lighttpd -tt -f "$work/lighttpd.conf"
/usr/sbin/lighttpd -D -f "$work/lighttpd.conf" > "$work/lighttpd.stdout" 2>&1 &
server_pid=$!
trap 'kill "$server_pid" 2>/dev/null || :; wait "$server_pid" 2>/dev/null || :' EXIT
/bin/busybox sleep 2
if ! kill -0 "$server_pid" 2>/dev/null; then
    echo '[M143 LIGHTTPD FAIL] real server exited before loopback HTTP'
    /bin/busybox cat "$work/lighttpd.stdout" || :
    /bin/busybox cat "$work/lighttpd.err" || :
    exit 1
fi
if ! /usr/bin/curl --noproxy '*' --fail --silent --show-error \
    --retry 3 --retry-delay 1 --retry-connrefused \
    -o "$work/lighttpd.out" http://127.0.0.1:15432/index.txt; then
    echo '[M143 LIGHTTPD FAIL] original curl could not GET guest server'
    /bin/busybox cat "$work/lighttpd.stdout" || :
    /bin/busybox cat "$work/lighttpd.err" || :
    exit 1
fi
/bin/busybox cmp "$work/site/index.txt" "$work/lighttpd.out"
echo M143_REAL_LIGHTTPD_LOOPBACK_PASS
echo M143_HTTP_COHORT_PASS

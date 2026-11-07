#!/bin/sh
# The signed native Alpine HTTP clients and server are the only accepted ELFs.
# Host-source bytes live OUTSIDE the guest; no content oracle is preseeded.
set -eu
[ "$#" -eq 3 ] && [ "$1" = --exercise ] || exit 2
case "${NAS_M223_BOOT_PHASE:-}" in first|reopen) ;; *) echo 'M223: missing genuine QEMU boot phase' >&2; exit 1 ;; esac
phase=$NAS_M223_BOOT_PHASE
case_id=$2 elf=$3
busy=/bin/busybox
test -x "/$elf"
magic=$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')
[ "$magic" = 7f454c46 ] || { echo 'M223: original ELF magic absent' >&2; exit 1; }
target=/root/m223-work/$case_id.$phase
test ! -e "$target" || { echo 'M223: stale phase workload target' >&2; exit 1; }
mkdir -p "$target"

host_download() {
    which=$1
    out="$target/download.bin"
    test ! -e "$out" || return 1
    attempt=0
    while [ "$attempt" -lt 8 ]; do
        attempt=$((attempt+1))
        if [ "$which" = wget ]; then
            if /usr/bin/wget -q -t 1 -T 8 -O "$out" \
                    http://10.0.2.2:15431/wget > "$target/client.log" 2>&1; then
                test -s "$out" && return 0
            fi
        else
            if /usr/bin/aria2c --quiet=true --disable-ipv6=true \
                    --max-tries=1 --connect-timeout=8 --timeout=12 \
                    --file-allocation=none --dir="$target" --out=download.bin \
                    --allow-overwrite=true http://10.0.2.2:15431/aria2 \
                    > "$target/client.log" 2>&1; then
                test -s "$out" && return 0
            fi
        fi
        $busy sleep 2
    done
    echo "M223 signed $which failed to fetch host HTTP bytes" >&2
    $busy tail -n 4 "$target/client.log" >&2 || :
    return 1
}

loopback_lighttpd() {
    test -x /usr/bin/wget && apk info -e wget >/dev/null 2>&1 || return 1
    test "$( $busy od -An -tx1 -N4 /usr/bin/wget | $busy tr -d ' \t\r\n' )" = 7f454c46 || return 1
    # Historical M143 RTL/QEMU observed asynchronous Netconn release and an
    # eight-slot socket limit after two host HTTP connections. Reuse its
    # precise 3-second idle gap before the actual signed server accepts.
    $busy sleep 3
    mkdir -p "$target/www" "$target/tmp"
    printf 'M223_LIGHTTPD_LOCAL_%s_HTTP_GET\n' "$phase" > "$target/www/index.txt"
    config="$target/lighttpd.conf"
    {
        printf '%s\n' 'server.modules = ( "mod_staticfile" )'
        printf 'server.document-root = "%s"\n' "$target/www"
        printf '%s\n' 'server.bind = "127.0.0.1"' 'server.port = 15432'
        printf 'server.errorlog = "%s"\n' "$target/lighttpd.error"
        printf 'server.upload-dirs = ( "%s" )\n' "$target/tmp"
        printf '%s\n' 'mimetype.assign = ( ".txt" => "text/plain" )'
    } > "$config"
    /usr/sbin/lighttpd -D -f "$config" > "$target/lighttpd.log" 2>&1 &
    pid=$!
    result=1
    attempt=0
    while [ "$attempt" -lt 10 ]; do
        attempt=$((attempt+1))
        if ! $busy kill -0 "$pid" 2>/dev/null; then
            echo 'M223 genuine lighttpd exited before loopback GET' >&2
            break
        fi
        if /usr/bin/wget -q -t 1 -T 3 -O "$target/loopback.txt" \
                http://127.0.0.1:15432/index.txt > "$target/wget-loopback.log" 2>&1 &&
                $busy cmp -s "$target/www/index.txt" "$target/loopback.txt"; then
            result=0
            break
        fi
        $busy sleep 1
    done
    $busy kill "$pid" 2>/dev/null || :
    wait "$pid" 2>/dev/null || :
    if $busy kill -0 "$pid" 2>/dev/null; then
        echo 'M223 lighttpd did not terminate' >&2
        return 1
    fi
    if [ "$result" -ne 0 ]; then
        echo 'M223 original lighttpd loopback HTTP GET mismatch' >&2
        $busy tail -n 4 "$target/lighttpd.log" >&2 || :
    fi
    return "$result"
}

case "$case_id:$elf" in
    real-host-wget-download:usr/bin/wget) host_download wget ;;
    real-host-aria2-download:usr/bin/aria2c) host_download aria2 ;;
    real-loopback-lighttpd:usr/sbin/lighttpd) loopback_lighttpd ;;
    *) echo "M223 unknown signed HTTP semantic probe $case_id:$elf" >&2; exit 2 ;;
esac

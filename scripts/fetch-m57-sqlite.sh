#!/bin/sh
# One owner for the official SQLite 3.54.1 AUTOCONF source archive. M57
# and later original-source guest workloads consume these exact bytes;
# neither compiler flags nor a NaSeed-specific source patch enter this step.
set -eu
[ "$#" -eq 0 ] || { echo 'usage: fetch-m57-sqlite.sh' >&2; exit 2; }
version=3540100
expected_sha3=b6ced585c750916b1052667239a12c25a7ff34e7f06e1e6e01ecb0427657d9c4
archive="build/deps/m57-sqlite/sqlite-autoconf-$version.tar.gz"
mkdir -p "$(dirname "$archive")"
if [ ! -s "$archive" ]; then
    # M57 and M84 (and different target ports) may fetch in parallel.
    # Publish only a checksum-verified complete archive, atomically.
    temporary="$(mktemp "${archive}.tmp.XXXXXX")"
    trap 'rm -f "$temporary"' EXIT HUP INT TERM
    for mirror in www.sqlite.org www2.sqlite.org www3.sqlite.org; do
        if curl -fLsS --retry 2 --connect-timeout 10 --max-time 120 \
                -o "$temporary" \
                "https://$mirror/2026/sqlite-autoconf-$version.tar.gz" &&
           [ "$(openssl dgst -sha3-256 "$temporary" | awk '{print $NF}')" = "$expected_sha3" ]; then
            mv "$temporary" "$archive"
            trap - EXIT HUP INT TERM
            echo "[M57 SOURCE] official SQLite SHA3-256 verified via $mirror" >&2
            break
        fi
    done
    test -s "$archive" || {
        echo '[M57 FAIL] official pinned SQLite archive unavailable' >&2
        exit 1
    }
fi
[ "$(openssl dgst -sha3-256 "$archive" | awk '{print $NF}')" = "$expected_sha3" ] || {
    echo '[M57 FAIL] official SQLite SHA3-256 mismatch' >&2
    exit 1
}
printf '%s\n' "$archive"

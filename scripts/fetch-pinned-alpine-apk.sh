#!/bin/sh
# Alpine release fixtures: package VERSION and SHA256 are pinned upstream of
# this call. Alternate official mirrors are mere transports, never trust roots.
set -eu
[ "$#" -eq 4 ] || { echo 'usage: fetch-pinned-alpine-apk.sh ARCH NAME SHA256 OUTPUT' >&2; exit 2; }
arch=$1 name=$2 expected=$3 dest=$4
case "$arch" in x86_64|aarch64|riscv64) ;; *) exit 2 ;; esac
case "$name" in *[!a-zA-Z0-9._+-]*|'') exit 2 ;; esac
case "$expected" in *[!0-9a-f]*|'') exit 2 ;; esac
[ "${#expected}" -eq 64 ] || exit 2
mkdir -p "$(dirname "$dest")"
if [ -s "$dest" ] && printf '%s  %s\n' "$expected" "$dest" | sha256sum -c - >/dev/null 2>&1; then
    echo "[ALPINE CACHE] authenticated $arch $name"
    exit 0
fi
# mirrors.alpinelinux.org lists these official/registered v3.23 transports.
# Override only to inject offline fixtures in the self-contained CI test.
# Alpine v3.23 mirrors do not all retain superseded musl revisions.
# Keep verified historical transports FIRST: the official rolling CDN now
# serves musl-r24 whereas our tested runtime pin is musl-r23.
mirrors=${NAS_ALPINE_MIRRORS:-'https://ftp.lysator.liu.se/pub/alpine https://download.nus.edu.sg/mirror/alpine https://mirrors.aliyun.com/alpine https://dl-cdn.alpinelinux.org/alpine https://mirror.math.princeton.edu/pub/alpinelinux'}
tmp="$dest.tmp.$$"
trap 'rm -f "$tmp"' EXIT
for mirror in $mirrors; do
    rm -f "$tmp"
    url="${mirror%/}/v3.23/main/$arch/$name.apk"
    if ! curl -fLsS --retry 2 --connect-timeout 15 --max-time 60 -o "$tmp" "$url"; then
        echo "[ALPINE MIRROR] unavailable: $url" >&2
        continue
    fi
    if ! printf '%s  %s\n' "$expected" "$tmp" | sha256sum -c - >/dev/null 2>&1; then
        echo "[ALPINE MIRROR] digest mismatch: $url" >&2
        continue
    fi
    mv -f "$tmp" "$dest"
    echo "[ALPINE FETCH] authenticated $arch $name"
    exit 0
done
echo "[ALPINE FETCH FAIL] no mirror supplied pinned bytes: $arch $name" >&2
exit 1

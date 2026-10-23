#!/bin/sh
set -eu
# No network required. Exercise a missing mirror, corrupt content, verified
# fallback, verified cache, tampered cached bytes and fail-closed behavior.
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
bad="$tmp/bad"
good="$tmp/good"
mkdir -p "$bad/v3.23/main/x86_64" "$good/v3.23/main/x86_64" "$tmp/out"
printf 'mismatch\n' > "$bad/v3.23/main/x86_64/busybox-1.37.0-r30.apk"
printf 'official pinned payload fixture\n' > "$good/v3.23/main/x86_64/busybox-1.37.0-r30.apk"
sum=$(sha256sum "$good/v3.23/main/x86_64/busybox-1.37.0-r30.apk" | cut -d ' ' -f 1)
name=busybox-1.37.0-r30
out="$tmp/out/$name.apk"
export NAS_ALPINE_MIRRORS="file://$tmp/missing file://$bad file://$good"
sh scripts/fetch-pinned-alpine-apk.sh x86_64 "$name" "$sum" "$out"
printf '%s  %s\n' "$sum" "$out" | sha256sum -c - >/dev/null
export NAS_ALPINE_MIRRORS="file://$tmp/missing"
sh scripts/fetch-pinned-alpine-apk.sh x86_64 "$name" "$sum" "$out"
printf 'stale content\n' > "$out"
if sh scripts/fetch-pinned-alpine-apk.sh x86_64 "$name" "$sum" "$out" >/dev/null 2>&1; then
    echo '[FAIL] stale digest accepted' >&2
    exit 1
fi
if printf '%s  %s\n' "$sum" "$out" | sha256sum -c - >/dev/null 2>&1; then
    echo '[FAIL] corrupted output became authenticated' >&2
    exit 1
fi
if sh scripts/fetch-pinned-alpine-apk.sh invalid_arch "$name" "$sum" "$out" >/dev/null 2>&1; then
    echo '[FAIL] invalid architecture accepted' >&2
    exit 1
fi
echo '[PASS] Alpine transport mirrors, digest cache and failure isolation'

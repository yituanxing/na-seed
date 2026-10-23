#!/bin/sh
# Unmodified official Alpine v3.23 x86_64 busybox + musl runtime.
# APK archives can contain concatenated gzip'd tar records.
set -eu
out="$1"
base='https://dl-cdn.alpinelinux.org/alpine/v3.23/main/x86_64'
root="$out/alpine"
mkdir -p "$root/packages" "$root/unpack" "$root/bin" "$root/lib"
for entry in busybox-1.37.0-r30 musl-1.2.5-r23; do
    apk="$root/packages/$entry.apk"
    case "$entry" in
      busybox*) expected='2fea8c73710994099eaec6832b69a0f70163e0d6ee13848af3a5270a16d15c5c' ;;
      musl*) expected='4f3c4a7bf9f51d2c91007e333b17459362ffd881b4a343e8da07b6c50c4f4a0d' ;;
    esac
    # CDN HTTP 404 must not silently alter pinned package provenance.
    sh scripts/fetch-pinned-alpine-apk.sh x86_64 "$entry" "$expected" "$apk"
    printf '%s  %s\n' "$expected" "$apk" | sha256sum -c -
    echo "[ALPINE] verified upstream package $entry"
    case "$entry" in
        busybox*) unpack="$root/unpack/busybox" ;;
        musl*) unpack="$root/unpack/musl" ;;
    esac
    mkdir -p "$unpack"
    # APK v2 may include a signature tar member before the actual content;
    # ignore tar EOF blocks between concatenated members.
    gzip -dc "$apk" | tar -xf - --ignore-zeros -C "$unpack"
done
if [ ! -f "$root/unpack/busybox/bin/busybox" ]; then
    echo "[ALPINE] missing upstream /bin/busybox ELF" >&2
    exit 1
fi
if [ ! -e "$root/unpack/musl/lib/ld-musl-x86_64.so.1" ]; then
    echo "[ALPINE] missing upstream musl interpreter" >&2
    exit 1
fi
cp "$root/unpack/busybox/bin/busybox" "$root/bin/busybox"
# cp -L resolves the official symlink into the actual shared object.
cp -L "$root/unpack/musl/lib/ld-musl-x86_64.so.1" "$root/lib/ld-musl-x86_64.so.1"
chmod +x "$root/bin/busybox" "$root/lib/ld-musl-x86_64.so.1"
echo "[ALPINE] PROGRAM ELF:"
readelf -h "$root/bin/busybox" | grep -E 'Type:|Machine:'
readelf -l "$root/bin/busybox" | grep -A1 INTERP || {
    echo "[ALPINE] expected real PT_INTERP missing" >&2; exit 1;
}
echo "[ALPINE] INTERPRETER ELF:"
readelf -h "$root/lib/ld-musl-x86_64.so.1" | grep -E 'Type:|Machine:'
echo "[ALPINE] host dynamic BusyBox smoke"
output=$("$root/lib/ld-musl-x86_64.so.1" --library-path "$root/lib" \
    "$root/bin/busybox" echo hello)
test "$output" = hello
echo "[ALPINE] PASS real musl dynamic loader + BusyBox on Linux host"

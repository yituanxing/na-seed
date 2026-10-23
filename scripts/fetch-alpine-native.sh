#!/bin/sh
# Real Alpine Linux v3.23 native ET_DYN BusyBox + native musl loader.
# All four official package archives must match checked-in SHA256 before use.
# The downloaded binaries are NEVER edited or rebuilt.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: fetch-alpine-native.sh arm64|riscv64 OUTPUT' >&2; exit 2; }
port=$1 out=$2
case "$port" in arm64) apk_arch=aarch64 ;; riscv64) apk_arch=riscv64 ;; *) exit 2 ;; esac
base="https://dl-cdn.alpinelinux.org/alpine/v3.23/main/$apk_arch"
mkdir -p "$out/packages" "$out/unpack"
for name in busybox-1.37.0-r30 musl-1.2.5-r23; do
    pkg="$out/packages/$name.apk"
    required=$(awk -F '|' -v a="$apk_arch" -v p="$name" \
        '$1==a && $2==p {print $3}' scripts/alpine-native-fixtures.txt)
    case "$required" in
        ''|*'
'*) echo "[ALPINE FIXTURE FAIL] missing/duplicate digest: $apk_arch $name" >&2; exit 1 ;;
        '-')
            echo '[ALPINE FIXTURE FAIL] unpinned Alpine package checksum' >&2
            exit 1 ;;
        *)
            sh scripts/fetch-pinned-alpine-apk.sh "$apk_arch" "$name" "$required" "$pkg"
            sum=$(sha256sum "$pkg" | cut -d ' ' -f 1)
            printf '[ALPINE NATIVE SHA256] %s %s %s\n' "$apk_arch" "$name" "$sum"
            printf '%s  %s\n' "$required" "$pkg" | sha256sum -c - ;;

    esac
    case "$name" in busybox*) root="$out/unpack/busybox" ;; *) root="$out/unpack/musl" ;; esac
    mkdir -p "$root"
    gzip -dc "$pkg" | tar -xf - --ignore-zeros -C "$root"
done
bb="$out/unpack/busybox/bin/busybox"
loader="$out/unpack/musl/lib/ld-musl-$apk_arch.so.1"
test -s "$bb"
test -e "$loader"
mkdir -p "$out/bin" "$out/lib"
cp "$bb" "$out/bin/busybox"
cp -L "$loader" "$out/lib/ld-musl-$apk_arch.so.1"
chmod +x "$out/bin/busybox" "$out/lib/ld-musl-$apk_arch.so.1"
case "$apk_arch" in aarch64) machine=AArch64 ;; riscv64) machine='RISC-V' ;; esac
for elf in "$out/bin/busybox" "$out/lib/ld-musl-$apk_arch.so.1"; do
    readelf -h "$elf" | grep -Fq "$machine"
done
readelf -l "$out/bin/busybox" | grep -q INTERP || {
    echo '[ALPINE NATIVE FAIL] busybox unexpectedly static' >&2
    exit 1
}
readelf -l "$out/bin/busybox" | grep -Fq "/lib/ld-musl-$apk_arch.so.1" || {
    echo '[ALPINE NATIVE FAIL] unexpected interpreter ABI' >&2
    exit 1
}
echo "[ALPINE NATIVE] official $apk_arch BusyBox ET_DYN + musl ELF staged"

#!/bin/sh
# Reproducibly compile one upstream BusyBox source/config for ALL NaSeed ISAs.
# This profile is deliberately Linux ABI + target libc, NOT a new NaSeed ABI.
# Do not alter BusyBox source; fail closed on checksum/Kconfig/ELF mismatch.
set -eu
[ "$#" -eq 1 ] || { echo 'usage: build-source-busybox.sh amd64|riscv64|arm64' >&2; exit 2; }
case "$1" in
    amd64) machine='Advanced Micro Devices X86-64'; cross=''; emulator='' ;;
    riscv64) machine='RISC-V'; cross='riscv64-linux-gnu-'; emulator=qemu-riscv64 ;;
    arm64) machine='AArch64'; cross='aarch64-linux-gnu-'; emulator=qemu-aarch64 ;;
    *) echo "unsupported BusyBox target: $1" >&2; exit 2 ;;
esac
port=$1
cc="${cross}gcc"
command -v "$cc" >/dev/null 2>&1 || { echo "[SOURCE BUSYBOX] cross compiler missing: $cc" >&2; exit 2; }
command -v make >/dev/null 2>&1
command -v readelf >/dev/null 2>&1
if [ -n "$emulator" ]; then command -v "$emulator" >/dev/null 2>&1; fi
version=1.37.0
sha=3311dff32e746499f4df0d5df04d7eb396382d7e108bb9250e7b519b837043a4
tarball=build/deps/source-busybox/busybox-$version.tar.bz2
source_root=build/deps/source-busybox/busybox-$version
target=build/source-busybox/$port
obj="$target/obj"
binary="$target/busybox"
mkdir -p "$(dirname "$tarball")" "$target" "$obj"
if [ ! -s "$tarball" ]; then
    # Known official release, with independent content pin. No floating git tags.
    curl -fLsS --retry 3 --retry-delay 2 -o "$tarball.tmp" \
        "https://busybox.net/downloads/busybox-$version.tar.bz2"
    mv "$tarball.tmp" "$tarball"
fi
printf '%s  %s\n' "$sha" "$tarball" | sha256sum -c -
if [ ! -s "$source_root/Makefile" ]; then
    tar -xjf "$tarball" -C "$(dirname "$source_root")"
fi
test -s "$source_root/Makefile"
# O= is out-of-source. BusyBox requires this directory to preexist.
# Never patch upstream and never reuse Kconfig across architectures.
make -s -C "$source_root" O="$PWD/$obj" HOSTCC=cc CROSS_COMPILE="$cross" allnoconfig
# Kconfig may hide dependent symbols in allnoconfig; repeated oldconfig
# passes expose them. Every requested option MUST survive final resolution.
for pass in 1 2 3; do
    while IFS= read -r setting || [ -n "$setting" ]; do
        case "$setting" in ''|'#'*) continue ;; esac
        key=${setting%%=*}
        config="$obj/.config"
        if grep -Fxq "$setting" "$config"; then continue; fi
        if grep -Fqx "# $key is not set" "$config"; then
            sed -i "s|^# $key is not set$|$setting|" "$config"
        elif grep -q "^$key=" "$config"; then
            sed -i "s|^$key=.*$|$setting|" "$config"
        fi
    done < scripts/source-busybox.config
    yes '' | make -s -C "$source_root" O="$PWD/$obj" HOSTCC=cc CROSS_COMPILE="$cross" oldconfig >/dev/null
done
while IFS= read -r setting || [ -n "$setting" ]; do
    case "$setting" in ''|'#'*) continue ;; esac
    if ! grep -Fxq "$setting" "$obj/.config"; then
        echo "[SOURCE BUSYBOX] Kconfig rejected required feature $setting ($port)" >&2
        exit 1
    fi
done < scripts/source-busybox.config
cp "$obj/.config" "$target/verified.config"
make -s -j4 -C "$source_root" O="$PWD/$obj" HOSTCC=cc \
    CROSS_COMPILE="$cross" CC="$cc" busybox
cp "$obj/busybox" "$binary"
chmod 755 "$binary"
# This stage is statically linked so only the target native ELF is needed.
readelf -h "$binary" | grep -Fq "$machine"
readelf -h "$binary" | grep -q 'EXEC (Executable file)'
if readelf -l "$binary" | grep -q INTERP; then
    echo '[SOURCE BUSYBOX] unexpected dynamic ELF interpreter' >&2
    exit 1
fi
if [ "$port" = amd64 ]; then
    "$binary" --list > "$target/applets"
    test "$("$binary" echo source-hello)" = source-hello
else
    "$emulator" "$binary" --list > "$target/applets"
    test "$("$emulator" "$binary" echo source-hello)" = source-hello
fi
for applet in sh echo cat wc ls mkdir rm rmdir; do
    grep -Fxq "$applet" "$target/applets" || {
        echo "[SOURCE BUSYBOX] required applet $applet absent on $port" >&2
        exit 1
    }
done
# Retain evidence of exact source, options and final guest binary for diffing.
sha256sum "$binary" "$target/verified.config" > "$target/artifacts.sha256"
echo "[SOURCE BUSYBOX PASS] $port upstream=$version Linux-ABI static ELF, pinned source+config, Linux-user oracle"

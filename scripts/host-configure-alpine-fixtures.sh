#!/bin/sh
# Generate independent test fixtures on the Linux host, never in the guest.
set -eu
[ "$#" -eq 3 ] || exit 2
port=$1
root=$2
profile=$3
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$profile" in
    m119)
        command -v msgfmt >/dev/null
        mkdir -p "$root/tests"
        msgfmt -o "$root/tests/m119-catalog.mo" tests/m119-catalog.po
        test -s "$root/tests/m119-catalog.mo"
        ;;
    m120|m121|m122|m123|m124|m125|m126|m127|m128)
        case "$profile" in
            m126|m127|m128) [ "$root" = "build/external/$profile-batch/$port/rootfs" ] || exit 2 ;;
        esac
        if [ "$profile" = m126 ]; then test -d "$root"; mkdir -p "$root/tests"; fi
        if [ "$profile" = m123 ]; then command -v mke2fs >/dev/null; fi
        python3 "tests/$profile-make-fixtures.py" "$root/tests"
        case "$profile" in
            m120) set -- m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw ;;
            m121) set -- m121-original.pdf m121-invalid.pdf ;;
            m122) set -- m122-original.ppm m122-red.ppm m122-blue.ppm m122-black.pbm m122-white.pgm m122-flipped.ppm m122-cropped.ppm m122-inverted.ppm m122-red-channel.pgm m122-red8.ppm m122-red31.ppm m122-red-blue.ppm m122-original.png m122-invalid.ppm ;;
            m123) set -- m123-ext2.img m123-empty-ext.img m123-empty-fat.img ;;
            m124) set -- m124-empty-minix.img m124-empty-swap.img m124-cramfs-source/hello.txt m124-cramfs-source/nested/entry.txt m124-look-words.txt ;;
            m125) set -- m125-pattern.ppm m125-red.ppm m125-blue.ppm m125-magenta.ppm m125-black.ppm m125-cyan.ppm m125-checker.ppm m125-bw-checker.ppm m125-bw-checker.pgm m125-gray.pgm m125-gray-to-red.ppm m125-threshold.pbm m125-indices.pgm m125-lookup.ppm m125-odd.ppm m125-ascii.txt m125-tile_0_0.ppm m125-tile_0_1.ppm m125-tile_1_0.ppm m125-tile_1_1.ppm ;;
            m126) set -- m126-basic.roff m126-blank.roff m126-equation.roff m126-picture.roff m126-table.roff m126-include.roff m126-soelim.roff m126-utf8.roff m126-references.bib m126-refer.roff m126-lookbib.query ;;
            m127) set -- m127-tone.wav m127-tone.raw m127-tone.flac m127-tone.ogg m127-tone.mp3 m127-tone.spx m127-tone.wv m127-vorbis-comments.txt ;;
            m128) set -- m128-input.tar m128-blank-exfat.img m128-tree/alpha.txt m128-tree/nested/beta.txt ;;
        esac
        for file in "$@"; do test -s "$root/tests/$file" || exit 1; done
        if [ "$profile" = m120 ]; then test "$(wc -c < "$root/tests/m120-gray.raw")" -eq 256; fi
        ;;
    m143|m144|m147|m149|m152)
        test -d "$root/etc"
        [ -s "$root/etc/passwd" ] || printf 'root:x:0:0:root:/tmp:/bin/sh\n' > "$root/etc/passwd"
        [ -s "$root/etc/group" ] || printf 'root:x:0:\n' > "$root/etc/group"
        ;;
    m163|m165)
        [ "$root" = "build/external/$profile-batch/$port/rootfs" ] || exit 2
        python3 - "$root/tests" <<'PY'
from pathlib import Path
import sys

target = Path(sys.argv[1])
target.mkdir(parents=True, exist_ok=True)
target.joinpath('m163-color.ppm').write_bytes(
    b'P3\n8 8\n255\n' + b''.join(
        f'{x * 32} {y * 32} {(x + y) * 16}\n'.encode()
        for y in range(8) for x in range(8)))
target.joinpath('m163-gray.pgm').write_bytes(
    b'P2\n8 8\n255\n' + b''.join(
        f'{(x * 29 + y * 13) % 256}\n'.encode()
        for y in range(8) for x in range(8)))
target.joinpath('m163-bw.pbm').write_bytes(
    b'P1\n8 8\n' + b''.join(
        f'{(x + y) % 2}\n'.encode()
        for y in range(8) for x in range(8)))
target.joinpath('m163-rgb.txt').write_text(
    '0 0 0 black\n255 255 255 white\n128 128 128 gray\n', encoding='ascii')
PY
for name in color.ppm gray.pgm bw.pbm rgb.txt; do
    test -s "$root/tests/m163-$name"
done
        ;;
    m166|m167|m168)
        [ "$root" = "build/external/$profile-batch/$port/rootfs" ] || exit 2
        case "$port" in
            amd64) compiler=gcc; machine='Advanced Micro Devices X86-64' ;;
            riscv64) compiler=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
            arm64) compiler=aarch64-linux-gnu-gcc; machine='AArch64' ;;
        esac
        command -v "$compiler" >/dev/null
        verify_static() {
            [ "$(od -An -tx1 -N4 "$1" | tr -d ' \n')" = 7f454c46 ] || exit 1
            readelf -h "$1" | grep -Fq "$machine"
            if readelf -l "$1" | grep -Fq INTERP; then
                echo "[$profile APP FAIL] dynamic native application $1" >&2
                exit 1
            fi
        }
        if [ "$profile" = m166 ]; then
            mkdir -p "$root/tests"
            app="$root/tests/m166-input-display"
            "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
                tests/m166-input-display.c -o "$app"
            verify_static "$app"
            sha256sum "$app" > "$(dirname "$root")/m166-test-application.sha256"
        else
            mkdir -p "$root/usr/bin"
            set -- naseed-desktop naseed-image naseed-terminal
            if [ "$profile" = m168 ]; then set -- "$@" naseed-browser; fi
            for app in "$@"; do
                "$compiler" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
                    "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
                    -o "$root/usr/bin/$app"
                verify_static "$root/usr/bin/$app"
            done
            for app in "$@"; do sha256sum "$root/usr/bin/$app"; done \
                > "$(dirname "$root")/$profile-native-applications.sha256"
        fi
        ;;
    *) exit 2 ;;
esac
echo "[$profile HOST DATA PASS] $port independent fixtures generated on Linux host"

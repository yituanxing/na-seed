#!/bin/sh
# One signed APK staging owner for the original M109–M141 batches.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: stage-alpine-signed-batch.sh PORT PROFILE' >&2; exit 2; }
port=$1
profile=$2
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
case "$profile" in
    m109|m111|m112|m113|m114|m115) count=15; mode=count ;;
    m110) count=21; mode=count ;;
    m116|m117|m118|m119|m120|m121|m122|m123) count=15; mode=unique ;;
    m124|m125|m126|m127|m128|m129|m130) count=15; mode=alias-path ;;
    m131|m132|m133|m134|m135) count=15; mode=alias-exec ;;
    m136|m137|m138) count=20; mode=alias-exec ;;
    m139|m140) count=20; mode=contained-exec ;;
    m141) count=68; mode=contained-file ;;
    *) exit 2 ;;
esac
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
manifest="scripts/$profile-original-elfs.txt"
case "$profile" in
    m12[1-9]|m13[0-9]|m140)
        sh scripts/acceptance/check-elf-history.sh "$label" "$manifest" ;;
    m141)
        actual=$(sh scripts/acceptance/check-elf-history.sh "$label" "$manifest" --count)
        [ "$actual" -eq "$count" ] || { echo "[$label STAGE FAIL] $actual paths, expected $count" >&2; exit 1; } ;;
esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" "$profile-batch"
work="build/external/$profile-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
test -s "$root/lib/apk/db/installed"
require_fixtures() {
    for fixture in "$@"; do test -s "$root/tests/$fixture" || exit 1; done
}
require_packages() {
    for package in "$@"; do
        grep -Fxq "P:$package" "$root/lib/apk/db/installed" || {
            echo "[$label HOST STAGE FAIL] $port signed APK missing $package" >&2
            exit 1
        }
    done
}
case "$profile" in
    m118) require_fixtures m118-debug.o m118-debug-other.o m118-debug.elf m118-textrel.so ;;
    m119) require_fixtures m119-catalog.po m119-batch-run.sh m119-catalog.mo ;;
    m120) require_fixtures m120-color.ppm m120-inverted.ppm m120-gray.pgm m120-gray.raw ;;
    m121) require_fixtures m121-original.pdf m121-invalid.pdf m121-attachment.txt ;;
    m122)
        for fixture in original.ppm red.ppm blue.ppm black.pbm white.pgm flipped.ppm \
                       cropped.ppm inverted.ppm red-channel.pgm red8.ppm red31.ppm \
                       red-blue.ppm original.png invalid.ppm; do
            require_fixtures "m122-$fixture"
        done ;;
    m123) require_fixtures m123-ext2.img m123-empty-ext.img m123-empty-fat.img ;;
    m124)
        require_packages util-linux-misc
        require_fixtures m124-empty-minix.img m124-empty-swap.img m124-cramfs-source/hello.txt m124-look-words.txt ;;
    m125) require_packages netpbm ;;
    m126)
        require_packages groff
        for resource in font tmac; do
            dir="$root/usr/share/groff/1.23.0/$resource"
            if ! test -d "$dir" || ! find "$dir" -type f -print -quit | grep -q .; then
                echo "[$label HOST STAGE FAIL] missing signed groff resource directory: $dir" >&2
                exit 1
            fi
        done ;;
    m127) require_packages wavpack vorbis-tools speex-tools flac lame mpg123 ;;
    m128) require_packages exfatprogs squashfs-tools squashfs-tools-ng blkid wipefs ;;
    m129) require_packages procps-ng psmisc util-linux-misc flock tmux ;;
    m130) require_packages netpbm binutils libarchive-tools zip attr acl e2fsprogs-extra ;;
    m131) require_packages netpbm ;;
    m132) require_packages icu libwebp-tools zopfli pcre2-tools bzip3 ;;
    m133) require_packages graphviz xz diffutils ed mtools ;;
    m134) require_packages graphviz lua5.4 dash mawk xxd gperf ninja-build fish ;;
    m135) require_packages cmake libjpeg-turbo-utils tidyhtml gdbm-tools graphviz netpbm ;;
    m136) require_packages flex byacc re2c ncurses ncurses-terminfo-base util-linux-misc uuidgen findmnt flock lscpu ;;
    m137) require_packages lmdb-tools fontconfig font-dejavu protoc flatc swig ragel xorriso graphicsmagick mdbook rclone restic recode fdupes zint zbar ;;
    m138) require_packages llvm21 ;;
    m139) require_packages fontconfig font-dejavu netpbm util-linux-misc mcookie gnutls-utils gnupg-gpgconf ;;
    m140) require_packages coreutils netpbm ;;
esac
sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$work/$profile-original-signed-elfs.sha256" "$count" "$mode" "$label"
echo "[$label HOST STAGE PASS] $port $count original signed Alpine ELFs ($mode); native guest acceptance remains separate"

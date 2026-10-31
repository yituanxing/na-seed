#!/bin/sh
# Signed package installation and all test audio data are prepared on host.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1
root=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
[ "$root" = "build/external/m127-batch/$port/rootfs" ] || exit 2
python3 tests/m127-make-fixtures.py "$root/tests"
for item in tone.wav tone.raw tone.flac tone.ogg tone.mp3 tone.spx tone.wv \
            vorbis-comments.txt; do
    test -s "$root/tests/m127-$item"
done
echo "[M127 HOST CONFIG PASS] $port Linux-host-generated audio, no guest compiler"

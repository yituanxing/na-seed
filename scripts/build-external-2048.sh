#!/bin/sh
# Compile the genuinely original MIT-licensed 2048.c C source for native Linux.
# Immutable upstream commit + exact Git blob checksum, no local source patch.
set -eu
port=${1:-amd64}
[ "$port" = amd64 ] || { echo "initial port is x86-64 only" >&2; exit 2; }
for cmd in curl gcc git readelf; do command -v "$cmd" >/dev/null; done
upstream=90343eb209e84fa9687d596f5d5eecfdf7d4df51
blob=a1673a2cfe86de6bc50a6c9db0e5bd125395ab74
work=build/external/2048/amd64
mkdir -p "$work"
src="$work/2048.c"
if [ ! -s "$src" ]; then
    curl -fLsS --retry 3 --retry-delay 2 \
      -o "$src.tmp" "https://raw.githubusercontent.com/mevdschee/2048.c/$upstream/2048.c"
    mv "$src.tmp" "$src"
fi
test "$(git hash-object "$src")" = "$blob" || {
    echo "[GAME] upstream blob mismatch; refusing to build" >&2; exit 1;
}
gcc -std=gnu99 -O2 -Wall -Wextra -static -no-pie "$src" -o "$work/2048"
readelf -h "$work/2048" | grep -Fq 'Advanced Micro Devices X86-64'
if readelf -l "$work/2048" | grep -q INTERP; then
    echo "[GAME] nonstatic Linux ELF" >&2; exit 1
fi
test "$("$work/2048" --version)" = "2048.c version 1.0.3"
"$work/2048" test | grep -Fxq 'All 13 tests executed successfully'
sha256sum "$work/2048" > "$work/binary.sha256"
echo "[GAME LINUX ORACLE] original 2048.c v1.0.3 game rules 13/13 pass"

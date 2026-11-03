#!/bin/sh
# Pin upstream LVGL 9.4; original Alpine signed binaries are unchanged.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 root=$2
case "$port" in
    amd64) cc=gcc;cxx=g++;machine='Advanced Micro Devices X86-64' ;;
    riscv64) cc=riscv64-linux-gnu-gcc;cxx=riscv64-linux-gnu-g++;machine='RISC-V' ;;
    arm64) cc=aarch64-linux-gnu-gcc;cxx=aarch64-linux-gnu-g++;machine='AArch64' ;;
    *) exit 2 ;;
esac
command -v "$cc" >/dev/null
command -v "$cxx" >/dev/null
test -s third_party/lvgl/CMakeLists.txt
work="build/external/m171-lvgl/$port"
mkdir -p "$work" "$root/usr/bin"
cmake -S third_party/lvgl -B "$work" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_C_COMPILER="$(command -v "$cc")" \
    -DCMAKE_CXX_COMPILER="$(command -v "$cxx")" \
    -DLV_BUILD_CONF_DIR="$(pwd)/user/desktop/lvgl-config" \
    -DCONFIG_LV_BUILD_DEMOS=OFF \
    -DCONFIG_LV_BUILD_EXAMPLES=OFF \
    -DCONFIG_LV_USE_THORVG_INTERNAL=OFF \
    -DBUILD_SHARED_LIBS=OFF > "$work/configure.log"
cmake --build "$work" --target lvgl -j 4 > "$work/build.log"
test -s "$work/lib/liblvgl.a"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    user/desktop/naseed-desktop.c user/desktop/protocol.c user/desktop/text.c \
    -o "$root/usr/bin/naseed-desktop"
"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    -DLV_CONF_INCLUDE_SIMPLE -Ithird_party/lvgl \
    -Iuser/desktop/lvgl-config \
    user/desktop/naseed-lvgl.c user/desktop/protocol.c \
    "$work/lib/liblvgl.a" -lm -o "$root/usr/bin/naseed-lvgl"
for app in naseed-desktop naseed-image naseed-terminal naseed-browser naseed-lvgl; do
    file="$root/usr/bin/$app"
    test -x "$file"
    readelf -h "$file" | grep -Fq "$machine"
    if readelf -l "$file" | grep -Fq INTERP; then
        echo "[M171 FAIL] dynamic client $app" >&2;exit 1
    fi
done
if [ "$port" = amd64 ]; then
    "$root/usr/bin/naseed-lvgl" --selftest171 > "$work/host-lvgl.log"
    grep -Fq '[M171 HOST LVGL PASS]' "$work/host-lvgl.log"
fi
sha256sum "$root/usr/bin/naseed-lvgl" "$root/usr/bin/naseed-desktop" \
    > "$work/naseed-lvgl-native.sha256"
echo "[M171 LVGL ABI PASS] $port actual pinned LVGL 9.4 static GUI client"

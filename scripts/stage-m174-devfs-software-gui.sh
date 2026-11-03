#!/bin/sh
# One installed signed-Alpine profile, then host-built independent Linux ABI
# desktop and devfs launcher. Repack the same archive only after all additions.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) cc=gcc; machine='Advanced Micro Devices X86-64' ;;
    riscv64) cc=riscv64-linux-gnu-gcc; machine='RISC-V' ;;
    arm64) cc=aarch64-linux-gnu-gcc; machine='AArch64' ;;
    *) exit 2 ;;
esac
sh scripts/stage-m174-prebuilt.sh "$port"
work="build/external/m174-software-batch/$port"
root="$work/rootfs"
mkdir -p "$root/usr/bin"

# Image, terminal, browser are independent programs, not compositor roles.
# The LVGL host stage compiles the compositor and actual upstream LVGL client.
for app in naseed-image naseed-terminal naseed-browser; do
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        "user/desktop/$app.c" user/desktop/protocol.c user/desktop/text.c \
        -o "$root/usr/bin/$app"
done
sh scripts/host-configure-m171-lvgl.sh "$port" "$root"

"$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
    user/m174-devfs-launch.c -o "$root/usr/bin/naseed-m174-devfs-launch"
ln -sfn naseed-lvgl "$root/usr/bin/gui"

# M176 is an explicitly opt-in extension of THIS SAME M174 rootfs, not a
# second boot/profile: host APK adds untouched original Ncat and the guest
# launcher performs a real external HTTP transaction before the prior
# M174 devfs/software/five-frame desktop workload.
if [ "${NAS_M176_NET_STAGE:-0}" = 1 ]; then
    sh scripts/stage-m176-irq-network.sh "$port" "$root" "$cc"
fi

# M180 adds one ordinary, separately linked ring3 ELF and extends the same
# opt-in M176 PID1 to wait for its actual exit status before the original
# software/desktop workload. Do not alter the M174-M179 default rootfs.
m180_apps=
if [ "${NAS_M180_FS_USER_STAGE:-0}" = 1 ]; then
    [ "${NAS_M176_NET_STAGE:-0}" = 1 ] || {
        echo "[M180 STAGE FAIL] requires the preserved M176 native launcher" >&2
        exit 1
    }
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        user/m180-fs-user.c -o "$root/usr/bin/naseed-m180-fs-user"
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        -DNAS_M180_FS_USER_STAGE=1 \
        scripts/test-m176-ncat-launch.c \
        -o "$root/usr/bin/naseed-m174-devfs-launch"
    m180_apps=' naseed-m180-fs-user'
fi

for app in naseed-desktop naseed-image naseed-terminal naseed-browser \
           naseed-lvgl naseed-m174-devfs-launch $m180_apps; do
    file="$root/usr/bin/$app"
    test -x "$file"
    readelf -h "$file" | grep -Fq "$machine"
    if readelf -l "$file" | grep -Fq INTERP; then
        echo "[M174 STAGE FAIL] dynamic NaSeed native executable: $app" >&2
        exit 1
    fi
done
test -L "$root/usr/bin/gui"
test -s "$root/tests/m174-software-run.sh"
(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "${#path}" -le 254 ] || exit 1
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
echo "[M174 STAGE PASS] $port 20 original signed Alpine ELFs + VFS-devfs PID1 launcher + five GUI clients"
if [ "${NAS_M180_FS_USER_STAGE:-0}" = 1 ]; then
    echo "[M180 STAGE PASS] $port separate static ring3 user ext4 fsync ELF + existing M176 PID1 launcher"
fi

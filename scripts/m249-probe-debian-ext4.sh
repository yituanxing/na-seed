#!/bin/sh
# M249: signed original Debian 13 glibc dash and coreutils run FROM a real
# physical ext4 root on the same native NaSeed kernel.  The CPIO is a distinct
# rescue image and deliberately does not contain Debian's tested binaries.
set -eu

[ "$#" -eq 1 ] || { echo 'usage: sh scripts/m249-probe-debian-ext4.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
case "$port" in
    amd64) triplet=x86_64-linux-gnu; loader=ld-linux-x86-64.so.2; label=X86 ;;
    riscv64) triplet=riscv64-linux-gnu; loader=ld-linux-riscv64-lp64d.so.1; label=RV64 ;;
    arm64) triplet=aarch64-linux-gnu; loader=ld-linux-aarch64.so.1; label=ARM64 ;;
    *) echo "[M249 FAIL] unsupported architecture $port" >&2; exit 2 ;;
esac
. scripts/native-qemu-target.sh

for tool in python3 apt-get apt-cache dpkg-deb readelf cpio cmp sha256sum \
    mke2fs e2fsck debugfs sudo xorriso sgdisk mformat mmd mcopy; do
    command -v "$tool" >/dev/null || { echo "[M249 FAIL] missing $tool" >&2; exit 1; }
done

work="build/m249-debian-ext4/$port"
root="$work/rootfs"
disk="$PWD/$work/m249-debian-root.img"
manifest="$work/source/manifest.json"
mkdir -p "$work"
python3 scripts/m248-fetch-debian-multiarch.py --arch "$port" --out "$work/source"
source=$(cd "$work/source/sysroot" && pwd -P)
test -s "$manifest"

# This separately staged guest disk contains ONLY signed package ELF bytes,
# their signed glibc dependency, and small clearly marked CI configuration.
# It is a minimal verified Debian userland, not an original full Debian image.
mkdir -p "$root/usr/bin" "$root/usr/lib/$triplet" "$root/lib" "$root/lib64" \
    "$root/etc" "$root/dev" "$root/proc" "$root/sys" "$root/var/tmp" "$root/tmp"
chmod 1777 "$root/tmp" "$root/var/tmp"
for name in dash true cat wc printf; do
    original="$source/usr/bin/$name"
    resolved=$(readlink -f "$original")
    case "$resolved" in "$source"/*) : ;; *) echo '[M249 FAIL] source binary path escape' >&2; exit 1 ;; esac
    cp -L "$original" "$root/usr/bin/$name"
    chmod 755 "$root/usr/bin/$name"
    cmp "$original" "$root/usr/bin/$name"
done
for name in libc.so.6 "$loader"; do
    original="$source/usr/lib/$triplet/$name"
    resolved=$(readlink -f "$original")
    case "$resolved" in "$source"/*) : ;; *) echo '[M249 FAIL] source glibc path escape' >&2; exit 1 ;; esac
    cp -L "$original" "$root/usr/lib/$triplet/$name"
    chmod 755 "$root/usr/lib/$triplet/$name"
    cmp "$original" "$root/usr/lib/$triplet/$name"
done
ln -sfn "../usr/lib/$triplet" "$root/lib/$triplet"
if [ "$port" = amd64 ]; then
    ln -sfn "../usr/lib/$triplet/$loader" "$root/lib64/$loader"
else
    ln -sfn "../usr/lib/$triplet/$loader" "$root/lib/$loader"
fi

printf '%s\n' M249-EXT4-INPUT > "$root/etc/m249-input"
marker="M249-DEBIAN-GLIBC-EXT4-$label-OK"
printf '%s\n' \
    '#!/usr/bin/dash' \
    'set -eu' \
    'value=$(/usr/bin/cat /etc/m249-input)' \
    '[ "$value" = M249-EXT4-INPUT ]' \
    'count=$(/usr/bin/wc -c < /etc/m249-input)' \
    '[ "$count" = 16 ]' \
    '/usr/bin/true > /dev/null' \
    '/usr/bin/cat /etc/m249-input' \
    '/usr/bin/wc -c /etc/m249-input' \
    '/usr/bin/cat /etc/m249-input | /usr/bin/wc -c &' \
    'pipe_pid=$!' \
    'wait "$pipe_pid"' \
    '[ ! -e /var/tmp/m249-persisted ]' \
    "/usr/bin/printf 'M249-EXT4-GUEST-WROTE\\n' > /var/tmp/m249-persisted" \
    '[ "$(/usr/bin/cat /var/tmp/m249-persisted)" = M249-EXT4-GUEST-WROTE ]' \
    "exec /usr/bin/printf '$marker\\n'" \
    > "$root/etc/m249-init.sh"
chmod 755 "$root/etc/m249-init.sh"

# On-disk evidence is captured/verified both before and after QEMU.  The
# actual writable partition is a second physical virtio-blk drive, NOT a
# Limine-delivered rootfs.cpio and NOT a host-mode qemu-user emulation.
sudo python3 scripts/m249-debian-ext4-disk.py prepare "$port" "$root" "$disk" "$manifest"
# mke2fs and the physical-disk verifier run as root, but the native QEMU
# process intentionally runs unprivileged. Preserve the fully verified disk
# bytes and hand ONLY this disposable disk file back to the CI runner.
sudo chown "$(id -u):$(id -g)" "$disk"
test -r "$disk" && test -w "$disk" || {
    echo '[M249 FAIL] verified ext4 root is not accessible to unprivileged QEMU' >&2
    exit 1
}

# The M191 rescue CPIO is separate and contains no Debian glibc/dash files.
rescue="$work/rescue"
mkdir -p "$rescue"
sh scripts/fetch-busybox-fixture.sh "$port" "$work/rescue-busybox"
sh scripts/stage-busybox-rootfs.sh "$work/rescue-busybox" "$rescue"
test -s "$rescue/rootfs.cpio"
if cpio -it --quiet < "$rescue/rootfs.cpio" | grep -Eq '(^|/)usr/bin/(dash|true|cat|wc|printf)$'; then
    echo '[M249 FAIL] Debian binaries contaminated rescue CPIO' >&2
    exit 1
fi

case_id=m249-batch
case_file="$work/m249-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio /etc/m249-init.sh "$marker" - /usr/bin/dash \
    > "$case_file"

export NAS_ENABLE_LWEXT4=1 NAS_ENABLE_VIRTIO_BLK=1 NAS_ENABLE_M191_ALPINE_ROOT=1
export NAS_ENABLE_RAMFS=0 NAS_ENABLE_LITTLEFS=0
unset NAS_ENABLE_M179_FS_IRQ NAS_ENABLE_M180_FS_USER_IRQ
unset NAS_ENABLE_VIRTIO_BLK_ASYNC NAS_QEMU_ASYNC_BLK NAS_QEMU_ASYNC_BLK_IMAGE
export NAS_VIRTIO_BLK_IMAGE="$disk"
export NAS_ACCEPTANCE_CPIO="$PWD/$rescue/rootfs.cpio"
export BUSYBOX_PARITY_BINARY="$PWD/$work/rescue-busybox"
export BUSYBOX_PARITY_CASE="$case_id"
export BUSYBOX_PARITY_MANIFEST="$case_file"
export NAS_ACCEPTANCE_QEMU_RAM=1024M NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_SYSCALL_TRACE=1

# The multiarch matrix runners reuse existing kernel.elf opportunistically.
# Build this EXPLICITLY with real external ext4 root enabled first.
if [ "$port" = amd64 ]; then
    . scripts/native-source-qemu.sh
    nas_source_build_kernel m249-debian
else
    case "$port" in
        riscv64) sh scripts/build-riscv64-full.sh ;;
        arm64) sh scripts/build-aarch64-full.sh ;;
    esac
fi

echo "[M249 NATIVE $label] original signed Debian/glibc direct PID1 FROM real ext4 root"
sh "$runner"
log="$logs/$case_id.clean.log"
test -s "$log"
grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"
grep -Fq '[ROOTFS] physical ext4 root registered at /' "$log"
grep -Fq '[ROOTFS] executable PID1 from mounted ext4: /usr/bin/dash' "$log"
grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=/usr/bin/dash /etc/m249-init.sh" "$log"
for name in true cat wc printf; do
    grep -Fq "[EXEC] replaced process with lwext4 ELF: /usr/bin/$name" "$log"
done
dyn_count=$(grep -Fc '[DYN] real ELF interpreter loaded, entry=' "$log")
test "$dyn_count" -ge 5
grep -Fxq M249-EXT4-INPUT "$log"
grep -Eq '^[[:space:]]*16[[:space:]]+/etc/m249-input$' "$log"
grep -Fxq 16 "$log"
grep -Fxq "$marker" "$log"
grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo '[M249 FAIL] guest serial contains explicit failure' >&2
    exit 1
fi
sudo python3 scripts/m249-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest"
echo "[M249 ORIGINAL DEBIAN GLIBC EXT4 REAL $label QEMU PASS] direct dash PID1, original 4 coreutils, durable guest write, clean unmount and readback"

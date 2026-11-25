#!/bin/sh
# M249: signed original Debian 13 glibc dash and coreutils run FROM a real
# physical ext4 root on the same native NaSeed kernel.  The CPIO is a distinct
# rescue image and deliberately does not contain Debian's tested binaries.
set -eu

[ "$#" -eq 1 ] || { echo 'usage: sh scripts/m249-probe-debian-ext4.sh amd64|riscv64|arm64' >&2; exit 2; }
port=$1
. scripts/acceptance/debian-ext4.sh
nas_debian_target M249

nas_debian_require_tools

work="build/m249-debian-ext4/$port"
root="$work/rootfs"
disk="$PWD/$work/m249-debian-root.img"
manifest="$work/source/manifest.json"
nas_debian_stage_base 'source binary path escape' 'source glibc path escape'

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
nas_debian_chown_disk 'verified ext4 root is not accessible to unprivileged QEMU'

# The M191 rescue CPIO is separate and contains no Debian glibc/dash files.
nas_debian_stage_rescue '(^|/)usr/bin/(dash|true|cat|wc|printf)$' \
    'Debian binaries contaminated rescue CPIO'

case_id=m249-batch
case_file="$work/m249-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio /etc/m249-init.sh "$marker" - /usr/bin/dash \
    > "$case_file"

nas_debian_root_env
export NAS_APP_BATCH_QEMU_TIMEOUT=240
export NAS_SYSCALL_TRACE=1

# The multiarch matrix runners reuse existing kernel.elf opportunistically.
# Build this EXPLICITLY with real external ext4 root enabled first.
nas_debian_build_kernel m249-debian

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

#!/bin/sh
# M250: physical QEMU UART -> test-only static CTTY PID1 -> execve() same PID1
# authentic Debian 13 glibc dash -i from the M249 real physical ext4 root.
# The UART driver sends original commands only on actual interactive prompts.
set -eu

[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/m250-probe-debian-interactive.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/acceptance/debian-ext4.sh
nas_debian_target M250

nas_debian_require_tools

work="build/m250-debian-interactive/$port"
root="$work/rootfs"
disk="$PWD/$work/m250-debian-root.img"
manifest="$work/source/manifest.json"
nas_debian_stage_base 'source path escape' 'glibc source path escape'

# Keep M249's audited disk source metadata in its inherited validator;
# M250 deliberately does NOT boot this script or pretend direct-dash PID1.
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m249-init.sh"
chmod 755 "$root/etc/m249-init.sh"
printf '%s\n' M249-EXT4-INPUT > "$root/etc/m249-input"
printf '%s\n' M250-EXT4-INPUT > "$root/etc/m250-input"

# The ONLY added ELF is our explicitly NON-DEBIAN CTTY transport launcher.
# It sits on ext4, not in CPIO, and is fully replaced via execve by the
# unmodified original /usr/bin/dash -i with its genuine PT_INTERP loader.
nas_debian_stage_launcher 'self-contained test-only CTTY launcher must be static'
if ! readelf -lW "$root/usr/bin/dash" | grep -q INTERP; then
    echo '[M250 FAIL] original glibc Debian dash PT_INTERP is absent' >&2
    exit 1
fi

sudo python3 scripts/m250-debian-ext4-disk.py prepare "$port" "$root" "$disk" "$manifest"
# mkfs under sudo creates root-owned image; real QEMU must be able to write.
nas_debian_chown_disk 'physical ext4 image inaccessible to unprivileged QEMU'

# This separate rescue archive never contains either Debian ELF or our
# launcher. There is no alternate CPIO shell success path in this test.
nas_debian_stage_rescue '(^|/)(sbin/m250-tty-launch|usr/bin/(dash|true|cat|wc|printf))$' \
    'physical ext4 guest binaries contaminated rescue CPIO'

case_id=m250-batch
case_file="$work/m250-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio --console - - /sbin/m250-tty-launch > "$case_file"

nas_debian_root_env
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M250_QEMU_TIMEOUT:-300}
export NAS_SYSCALL_TRACE=1

nas_debian_build_kernel m250-debian

nas_debian_uart_env m250
echo "[M250 NATIVE $label] genuine QEMU UART -> physical ext4 CTTY launcher -> original Debian glibc dash -i"
python3 scripts/m250-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"

log="$logs/$case_id.clean.log"
test -s "$log"
grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"
grep -Fq '[ROOTFS] physical ext4 root registered at /' "$log"
grep -Fq '[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch' "$log"
grep -Fq '[CMDLINE] userspace argv from Limine: naseed.argv=/sbin/m250-tty-launch --console' "$log"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/dash' "$log"
grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
if grep -Fq '[FAIL]' "$log"; then
    echo '[M250 FAIL] guest serial has explicit failure' >&2
    exit 1
fi
sudo python3 scripts/m250-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest"
echo "[M250 INTERACTIVE ORIGINAL DEBIAN GLIBC EXT4 REAL $label QEMU PASS] unmodified dash -i, live UART, pipe, original coreutils, durable ext4 write, clean unmount and fsck"

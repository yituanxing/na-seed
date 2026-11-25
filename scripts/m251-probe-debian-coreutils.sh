#!/bin/sh
# M251: original signed Debian 13/glibc coreutils, executed interactively from
# a real writable VirtIO ext4 root on native NaSeed system-QEMU.
set -eu

[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/m251-probe-debian-coreutils.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/acceptance/debian-ext4.sh
nas_debian_target M251

nas_debian_require_tools

work="build/m251-debian-coreutils/$port"
root="$work/rootfs"
disk="$PWD/$work/m251-debian-root.img"
manifest="$work/source/manifest.json"
extra="$work/m251-extra.json"
nas_debian_stage_base 'signed source path escape' 'signed glibc path escape'

# Keep the M249/M250 signed disk base contracts independent of the new batch.
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m249-init.sh"
chmod 755 "$root/etc/m249-init.sh"
printf '%s\n' M249-EXT4-INPUT > "$root/etc/m249-input"
printf '%s\n' M250-EXT4-INPUT > "$root/etc/m250-input"
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m251-init.sh"
chmod 755 "$root/etc/m251-init.sh"
printf '%s\n' M251-EXT4-INPUT > "$root/etc/m251-input"

nas_debian_stage_launcher 'test-only CTTY launcher must be static'

python3 scripts/m251-debian-provenance.py prepare "$port" "$root" "$manifest" "$extra"
sudo python3 scripts/m251-debian-ext4-disk.py prepare "$port" "$root" "$disk" "$manifest" "$extra"
nas_debian_chown_disk 'real ext4 disk inaccessible to unprivileged QEMU'

nas_debian_stage_rescue '(^|/)(sbin/m250-tty-launch|usr/bin/(dash|true|cat|wc|printf|ls|head|sort|mkdir|rm|env|sleep))$' \
    'original Debian ELF leaked into rescue CPIO'

case_id=m251-batch
case_file="$work/m251-cases.tsv"
printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
    "$case_id" cpio --console - - /sbin/m250-tty-launch > "$case_file"
nas_debian_root_env
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M251_QEMU_TIMEOUT:-300}
export NAS_SYSCALL_TRACE=1

nas_debian_build_kernel m251-debian

nas_debian_uart_env m251
echo "[M251 NATIVE $label] authenticated original Debian coreutils over real UART/ext4"
python3 scripts/m251-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"

log="$logs/$case_id.clean.log"
test -s "$log"
grep -Fq '[ROOTFS] physical ext4 root registered at /' "$log"
grep -Fq '[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch' "$log"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/dash' "$log"
grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
for name in ls head sort mkdir rm env sleep; do
    grep -Fq "[EXEC] replaced process with lwext4 ELF: /usr/bin/$name" "$log"
done
if grep -Fq '[FAIL]' "$log"; then
    echo '[M251 FAIL] native QEMU serial has explicit failure' >&2; exit 1
fi
sudo python3 scripts/m251-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest" "$extra"
echo "[M251 ORIGINAL DEBIAN COREUTILS EXT4 REAL $label QEMU PASS] seven new signed ELF, live UART commands, clean fsck and durable disk readback"

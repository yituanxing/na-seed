#!/bin/sh
# M252: original signed Debian 13/glibc grep/sed/find, executed interactively from
# a real writable VirtIO ext4 root on native NaSeed system-QEMU.
set -eu

[ "$#" -eq 1 ] || {
    echo 'usage: sh scripts/m252-probe-debian-text-tools.sh amd64|riscv64|arm64' >&2
    exit 2
}
port=$1
. scripts/acceptance/debian-ext4.sh
nas_debian_target M252

# M254 adds a guest-launched Debian shell lifecycle workload *inside the same
# M252 native QEMU boot*, preserving every signed M252 command and disk oracle.
case "${NAS_ENABLE_M254_DEBIAN_SHELL:-0}" in
    0) m254_shell=0 ;;
    1) m254_shell=1 ;;
    *) echo '[M252/M254 FAIL] invalid shell lifecycle toggle' >&2; exit 2 ;;
esac
case "${NAS_ENABLE_M255_DEBIAN_INIT:-0}" in
    0) m255_init=0 ;;
    1) m255_init=1 ;;
    *) echo '[M255 FAIL] invalid original sysvinit toggle' >&2; exit 2 ;;
esac
case "${NAS_ENABLE_M256_DEBIAN_INITCTL:-0}" in
    0) m256_initctl=0 ;;
    1) m256_initctl=1 ;;
    *) echo '[M256 FAIL] invalid signed initctl toggle' >&2; exit 2 ;;
esac
case "${NAS_ENABLE_M257_DEBIAN_SHUTDOWN:-0}" in
    0) m257_shutdown=0 ;;
    1) m257_shutdown=1 ;;
    *) echo '[M257 FAIL] invalid original Debian natural poweroff toggle' >&2; exit 2 ;;
esac
[ "$m254_shell" = 0 ] || { [ "$m255_init" = 0 ] && [ "$m256_initctl" = 0 ] && [ "$m257_shutdown" = 0 ]; } || {
    echo '[M255-M257 FAIL] interactive dash and original init are exclusive' >&2
    exit 2
}
[ $((m255_init + m256_initctl + m257_shutdown)) -le 1 ] || {
    echo '[M257 FAIL] original init lifecycle/initctl/poweroff profiles are exclusive' >&2
    exit 2
}

nas_debian_require_tools

if [ "$m257_shutdown" = 1 ]; then
    work="build/m257-debian-shutdown/$port"
    case_id=m257-shutdown
elif [ "$m256_initctl" = 1 ]; then
    work="build/m256-debian-initctl/$port"
    case_id=m256-initctl
elif [ "$m255_init" = 1 ]; then
    work="build/m255-debian-init/$port"
    case_id=m255-init
elif [ "$m254_shell" = 1 ]; then
    work="build/m254-debian-shell/$port"
    case_id=m254-shell
else
    work="build/m252-debian-text-tools/$port"
    case_id=m252-batch
fi
root="$work/rootfs"
disk="$PWD/$work/m252-debian-root.img"
manifest="$work/source/manifest.json"
m251extra="$work/m251-extra.json"
extra="$work/m252-extra.json"
m255extra="$work/m255-extra.json"
m256extra="$work/m256-extra.json"
m257extra="$work/m257-extra.json"
nas_debian_stage_base 'signed source path escape' 'signed glibc path escape'

# Keep the M249/M250 signed disk base contracts independent of the new batch.
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m249-init.sh"
chmod 755 "$root/etc/m249-init.sh"
printf '%s\n' M249-EXT4-INPUT > "$root/etc/m249-input"
printf '%s\n' M250-EXT4-INPUT > "$root/etc/m250-input"
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m251-init.sh"
chmod 755 "$root/etc/m251-init.sh"
printf '%s\n' M251-EXT4-INPUT > "$root/etc/m251-input"
printf '%s\n' '#!/usr/bin/dash' 'exit 0' > "$root/etc/m252-init.sh"
chmod 755 "$root/etc/m252-init.sh"
printf '%s\n' M252-EXT4-INPUT > "$root/etc/m252-input"
if [ "$m254_shell" = 1 ]; then
    cp scripts/m254-guest-workload.sh "$root/etc/m254-guest-workload.sh"
    chmod 755 "$root/etc/m254-guest-workload.sh"
fi
if [ "$m255_init" = 1 ] || [ "$m256_initctl" = 1 ] || [ "$m257_shutdown" = 1 ]; then
    python3 scripts/m255-debian-ext4-disk.py stage "$root"
fi
if [ "$m256_initctl" = 1 ] || [ "$m257_shutdown" = 1 ]; then
    python3 scripts/m256-debian-ext4-disk.py stage "$root"
    mkdir -p "$root/usr/sbin"
    "$cc" -std=c11 -O2 -Wall -Wextra -Werror -static -no-pie \
        tests/m256-mount-run.c -o "$root/sbin/m256-mount-run"
    chmod 755 "$root/sbin/m256-mount-run"
    if readelf -lW "$root/sbin/m256-mount-run" | grep -q INTERP; then
        echo '[M256 FAIL] test-only mount helper unexpectedly dynamic' >&2; exit 1
    fi
fi
if [ "$m257_shutdown" = 1 ]; then
    python3 scripts/m257-debian-ext4-disk.py stage "$root"
fi

nas_debian_stage_launcher 'test-only CTTY launcher must be static'

python3 scripts/m251-debian-provenance.py prepare "$port" "$root" "$manifest" "$m251extra"
python3 scripts/m252-debian-provenance.py prepare "$port" "$root" "$manifest" "$m251extra" "$extra"
if [ "$m255_init" = 1 ] || [ "$m256_initctl" = 1 ] || [ "$m257_shutdown" = 1 ]; then
    python3 scripts/m255-debian-provenance.py prepare \
        "$port" "$root" "$manifest" "$m251extra" "$extra" "$m255extra"
fi
if [ "$m256_initctl" = 1 ] || [ "$m257_shutdown" = 1 ]; then
    python3 scripts/m256-debian-provenance.py prepare \
        "$port" "$root" "$manifest" "$m251extra" "$extra" "$m255extra" "$m256extra"
fi
if [ "$m257_shutdown" = 1 ]; then
    python3 scripts/m257-debian-provenance.py prepare \
        "$port" "$root" "$manifest" "$m251extra" "$extra" \
        "$m255extra" "$m256extra" "$m257extra"
fi
sudo python3 scripts/m252-debian-ext4-disk.py prepare "$port" "$root" "$disk" "$manifest" "$m251extra" "$extra"
if [ "$m255_init" = 1 ]; then
    sudo python3 scripts/m255-debian-ext4-disk.py "$port" "$root" "$disk" \
        "$manifest" "$m251extra" "$extra" "$m255extra"
fi
if [ "$m256_initctl" = 1 ]; then
    sudo python3 scripts/m256-debian-ext4-disk.py before "$port" "$root" "$disk" \
        "$manifest" "$m251extra" "$extra" "$m255extra" "$m256extra"
fi
if [ "$m257_shutdown" = 1 ]; then
    sudo python3 scripts/m257-debian-ext4-disk.py before "$port" "$root" "$disk" \
        "$manifest" "$m251extra" "$extra" "$m255extra" "$m256extra" "$m257extra"
fi
if [ "$m254_shell" = 1 ]; then
    sudo python3 scripts/m254-debian-ext4-disk.py before "$port" "$root" "$disk" "$manifest" "$m251extra" "$extra"
fi
nas_debian_chown_disk 'real ext4 disk inaccessible to unprivileged QEMU'

nas_debian_stage_rescue '(^|/)(sbin/(m250-tty-launch|m256-mount-run)|usr/sbin/(telinit|halt)|usr/bin/(dash|true|cat|wc|printf|ls|head|sort|mkdir|rm|env|sleep|sync|grep|sed|find))$' \
    'original Debian ELF leaked into rescue CPIO'

case_file="$work/m252-cases.tsv"
if [ "$m255_init" = 1 ] || [ "$m256_initctl" = 1 ] || [ "$m257_shutdown" = 1 ]; then
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$case_id" cpio ' ' - - /sbin/init > "$case_file"
else
    printf '%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$case_id" cpio --console - - /sbin/m250-tty-launch > "$case_file"
fi
nas_debian_root_env
if [ "$m257_shutdown" = 1 ]; then
    export NAS_M241_PLATFORM_POWERDOWN=1 NAS_M241_EXPECT_NATURAL_POWEROFF=1
fi
export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M252_QEMU_TIMEOUT:-300}
if [ "$m254_shell" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M254_QEMU_TIMEOUT:-420}
elif [ "$m255_init" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M255_QEMU_TIMEOUT:-420}
elif [ "$m256_initctl" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M256_QEMU_TIMEOUT:-480}
elif [ "$m257_shutdown" = 1 ]; then
    export NAS_APP_BATCH_QEMU_TIMEOUT=${NAS_M257_QEMU_TIMEOUT:-700}
fi
export NAS_SYSCALL_TRACE=1

if [ "$port" = amd64 ] && [ "$m257_shutdown" = 1 ]; then
    nas_debian_build_kernel m257-debian-shutdown
else
    nas_debian_build_kernel m252-debian
fi

nas_debian_uart_env m252
if [ "$m257_shutdown" = 1 ]; then
    echo "[M257 NATIVE $label] original signed Debian telinit 0 and halt POWER_OFF on physical ext4"
    python3 scripts/m257-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"
    sudo python3 scripts/m257-debian-ext4-disk.py after "$port" "$root" "$disk" \
        "$manifest" "$m251extra" "$extra" "$m255extra" "$m256extra" "$m257extra"
    echo "[M257 ORIGINAL DEBIAN SYSVINIT NATIVE POWER_OFF/EXT4 PASS] $port: original telinit 0, halt -p -f, natural native QEMU shutdown, upstream ext4 clean unmount and fresh guest nonce offline fsck/readback"
    exit 0
elif [ "$m256_initctl" = 1 ]; then
    echo "[M256 NATIVE $label] original signed Debian sysvinit PID1 with tmpfs FIFO and native telinit 3"
    python3 scripts/m256-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"
    echo "[M256 ORIGINAL DEBIAN INITCTL NATIVE DIAGNOSTIC] $port: real FIFO and runlevel3 observed; clean poweroff and durable ext4 NOT YET PASS"
    exit 0
elif [ "$m255_init" = 1 ]; then
    echo "[M255 NATIVE $label] signed ORIGINAL Debian sysvinit-core /sbin/init PID1, inittab sysinit/respawn"
    python3 scripts/m255-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"
    # Diagnostic only: original sysvinit stays alive and final poweroff(2)
    # remains unimplemented. Never invoke M252 post-shutdown ext4/PASS checks.
    echo "[M255 ORIGINAL DEBIAN SYSVINIT NATIVE DIAGNOSTIC] $port: PID1/respawn observed; clean poweroff and durable ext4 NOT YET PASS"
    exit 0
elif [ "$m254_shell" = 1 ]; then
    echo "[M254 NATIVE $label] original Debian multi-process dash script + M252 signed ELF on physical ext4"
    python3 scripts/m254-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"
else
    echo "[M252 NATIVE $label] authenticated original Debian grep/sed/find over real UART/ext4"
    python3 scripts/m252-debian-uart.py "$port" "$runner" "$serial" "$runner_log" "$fifo"
fi

log="$logs/$case_id.clean.log"
test -s "$log"
grep -Fq '[ROOTFS] physical ext4 root registered at /' "$log"
grep -Fq '[ROOTFS] executable PID1 from mounted ext4: /sbin/m250-tty-launch' "$log"
grep -Fq '[EXEC] replaced process with lwext4 ELF: /usr/bin/dash' "$log"
grep -Fq '[LWEXT4 GUEST] clean upstream unmount and durable flush' "$log"
grep -Fq '[PASS] cmdline userspace exit 0' "$log"
for name in grep sed find; do
    grep -Fq "[EXEC] replaced process with lwext4 ELF: /usr/bin/$name" "$log"
done
if grep -Fq '[FAIL]' "$log"; then
    echo '[M252 FAIL] native QEMU serial has explicit failure' >&2; exit 1
fi
sudo python3 scripts/m252-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest" "$m251extra" "$extra"
if [ "$m254_shell" = 1 ]; then
    sudo python3 scripts/m254-debian-ext4-disk.py after "$port" "$root" "$disk" "$manifest" "$m251extra" "$extra"
    echo "[M254 ORIGINAL DEBIAN SHELL LIFECYCLE EXT4 REAL $label QEMU PASS] signed dash script, multi-process wait, pipe, host disk oracle"
fi
echo "[M252 ORIGINAL DEBIAN TEXT TOOLS EXT4 REAL $label QEMU PASS] three new signed ELF, live UART commands, clean fsck and durable disk readback"

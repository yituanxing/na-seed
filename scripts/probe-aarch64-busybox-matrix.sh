#!/bin/sh
# Architecture parity regression: run *unmodified* official Debian ARM64
# BusyBox through the SAME shared kernel as x86 for a growing workload matrix.
# No guest-side test-specific kernel shortcuts. Strict stdout + exit checks.
set -eu
sh scripts/fetch-busybox-fixture.sh arm64 build/port-aarch64/full/busybox
test -s build/port-aarch64/full/kernel.elf || sh scripts/build-aarch64-full.sh
dir=build/port-aarch64/full
work="$dir/matrix"
mkdir -p "$work"
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
        https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
efi=build/deps/limine-binary/BOOTAA64.EFI
test -s "$efi"
fw=/usr/share/qemu-efi-aarch64/QEMU_EFI.fd
test -s "$fw"
# The rootfs is built only from verified native BusyBox plus actual bytes
# in the archive. The same kernel bootfs.c handles archive/file lookup.
sh scripts/stage-busybox-rootfs.sh "$dir/busybox" "$work"
# Read exact guest program headers when a native ARM-only page access
# differs from the same x86/RV64 ELF loader. This is evidence, not a
# new architecture-specific loader or ELF transformation.
readelf -lW "$dir/busybox" | grep -E 'LOAD|GNU_RELRO|Entry point|Type'
# Two boot modes: real module first; and *archive-only* Limine handoff,
# analogous to the x86 initramfs tests. Command selection differs, kernel
# ELF loader / FS and task runtime are identical.
boot_case() {
    name=$1 mode=$2 args=$3 expected=$4 extra=${5:-}
    disk="$work/$name.img"
    config="$work/$name.conf"
    log="$work/$name.serial.log"
    clean="$work/$name.clean.log"
    truncate -s 128M "$disk"
    sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
    mformat -i "$disk@@1048576" -F ::
    mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
    mcopy -i "$disk@@1048576" "$efi" ::/EFI/BOOT/BOOTAA64.EFI
    mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
    if [ "$mode" = cpio ]; then
        mcopy -i "$disk@@1048576" "$work/rootfs.cpio" ::/boot/rootfs.cpio
        module='rootfs.cpio'
    else
        mcopy -i "$disk@@1048576" "$dir/busybox" ::/boot/busybox
        module='busybox'
    fi
    {
        printf '%s\n' 'timeout: 0' '/NaSeed ARM64 native BusyBox workload'
        printf '%s\n' '    protocol: limine' '    path: boot():/boot/kernel'
        printf '    cmdline: naseed.argv=/bin/busybox %s\n' "$args"
        printf '    module_path: boot():/boot/%s\n' "$module"
    } > "$config"
    mcopy -i "$disk@@1048576" "$config" ::/boot/limine/limine.conf
    # No BusyBox result is assumed. A failure or timeout is a real failure.
    case "$name" in ash-*) time_budget=22 ;; *) time_budget=28 ;; esac
    set +e
    timeout "${time_budget}s" qemu-system-aarch64 \
        -machine virt -cpu cortex-a57 -m 512M -smp 1 \
        -display none -serial stdio -monitor none \
        -semihosting-config enable=on,target=native -bios "$fw" \
        -drive if=none,id=esp,format=raw,file="$disk" \
        -device virtio-blk-device,drive=esp > "$log" 2>&1 < /dev/null &
    pid=$!
    sh scripts/port-boot-until.sh "$log" "$pid" "$time_budget" \
      '[INIT] creating common task/VM and architecture user entry' \
      '[PASS] cmdline userspace exit 0' \
      '[ARM64] userspace debug-exit status=0'
    monitor=$?
    kill "$pid" 2>/dev/null || :
    wait "$pid" 2>/dev/null || :
    set -e
    tr -d '\r' < "$log" > "$clean"
    if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$clean" || \
       ! grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=/bin/busybox $args" "$clean"; then
        echo "[ARM64 BUSYBOX FAIL] real $mode BusyBox $args did not cleanly exit" >&2
        tail -110 "$clean" >&2
        return 1
    fi
    if [ "$expected" != '-' ] && ! grep -Fxq "$expected" "$clean"; then
        echo "[ARM64 BUSYBOX FAIL] wrong stdout for $mode $args: expected [$expected]" >&2
        tail -110 "$clean" >&2
        return 1
    fi
    if [ "$mode" = cpio ] && ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean"; then
        echo '[ARM64 BUSYBOX FAIL] CPIO mount marker missing' >&2
        tail -110 "$clean" >&2
        return 1
    fi
    case "$extra" in
        '') ;;
        fork)
            if ! grep -Fq '[PROC] fork parent=' "$clean" ||
               ! grep -Fq '[PROC] wait4 reaped pid=' "$clean"; then
                echo "[ARM64 ASH FAIL] fork/exec/wait4 not reached: $args" >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        pipe)
            if ! grep -Fq '[PIPE] created read=' "$clean" ||
               ! grep -Fq '[PROC] wait4 reaped pid=' "$clean"; then
                echo "[ARM64 ASH FAIL] pipe/scheduler/EOF not reached: $args" >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        exec)
            if ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$clean"; then
                echo "[ARM64 ASH FAIL] syscall execve not reached: $args" >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        tmp)
            if ! grep -Fxq hello "$clean" || ! grep -Fxq removed "$clean"; then
                echo '[ARM64 ASH FAIL] tmpfs creation/remove/output incorrect' >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        *) echo "[ARM64 ASH TEST BUG] unexpected assertion: $extra" >&2
           return 1 ;;
    esac
    echo "[ACCEPTED] native ARM64 BusyBox ($mode): $args"
}
# The actual BusyBox argv, FS input and expected output are held in
# ONE architecture-independent manifest. Both CI ISA jobs consume it.
failures=0
total=0
tab=$(printf '\t')
while IFS="$tab" read -r name mode args expected extra; do
    case "$name" in ''|\#*) continue ;; esac
    if [ -n "${BUSYBOX_PARITY_CASE:-}" ] &&
       [ "$name" != "$BUSYBOX_PARITY_CASE" ]; then continue; fi
    [ "$extra" = '-' ] && extra=''
    total=$((total+1))
    if ! boot_case "$name" "$mode" "$args" "$expected" "$extra"; then
        failures=$((failures+1))
    fi
done < scripts/busybox-parity-cases.tsv
if [ -n "${BUSYBOX_PARITY_CASE:-}" ]; then test "$total" -eq 1; else test "$total" -eq 17; fi
if [ "$failures" -ne 0 ]; then
    echo "[ARM64 BUSYBOX FAIL] $failures/$total common parity fixtures failed" >&2
    exit 1
fi
echo "[PASS] native ARM64 Debian BusyBox: $total/$total canonical parity cases"

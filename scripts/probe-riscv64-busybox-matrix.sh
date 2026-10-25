#!/bin/sh
# Architecture parity regression: run *unmodified* official Debian RV64
# BusyBox through the SAME shared kernel as x86 for a growing workload matrix.
# No guest-side test-specific kernel shortcuts. Strict stdout + exit checks.
set -eu
test -s build/port-riscv64/full/kernel.elf || sh scripts/build-riscv64-full.sh
dir=build/port-riscv64/full
if [ -n "${BUSYBOX_PARITY_BINARY:-}" ]; then
    work="$dir/matrix-source"
    mkdir -p "$work"
    cp "$BUSYBOX_PARITY_BINARY" "$work/input-busybox"
    binary="$work/input-busybox"
else
    sh scripts/fetch-debian-riscv64.sh
    work="$dir/matrix"
    mkdir -p "$work"
    binary="$dir/busybox"
fi
archive=build/deps/limine-binary.tar.gz
if [ ! -s "$archive" ]; then
    curl -fL --retry 3 -o "$archive.tmp" \
        https://github.com/Limine-Bootloader/Limine/releases/latest/download/limine-binary.tar.gz
    mv "$archive.tmp" "$archive"
fi
tar -xzf "$archive" -C build/deps
efi=build/deps/limine-binary/BOOTRISCV64.EFI
test -s "$efi"
fw=/usr/share/qemu-efi-riscv64
test -s "$fw/RISCV_VIRT_CODE.fd"
# The rootfs is built only from verified native BusyBox plus actual bytes
# in the archive. The same kernel bootfs.c handles archive/file lookup.
# A non-BusyBox profile may provide its own exact CPIO archive.
# Existing BusyBox runs still stage their unchanged canonical fixture.
if [ -n "${NAS_ACCEPTANCE_CPIO:-}" ]; then
    test -s "$NAS_ACCEPTANCE_CPIO"
    cp "$NAS_ACCEPTANCE_CPIO" "$work/rootfs.cpio"
else
    sh scripts/stage-busybox-rootfs.sh "$binary" "$work"
fi
# Two boot modes: real module first; and *archive-only* Limine handoff,
# analogous to the x86 initramfs tests. Command selection differs, kernel
# ELF loader / FS and task runtime are identical.
boot_case() {
    name=$1 mode=$2 args=$3 expected=$4 extra=${5:-} entry=${6:-/bin/busybox}
    disk="$work/$name.img"
    config="$work/$name.conf"
    log="$work/$name.serial.log"
    clean="$work/$name.clean.log"
    truncate -s "${NAS_ACCEPTANCE_DISK_MB:-128}M" "$disk"
    sgdisk --clear --new 1:2048:0 --typecode 1:ef00 "$disk" >/dev/null
    mformat -i "$disk@@1048576" -F ::
    mmd -i "$disk@@1048576" ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine
    mcopy -i "$disk@@1048576" "$efi" ::/EFI/BOOT/BOOTRISCV64.EFI
    mcopy -i "$disk@@1048576" "$dir/kernel.elf" ::/boot/kernel
    if [ "$mode" = cpio ]; then
        mcopy -i "$disk@@1048576" "$work/rootfs.cpio" ::/boot/rootfs.cpio
        module='rootfs.cpio'
    else
        mcopy -i "$disk@@1048576" "$binary" ::/boot/busybox
        module='busybox'
    fi
    {
        printf '%s\n' 'timeout: 0' '/NaSeed RV64 native BusyBox workload'
        printf '%s\n' '    protocol: limine' '    path: boot():/boot/kernel'
        # Trusted boot UTC is a real host clock, not a synthesized
        # application timestamp. Keep argv and kernel semantics unchanged.
        boot_utc=${NAS_ACCEPTANCE_REALTIME_EPOCH:-$(date -u +%s)}
        case "$boot_utc" in ''|*[!0-9]*) echo '[BOOT CLOCK FAIL] nonnumeric UTC' >&2; return 1 ;; esac
        printf '    cmdline: naseed.realtime=%s naseed.argv=%s %s\n' \
            "$boot_utc" "$entry" "$args"
        printf '    module_path: boot():/boot/%s\n' "$module"
    } > "$config"
    mcopy -i "$disk@@1048576" "$config" ::/boot/limine/limine.conf
    cp "$fw/RISCV_VIRT_VARS.fd" "$work/$name.vars.fd"
    # 9s is the established firmware+kernel runtime. Bound unexpected
    # shell deadlocks to 30s per scenario so the *whole class* of five
    # shell tests produces diagnostics within the 10-minute CI budget.
    # Ordinary applet/cpio cases have a more generous 45s cap.
    case "$name" in
        ash-*) time_budget=30 ;;
        timer-preempt) time_budget=45 ;;
        process-capacity) time_budget=65 ;;
        init-reaper) time_budget=130 ;;
        m38-gcc|m49-capacity) time_budget=360 ;;
        m39-packages|m40-archives|m41-unix|m42-pthread|m43-zstd|m44-group|m45-robust|m46-exec|m47-tgkill|m48-parallel|m51-git|m52-python|m53-vim|m54-dev) time_budget=540 ;;
        m55-cjson) time_budget=${NAS_M55_QEMU_TIMEOUT:-2400} ;;
        m37-incremental) time_budget=150 ;;
        *) time_budget=45 ;;
    esac
    set +e
    timeout "${time_budget}s" qemu-system-riscv64 \
        -machine virt,acpi=off -m "${NAS_ACCEPTANCE_QEMU_RAM:-512M}" -smp 1 \
        -display none -serial stdio -monitor none \
        -drive if=pflash,format=raw,unit=0,readonly=on,file="$fw/RISCV_VIRT_CODE.fd" \
        -drive if=pflash,format=raw,unit=1,file="$work/$name.vars.fd" \
        -drive if=none,id=esp,format=raw,file="$disk" \
        -device virtio-blk-device,drive=esp > "$log" 2>&1 < /dev/null &
    pid=$!
    if [ "$name" = timer-preempt ]; then
        sh scripts/port-boot-until.sh "$log" "$pid" "$time_budget" \
          '[INIT] creating common task/VM and architecture user entry' \
          '[M28] timer-preempt from=' 'cpu-hog-did-not-starve-ash'
    else
        sh scripts/port-boot-until.sh "$log" "$pid" "$time_budget" \
          '[INIT] creating common task/VM and architecture user entry' \
          '[PASS] cmdline userspace exit 0' \
          '[RV64] userspace debug-exit status=0'
    fi
    monitor=$?
    kill "$pid" 2>/dev/null || :
    wait "$pid" 2>/dev/null || :
    set -e
    tr -d '\r' < "$log" > "$clean"
    if [ "$monitor" -ne 0 ] || grep -Fq '[FAIL]' "$clean" || \
       ! grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=$entry $args" "$clean"; then
        echo "[RV64 BUSYBOX FAIL] real $mode BusyBox $args did not cleanly exit" >&2
        tail -110 "$clean" >&2
        return 1
    fi
    if [ "$expected" != '-' ] && ! grep -Fxq "$expected" "$clean"; then
        echo "[RV64 BUSYBOX FAIL] wrong stdout for $mode $args: expected [$expected]" >&2
        tail -110 "$clean" >&2
        return 1
    fi
    if [ "$mode" = cpio ] && ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean"; then
        echo '[RV64 BUSYBOX FAIL] CPIO mount marker missing' >&2
        tail -110 "$clean" >&2
        return 1
    fi
    if [ "$name" = init-reaper ]; then
        sh scripts/check-m13-init-log.sh "$clean" || return 1
    fi
    case "$extra" in
        '') ;;
        fork)
            if ! grep -Fq '[PROC] fork parent=' "$clean" ||
               ! grep -Fq '[PROC] wait4 reaped pid=' "$clean"; then
                echo "[RV64 ASH FAIL] fork/exec/wait4 not reached: $args" >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        pipe)
            if ! grep -Fq '[PIPE] created read=' "$clean" ||
               ! grep -Fq '[PROC] wait4 reaped pid=' "$clean"; then
                echo "[RV64 ASH FAIL] pipe/scheduler/EOF not reached: $args" >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        exec)
            if ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$clean"; then
                echo "[RV64 ASH FAIL] syscall execve not reached: $args" >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        tmp)
            if ! grep -Fxq hello "$clean" || ! grep -Fxq removed "$clean"; then
                echo '[RV64 ASH FAIL] tmpfs creation/remove/output incorrect' >&2
                tail -110 "$clean" >&2
                return 1
            fi ;;
        *) echo "[RV64 ASH TEST BUG] unexpected assertion: $extra" >&2
           return 1 ;;
    esac
    echo "[ACCEPTED] native RV64 BusyBox ($mode): $args"
}
# The actual BusyBox argv, FS input and expected output are held in
# ONE architecture-independent manifest. Both CI ISA jobs consume it.
failures=0
total=0
tab=$(printf '\t')
while IFS="$tab" read -r name mode args expected extra entry; do
    case "$name" in ''|\#*) continue ;; esac
    if [ -n "${BUSYBOX_PARITY_CASE:-}" ] &&
       [ "$name" != "$BUSYBOX_PARITY_CASE" ]; then continue; fi
    [ "$extra" = '-' ] && extra=''
    total=$((total+1))
    if ! boot_case "$name" "$mode" "$args" "$expected" "$extra" "$entry"; then
        failures=$((failures+1))
    fi
done < "${BUSYBOX_PARITY_MANIFEST:-scripts/busybox-parity-cases.tsv}"
if [ -n "${BUSYBOX_PARITY_CASE:-}" ]; then
    test "$total" -eq 1
else
    test "$total" -eq "${BUSYBOX_PARITY_EXPECTED_COUNT:-17}"
fi
if [ "$failures" -ne 0 ]; then
    echo "[RV64 BUSYBOX FAIL] $failures/$total common parity fixtures failed" >&2
    exit 1
fi
echo '[PASS] native RV64 Debian BusyBox: ALL 17 canonical parity cases'

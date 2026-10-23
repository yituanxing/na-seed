#!/bin/sh
# Same pinned Debian BusyBox 1.37.0, same commands/CPIO bytes as RV64.
# Alpine dynamic/musl is intentionally a separate test profile.
set -eu
make kernel build/deps/limine-binary/BOOTX64.EFI
if [ -n "${BUSYBOX_PARITY_BINARY:-}" ]; then
    dir=build/busybox-source-parity-x86
    mkdir -p "$dir"
    cp "$BUSYBOX_PARITY_BINARY" "$dir/busybox"
else
    dir=build/busybox-parity-x86
    mkdir -p "$dir"
    sh scripts/fetch-busybox-fixture.sh amd64 "$dir/busybox"
fi
# A non-BusyBox profile may provide its own exact CPIO archive.
# Existing BusyBox runs still stage their unchanged canonical fixture.
if [ -n "${NAS_ACCEPTANCE_CPIO:-}" ]; then
    test -s "$NAS_ACCEPTANCE_CPIO"
    cp "$NAS_ACCEPTANCE_CPIO" "$dir/rootfs.cpio"
else
    sh scripts/stage-busybox-rootfs.sh "$dir/busybox" "$dir"
fi
efi=build/deps/limine-binary/BOOTX64.EFI
cdrom=build/deps/limine-binary/limine-uefi-cd.bin
test -s "$efi" && test -s "$cdrom"
run_case() {
    name=$1 mode=$2 args=$3 expected=$4 evidence=$5 entry=${6:-/bin/busybox}
    stage="$dir/$name-stage"
    iso="$dir/$name.iso"
    log="$dir/$name.serial.log"
    clean="$dir/$name.clean.log"
    mkdir -p "$stage/boot/limine" "$stage/EFI/BOOT"
    cp build/kernel.elf "$stage/boot/kernel"
    cp "$efi" "$stage/EFI/BOOT/BOOTX64.EFI"
    cp "$cdrom" "$stage/boot/limine/"
    if [ "$mode" = cpio ]; then
        cp "$dir/rootfs.cpio" "$stage/boot/rootfs.cpio"
        module=rootfs.cpio
    else
        cp "$dir/busybox" "$stage/boot/busybox"
        module=busybox
    fi
    {
        printf '%s\n' 'timeout: 0' '/NaSeed x86 Debian BusyBox parity'
        printf '%s\n' '    protocol: limine' '    path: boot():/boot/kernel'
        printf '    cmdline: naseed.argv=%s %s\n' "$entry" "$args"
        printf '    module_path: boot():/boot/%s\n' "$module"
    } > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    case "$name" in m37-incremental) budget=100 ;; process-capacity) budget=45 ;; init-reaper) budget=110 ;; *) budget=15 ;; esac
    timeout "${budget}s" sh scripts/run.sh "$iso" > "$log" 2>&1 < /dev/null || rc=$?
    tr -d '\r' < "$log" > "$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq "[CMDLINE] userspace argv from Limine: naseed.argv=$entry $args" "$clean" ||
       ! grep -Fq '[EXIT] user status=0' "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       grep -Fq '[FAIL]' "$clean"; then
        echo "[X86 PARITY FAIL] exit/argv/boot for $mode: $args (QEMU rc=$rc)" >&2
        tail -100 "$clean" >&2
        return 1
    fi
    if [ "$expected" != '-' ] && ! grep -Fxq "$expected" "$clean"; then
        echo "[X86 PARITY FAIL] expected stdout [$expected]: $args" >&2
        tail -100 "$clean" >&2
        return 1
    fi
    if [ "$mode" = cpio ] &&
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean"; then
        echo "[X86 PARITY FAIL] CPIO mount missing: $args" >&2
        tail -100 "$clean" >&2
        return 1
    fi
    if [ "$name" = init-reaper ]; then
        sh scripts/check-m13-init-log.sh "$clean" || return 1
    fi
    case "$evidence" in
        -) ;;
        exec) grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$clean" || return 1 ;;
        fork)
            grep -Fq '[PROC] fork parent=' "$clean" || return 1
            grep -Fq '[PROC] wait4 reaped pid=' "$clean" || return 1 ;;
        pipe)
            grep -Fq '[PIPE] created read=' "$clean" || return 1
            grep -Fq '[PROC] wait4 reaped pid=' "$clean" || return 1 ;;
        tmp)
            grep -Fxq hello "$clean" || return 1
            grep -Fxq removed "$clean" || return 1 ;;
        *) echo "[X86 PARITY] invalid evidence: $evidence" >&2; return 1 ;;
    esac
    echo "[ACCEPTED] x86 Debian BusyBox ($mode): $args"
}
failures=0
count=0
tab=$(printf '\t')
while IFS="$tab" read -r id mode args expected evidence entry; do
    case "$id" in ''|\#*) continue ;; esac
    if [ -n "${BUSYBOX_PARITY_CASE:-}" ] && [ "$id" != "$BUSYBOX_PARITY_CASE" ]; then continue; fi
    count=$((count+1))
    if ! run_case "$id" "$mode" "$args" "$expected" "$evidence" "$entry"; then failures=$((failures+1)); fi
done < "${BUSYBOX_PARITY_MANIFEST:-scripts/busybox-parity-cases.tsv}"
if [ -n "${BUSYBOX_PARITY_CASE:-}" ]; then
    test "$count" -eq 1
else
    test "$count" -eq "${BUSYBOX_PARITY_EXPECTED_COUNT:-17}"
fi
if [ "$failures" -ne 0 ]; then
    echo "[X86 PARITY FAIL] $failures/$count Debian static cases failed" >&2
    exit 1
fi
echo "[PASS] x86 Debian static BusyBox: $count/$count canonical parity cases"

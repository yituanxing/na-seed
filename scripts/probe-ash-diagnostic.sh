#!/bin/sh
# Discover missing *classes* of Linux process/shell semantics from the
# unmodified Alpine BusyBox ash on an archive-only NaSeed boot.
# Strict output acceptance for supported builtins; missing process features
# are recorded as explicit blockers, NEVER as accepted capabilities.
set -eu
base=build/alpine-archive-iso
for scenario in builtin builtinpwd pid cd readfile cdread readtwice redirect applet external pipeline; do
    case "$scenario" in
        builtin) args="sh -c 'echo shell-ready'" ;;
        builtinpwd) args="sh -c 'pwd'" ;;
        pid) args="sh -c 'echo $(printf '\044\044')'" ;;
        cd) args="sh -c 'cd /etc; pwd'" ;;
        readfile) args="sh -c 'read word < /etc/hostname; echo \$word'" ;;
        cdread) args="sh -c 'cd /etc; read word < hostname; echo \$word'" ;;
        readtwice) args="sh -c 'exec 3< /etc/hostname; read first <&3; read second <&3; echo \$first:\$second'" ;;
        redirect) args="sh -c 'echo hello > /dev/null; echo redirect-ok'" ;;
        applet) args="sh -c 'cat /etc/hostname'" ;;
        external) args="sh -c '/bin/busybox cat /etc/hostname'" ;;
        pipeline) args="sh -c 'echo abc | wc -c'" ;;
    esac
    stage="build/ash-diag-$scenario-iso"
    iso="build/naseed-ash-diag-$scenario.iso"
    log="build/ash-diag-$scenario-serial.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    awk -v args="$args" '
        /^    cmdline: / {
            print "    cmdline: naseed.argv=/bin/busybox " args
            next
        }
        { print }
    ' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1
    rc=0
    timeout 4s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    echo "[ASH-DIAG] case=$scenario qemu_status=$rc args=$args"
    tr -d '\r' < "$log" | grep -E '^\[(MISS|FAULT|FAIL|EXIT|PASS)\]|^(shell-ready|naseed|redirect-ok|/etc|/|4)$' || true
    if ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"; then
        echo "[ASH-DIAG-FAIL] missing actual archive mount" >&2
        exit 1
    fi
    case "$scenario" in
        builtin) expected='shell-ready' ;;
        builtinpwd) expected='/' ;;
        pid) expected='1' ;;
        cd) expected='/etc' ;;
        readfile|cdread) expected='naseed' ;;
        readtwice) expected='naseed:' ;;
        external) expected='naseed' ;;
        redirect) expected='redirect-ok' ;;
        *) expected='' ;;
    esac
    if [ -n "$expected" ]; then
        if [ "$rc" -ne 1 ] ||
           ! tr -d '\r' < "$log" | grep -Fxq "$expected" ||
           ! grep -Fq '[EXIT] user status=0' "$log" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$log"; then
            echo "[ASH-FAIL] real shell did not pass scenario=$scenario" >&2
            tail -85 "$log" >&2
            exit 1
        fi
        if [ "$scenario" = redirect ] &&
           tr -d '\r' < "$log" | grep -Fxq hello; then
            echo '[ASH-FAIL] redirected hello incorrectly reached stdout' >&2
            exit 1
        fi
        if [ "$scenario" = external ] &&
           ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$log"; then
            echo '[ASH-FAIL] external image was not replaced' >&2
            exit 1
        fi
        echo "[ACCEPTED] unmodified Alpine ash scenario=$scenario"
    else
        case "$scenario" in
            applet)
                blocker='[EXIT] user status=127'
                reason='PATH applet entry /bin/cat absent (no symlinks yet)' ;;
            pipeline)
                blocker='[MISS] syscall nr=22'
                reason='pipe + process semantics not implemented' ;;
        esac
        if grep -Fq '[PASS] cmdline userspace exit 0' "$log" ||
           ! grep -Fq "$blocker" "$log"; then
            echo "[ASH-FAIL] expected blocker changed for scenario=$scenario" >&2
            tail -85 "$log" >&2
            exit 1
        fi
        echo "[BLOCKED] scenario=$scenario, observed $reason"
    fi
done
" ;;
        cd) args="sh -c 'cd /etc; pwd'" ;;
        readfile) args="sh -c 'read word < /etc/hostname; echo \$word'" ;;
        cdread) args="sh -c 'cd /etc; read word < hostname; echo \$word'" ;;
        readtwice) args="sh -c 'exec 3< /etc/hostname; read first <&3; read second <&3; echo \$first:\$second'" ;;
        redirect) args="sh -c 'echo hello > /dev/null; echo redirect-ok'" ;;
        applet) args="sh -c 'cat /etc/hostname'" ;;
        external) args="sh -c '/bin/busybox cat /etc/hostname'" ;;
        pipeline) args="sh -c 'echo abc | wc -c'" ;;
    esac
    stage="build/ash-diag-$scenario-iso"
    iso="build/naseed-ash-diag-$scenario.iso"
    log="build/ash-diag-$scenario-serial.log"
    rm -rf "$stage"
    cp -R "$base" "$stage"
    awk -v args="$args" '
        /^    cmdline: / {
            print "    cmdline: naseed.argv=/bin/busybox " args
            next
        }
        { print }
    ' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" > /dev/null 2>&1
    rc=0
    timeout 4s sh scripts/run.sh "$iso" > "$log" 2>&1 || rc=$?
    echo "[ASH-DIAG] case=$scenario qemu_status=$rc args=$args"
    tr -d '\r' < "$log" | grep -E '^\[(MISS|FAULT|FAIL|EXIT|PASS)\]|^(shell-ready|naseed|redirect-ok|/etc|/|4)$' || true
    if ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$log"; then
        echo "[ASH-DIAG-FAIL] missing actual archive mount" >&2
        exit 1
    fi
    case "$scenario" in
        builtin) expected='shell-ready' ;;
        builtinpwd) expected='/' ;;
        pid) expected='1' ;;
        cd) expected='/etc' ;;
        readfile|cdread) expected='naseed' ;;
        readtwice) expected='naseed:' ;;
        external) expected='naseed' ;;
        redirect) expected='redirect-ok' ;;
        *) expected='' ;;
    esac
    if [ -n "$expected" ]; then
        if [ "$rc" -ne 1 ] ||
           ! tr -d '\r' < "$log" | grep -Fxq "$expected" ||
           ! grep -Fq '[EXIT] user status=0' "$log" ||
           ! grep -Fq '[PASS] cmdline userspace exit 0' "$log"; then
            echo "[ASH-FAIL] real shell did not pass scenario=$scenario" >&2
            tail -85 "$log" >&2
            exit 1
        fi
        if [ "$scenario" = redirect ] &&
           tr -d '\r' < "$log" | grep -Fxq hello; then
            echo '[ASH-FAIL] redirected hello incorrectly reached stdout' >&2
            exit 1
        fi
        if [ "$scenario" = external ] &&
           ! grep -Fq '[EXEC] replaced process with CPIO ELF: /bin/busybox' "$log"; then
            echo '[ASH-FAIL] external image was not replaced' >&2
            exit 1
        fi
        echo "[ACCEPTED] unmodified Alpine ash scenario=$scenario"
    else
        case "$scenario" in
            applet)
                blocker='[EXIT] user status=127'
                reason='PATH applet entry /bin/cat absent (no symlinks yet)' ;;
            pipeline)
                blocker='[MISS] syscall nr=22'
                reason='pipe + process semantics not implemented' ;;
        esac
        if grep -Fq '[PASS] cmdline userspace exit 0' "$log" ||
           ! grep -Fq "$blocker" "$log"; then
            echo "[ASH-FAIL] expected blocker changed for scenario=$scenario" >&2
            tail -85 "$log" >&2
            exit 1
        fi
        echo "[BLOCKED] scenario=$scenario, observed $reason"
    fi
done

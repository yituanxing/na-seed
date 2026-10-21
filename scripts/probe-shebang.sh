#!/bin/sh
# Native Linux execve(2) must launch the real musl BusyBox #! interpreter.
# The direct ELF has no userspace fallback on ENOEXEC; the other two cases
# invoke executable scripts through the original Alpine BusyBox ash applet.
set -eu
for scenario in direct normal workflow; do
    case "$scenario" in
        direct)
            args='/bin/shebang-abi'
            output='script-native:token'
            marker='script-success'
            interpreter='/bin/busybox' ;;
        normal)
            args="/bin/busybox sh -c '/etc/naseed-normal.sh token'"
            output='normal-native:token'
            marker='normal-native:token'
            interpreter='/bin/sh' ;;
        workflow)
            args="/bin/busybox sh -c '/etc/naseed-flow.sh'"
            output='flow-success'
            marker='source'
            interpreter='/bin/sh' ;;
    esac
    stage="build/ash-shebang-$scenario-iso"
    iso="build/naseed-ash-shebang-$scenario.iso"
    log="build/ash-shebang-$scenario-serial.log"
    clean="build/ash-shebang-$scenario-clean.log"
    rm -rf "$stage"
    cp -R build/alpine-archive-iso "$stage"
    cp build/shebang-abi-rootfs.cpio "$stage/boot/rootfs.cpio"
    awk -v args="$args" '/^    cmdline: / {
        print "    cmdline: naseed.argv=" args
        next
    } { print }' limine-alpine-archive.conf > "$stage/boot/limine/limine.conf"
    xorriso -as mkisofs -R -r -J \
      --efi-boot boot/limine/limine-uefi-cd.bin \
      --efi-boot-part --efi-boot-image -o "$iso" "$stage" >/dev/null 2>&1
    rc=0
    timeout 32s sh scripts/run.sh "$iso" >"$log" 2>&1 || rc=$?
    tr -d '\r' <"$log" >"$clean"
    if [ "$rc" -ne 1 ] ||
       ! grep -Fq '[FS] CPIO newc read-only archive mounted' "$clean" ||
       ! grep -Fq "[EXEC] CPIO #! interpreter: $interpreter" "$clean" ||
       ! grep -Fxq "$output" "$clean" ||
       ! grep -Fxq "$marker" "$clean" ||
       ! grep -Fq '[PASS] cmdline userspace exit 0' "$clean" ||
       grep -Fq '[FAULT]' "$clean" ||
       grep -Fq '[DEADLOCK]' "$clean"; then
        echo "[FAIL] native original BusyBox #! interpreter scenario=$scenario rc=$rc" >&2
        tail -120 "$clean" >&2
        exit 1
    fi
    if [ "$scenario" = direct ]; then
        grep -Fxq '[USER] checked execve negative shebang errno' "$clean" &&
        grep -Fxq 6 "$clean" || {
            echo '[FAIL] direct execve errno checks or actual script pipe output' >&2
            exit 1
        }
    fi
    if [ "$scenario" = workflow ]; then
        grep -Fxq 7 "$clean" || {
            echo '[FAIL] real script cp/mv/pipe byte count not observed' >&2
            exit 1
        }
    fi
    echo "[ACCEPTED] Linux #! execve into original BusyBox scenario=$scenario"
done

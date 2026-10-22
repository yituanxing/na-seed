#!/bin/sh
# Fast host preflight. No network, no QEMU, no kernel compilation.
set -eu
config=scripts/source-busybox.config
cases=scripts/source-busybox-cases.tsv
test -s "$config" && test -s "$cases"
[ "$(grep -c '^CONFIG_' "$config")" -ge 15 ]
[ "$(grep '^CONFIG_' "$config" | cut -d= -f1 | sort -u | wc -l)" -eq "$(grep -c '^CONFIG_' "$config")" ]
for applet in ASH SH_IS_ASH ECHO CAT LS WC MKDIR RMDIR RM STATIC; do
    grep -Fxq "CONFIG_$applet=y" "$config"
done
[ "$(grep -c -v '^#' "$cases")" -eq 8 ]
# BusyBox Kbuild requires a pre-existing O= directory even for allnoconfig.
grep -Fq '"$target" "$obj"' scripts/build-source-busybox.sh
grep -Fq 'sha256sum -c -' scripts/build-source-busybox.sh
grep -Fq 'BUSYBOX_EXTRA_DIR' scripts/stage-busybox-rootfs.sh
grep -Fq 'BUSYBOX_EXTRA_DIR' scripts/probe-source-busybox.sh
grep -Fq '#define NAS_TASK_LIMIT 16' kernel/task.h
# A single last command is tail-exec'd by ash, so it does NOT occupy a
# second task slot; the final echo forces test execution as PID 2.
grep -Fq 'm12-process --naseed-limit; echo process-parent-done' "$cases"
for id in echo ash-builtin ash-fork ash-pipe ash-tmp source-lifecycle vm-lifetime process-capacity; do
    [ "$(grep -c "^$id$(printf '\t')" "$cases")" -eq 1 ]
done
for filename in scripts/build-source-busybox.sh scripts/probe-source-busybox.sh \
                scripts/check-source-busybox.sh scripts/build-m11-vm-test.sh \
                scripts/build-m12-process-test.sh; do
    sh -n "$filename"
done
# Both 17-case and source-profile runners must consume the same manifest
# interface; no forked VM/ELF/syscall policy or private test binaries.
for f in scripts/probe-x86-busybox-parity.sh \
         scripts/probe-riscv64-busybox-matrix.sh \
         scripts/probe-aarch64-busybox-matrix.sh; do
    grep -Fq 'BUSYBOX_PARITY_MANIFEST' "$f"
    grep -Fq 'BUSYBOX_PARITY_BINARY' "$f"
done
echo '[PASS] controlled-source BusyBox config, eight-case manifest and shared QEMU runners'

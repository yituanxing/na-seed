#!/bin/sh
# Fail before QEMU if a new full-kernel port silently diverges in common TUs.
set -eu
manifest=scripts/full-kernel-core.sources
test -s "$manifest"
count=0
seen=' '
while IFS= read -r path || [ -n "$path" ]; do
    case "$path" in ''|\#*) continue ;; esac
    case "$path" in
        kernel/arch/*|kernel/platform/*)
            echo "[PORT SOURCES] hardware provider in shared manifest: $path" >&2
            exit 1 ;;
        kernel/*.c|kernel/*/*.c) ;;
        *) echo "[PORT SOURCES] invalid core path: $path" >&2; exit 1 ;;
    esac
    test -f "$path" || { echo "[PORT SOURCES] missing source: $path" >&2; exit 1; }
    case "$seen" in
        *" $path "*) echo "[PORT SOURCES] duplicate source: $path" >&2; exit 1 ;;
    esac
    seen="$seen$path "
    count=$((count+1))
done < "$manifest"
test "$count" -eq 47 || {
    echo "[PORT SOURCES] expected 47 common TUs, got $count" >&2
    exit 1
}
for script in scripts/build-riscv64-full.sh scripts/build-aarch64-full.sh; do
    grep -Fq 'scripts/full-kernel-core.sources' "$script" || {
        echo "[PORT SOURCES] $script bypasses canonical manifest" >&2
        exit 1
    }
done
# The common source manifest contains 46 TUs; signal-frame native
# providers are architecture units, not duplicated copies of shared policy.
for arch in riscv64 aarch64; do
    script="scripts/build-$arch-full.sh"
    provider="kernel/arch/$arch/signal_frame.c"
    test -f "$provider" || { echo "[PORT SOURCES] missing $provider" >&2; exit 1; }
    grep -Fq "$provider" "$script" || {
        echo "[PORT SOURCES] $script omits native signal frame provider" >&2
        exit 1
    }
done
grep -Fq '$(OUT)/arch_signal_frame.o' Makefile || {
    echo "[PORT SOURCES] x86 native signal frame not linked" >&2; exit 1;
}
echo "[PASS] three native signal-frame backends; both full-kernel ports share $count core TUs"

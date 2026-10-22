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
test "$count" -eq 22 || {
    echo "[PORT SOURCES] expected 22 common TUs, got $count" >&2
    exit 1
}
for script in scripts/build-riscv64-full.sh scripts/build-aarch64-full.sh; do
    grep -Fq 'scripts/full-kernel-core.sources' "$script" || {
        echo "[PORT SOURCES] $script bypasses canonical manifest" >&2
        exit 1
    }
done
echo "[PASS] both full-kernel ISA builds share $count core TUs"

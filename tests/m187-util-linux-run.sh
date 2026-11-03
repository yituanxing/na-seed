#!/bin/sh
# Guest loader/CLI entry ONLY for the 26 independent signed util-linux ELFs.
# Do not claim semantics of unsupported mount/namespace/IRQ/block operations.
set -eu
export PATH=/usr/sbin:/sbin:/usr/bin:/bin
work=/tmp/m187-util-linux
/bin/busybox mkdir -p "$work"
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    count=$((count + 1))
    result=0
    /bin/busybox timeout 8 "/$elf" --help > "$work/entry-$count" 2>&1 || result=$?
    # SIGSEGV / exec failures / timeout cannot pass as successful entry.
    [ "$result" -lt 124 ] && [ -s "$work/entry-$count" ] || {
        echo "[M187 SIGNED ENTRY FAIL] $elf exit=$result" >&2
        exit 1
    }
done < /tests/m187-original-elfs.txt
[ "$count" -eq 26 ]
echo M187_SOFTWARE_26_NEW_SIGNED_ELF_ENTRY_ONLY_PASS

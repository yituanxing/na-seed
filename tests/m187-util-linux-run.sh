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
    # --help is a finite, side-effect-free loader/CLI probe. Execute the
    # original ELF directly: BusyBox timeout daemonizes a separate watchdog
    # for EACH invocation, so a batch of 26 can exhaust our bounded task
    # table with simultaneous watchdogs even after their probes finish.
    # The host QEMU run owns the batch-wide timeout, and probe-m174-batch
    # requires an actual kernel [EXEC] record for EVERY target path.
    echo "[M187 SIGNED ENTRY START] $elf"
    "/$elf" --help > "$work/entry-$count" 2>&1 || result=$?
    # SIGSEGV / failed exec cannot pass as a successful entry.
    [ "$result" -lt 124 ] && [ -s "$work/entry-$count" ] || {
        echo "[M187 SIGNED ENTRY FAIL] $elf exit=$result" >&2
        exit 1
    }
    echo "[M187 SIGNED ENTRY OK] $elf exit=$result"
done < /tests/m187-original-elfs.txt
[ "$count" -eq 26 ]
echo M187_SOFTWARE_26_NEW_SIGNED_ELF_ENTRY_ONLY_PASS

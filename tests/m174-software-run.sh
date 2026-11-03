#!/bin/sh
# Independent signed Alpine programs on real NaSeed guest, not host emulation.
# This deliberately does NOT claim that all twenty have run functional cases.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }
work=/tmp/m174-software
busy mkdir -p "$work"

# Actually enter all 20 original binaries with their help path.  A displayed
# version is only a loader/argument smoke, not proof of subsystem semantics.
# The host already enforced original signed per-ISA ELF and distinct SHA256.
entry_count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    entry_count=$((entry_count+1))
    code=0
    busy timeout 7 "/$elf" --help > "$work/entry-$entry_count" 2>&1 || code=$?
    # 124 means timed out, 126/127 failed exec, >=128 usually a signal.
    [ "$code" -lt 124 ] && [ -s "$work/entry-$entry_count" ] || {
        echo "[M174 SOFTWARE ENTRY FAIL] $elf exit=$code" >&2
        exit 1
    }
done < /tests/m174-software-original-elfs.txt
[ "$entry_count" -eq 20 ]
echo M174_SOFTWARE_20_ENTRY_ONLY_PASS

# Actual D-Bus algorithm/output, beyond an ELF loader or --version smoke.
busy timeout 15 /usr/bin/dbus-uuidgen > "$work/dbus-uuid" 2> "$work/dbus-uuid-error"
[ "$(busy wc -c < "$work/dbus-uuid")" -eq 33 ]
busy grep -Eq '^[[:xdigit:]]{32}$' "$work/dbus-uuid"
echo M174_SOFTWARE_DBUS_UUIDGEN_FUNCTION_PASS

# Actual sysstat parsing of the kernel's current /proc/stat CPU counters.
busy grep -q '^cpu ' /proc/stat
busy timeout 15 /bin/mpstat 1 1 > "$work/mpstat" 2> "$work/mpstat-error"
busy grep -Eq '(^|[[:space:]])all([[:space:]]|$)' "$work/mpstat"
echo M174_SOFTWARE_MPSTAT_PROC_FUNCTION_PASS

# Original ELF entry/version exercises ONLY, never watch/TUI semantics.
busy timeout 15 /usr/bin/inotifywait --version > "$work/inotify-version" 2>&1
busy grep -iq 'inotify' "$work/inotify-version"
echo M174_SOFTWARE_INOTIFY_ENTRY_ONLY_PASS
busy timeout 15 /usr/bin/tig --version > "$work/tig-version" 2>&1
busy grep -iq 'tig' "$work/tig-version"
echo M174_SOFTWARE_TIG_ENTRY_ONLY_PASS

# inotify/fanotify event delivery and D-Bus session IPC remain separate strict
# functional acceptance tests; no success marker is emitted for either here.
echo M174_SOFTWARE_REPRESENTATIVE_FUNCTION_PASS

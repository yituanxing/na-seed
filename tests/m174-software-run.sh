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

# M184: real D-Bus machine-ID file semantics, independently of the D-Bus
# daemon/session ABI. The CPIO archive is read-only; /tmp is the existing
# writable tmpfs already used by this same signed-program acceptance script.
# Upstream dbus-uuidgen(1): --ensure=FILE creates/validates without stdout,
# --get=FILE reads that exact file, and a second ensure must not replace it.
machine_id="$work/dbus-machine-id"
busy timeout 15 /usr/bin/dbus-uuidgen --ensure="$machine_id" \
    > "$work/dbus-ensure-output" 2> "$work/dbus-ensure-error"
[ ! -s "$work/dbus-ensure-output" ]
[ "$(busy wc -c < "$machine_id")" -eq 33 ]
busy grep -Eq '^[[:xdigit:]]{32}$' "$machine_id"
busy cp "$machine_id" "$work/dbus-machine-id-original"
busy timeout 15 /usr/bin/dbus-uuidgen --get="$machine_id" \
    > "$work/dbus-get-output" 2> "$work/dbus-get-error"
busy cmp -s "$machine_id" "$work/dbus-get-output"
busy timeout 15 /usr/bin/dbus-uuidgen --ensure="$machine_id" \
    > "$work/dbus-reensure-output" 2> "$work/dbus-reensure-error"
[ ! -s "$work/dbus-reensure-output" ]
busy cmp -s "$machine_id" "$work/dbus-machine-id-original"
# A malformed existing ID is not silently overwritten by --ensure=FILE.
busy printf 'malformed-dbus-machine-id\n' > "$work/dbus-malformed-id"
bad_rc=0
busy timeout 15 /usr/bin/dbus-uuidgen --ensure="$work/dbus-malformed-id" \
    > "$work/dbus-reject-output" 2>&1 || bad_rc=$?
[ "$bad_rc" -gt 0 ] && [ "$bad_rc" -lt 124 ]
busy grep -Fxq 'malformed-dbus-machine-id' "$work/dbus-malformed-id"
echo M184_SOFTWARE_DBUS_UUID_FILE_FUNCTION_PASS

# Actual sysstat parsing of the kernel's current /proc/stat CPU counters.
busy grep -q '^cpu ' /proc/stat
busy timeout 15 /bin/mpstat 1 1 > "$work/mpstat" 2> "$work/mpstat-error"
busy grep -Eq '(^|[[:space:]])all([[:space:]]|$)' "$work/mpstat"
echo M174_SOFTWARE_MPSTAT_PROC_FUNCTION_PASS

# Original ELF entry/version exercises ONLY, never watch/TUI semantics.
# Alpine inotifywait's --version can exit 1 after printing its version/usage
# on a kernel without inotify. It is not an inotify-watch functional check:
# require entry/diagnostic text and reject exec errors, signals and timeout.
code=0
busy timeout 15 /usr/bin/inotifywait --version > "$work/inotify-version" 2>&1 || code=$?
[ "$code" -lt 124 ] && [ -s "$work/inotify-version" ]
busy grep -iq 'inotify' "$work/inotify-version"
echo M174_SOFTWARE_INOTIFY_ENTRY_ONLY_PASS
code=0
busy timeout 15 /usr/bin/tig --version > "$work/tig-version" 2>&1 || code=$?
[ "$code" -lt 124 ] && [ -s "$work/tig-version" ]
busy grep -iq 'tig' "$work/tig-version"
echo M174_SOFTWARE_TIG_ENTRY_ONLY_PASS

# inotify/fanotify event delivery and D-Bus session IPC remain separate strict
# functional acceptance tests; no success marker is emitted for either here.
echo M174_SOFTWARE_REPRESENTATIVE_FUNCTION_PASS

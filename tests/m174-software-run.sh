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

# M185: independent signed sysstat iostat ELF, in CPU-only mode. NaSeed
# exports actual monotonic /proc/stat task accounting; it does not yet export
# /proc/diskstats, so claiming a device-I/O iostat test would be dishonest.
# Two observations at 1s spacing must each contain six parsed numeric CPU
# percentages. This tests real /proc ingestion and the interval timer path,
# not just "--help" or a fixed stdout marker.
busy grep -q '^cpu ' /proc/stat
busy timeout 15 /bin/iostat -c 1 2 > "$work/iostat-cpu" 2> "$work/iostat-error"
busy awk '
    /^avg-cpu:/ { reports++; await_data=1; next }
    await_data && /%user/ { next }
    await_data && NF>=6 {
        for (i=1; i<=6; i++) {
            if ($i !~ /^[0-9]+([.][0-9]+)?$/) exit 1
            if ($i+0 < 0 || $i+0 > 100.5) exit 1
        }
        samples++
        await_data=0
    }
    END { if (reports!=2 || samples!=2) exit 1 }
' "$work/iostat-cpu"
echo M185_SOFTWARE_IOSTAT_CPU_FUNCTION_PASS

# M186: the actual Alpine pidstat ELF exercises live per-process accounting
# rather than just global /proc/stat. A selected SELF PID must survive two
# one-second sampling intervals and produce two genuine task CPU rows. The
# kernel already exposes /proc/<pid>/stat, /proc/stat and /proc/uptime; do
# not claim disk I/O, task delay accounting or a generic process monitor.
busy test -r /proc/self/stat
busy timeout 15 /usr/bin/pidstat -u -p SELF 1 2 \
    > "$work/pidstat-self" 2> "$work/pidstat-self-error"
busy awk '
    /(^|[[:space:]])PID[[:space:]]/ && /%CPU/ { headers++; next }
    $NF=="pidstat" && $1!="Average:" && NF>=9 {
        # Columns relative to Command are stable even with a 12-hour
        # timestamp (AM/PM adds one field): UID PID %usr %system %guest
        # %wait %CPU CPU Command. The process PID must be nonzero.
        pid=$(NF-7)
        if (pid !~ /^[0-9]+$/ || pid+0<=0) exit 1
        if (first_pid && pid!=first_pid) exit 1
        first_pid=pid
        for (i=NF-6; i<=NF-2; i++) {
            if ($i !~ /^[0-9]+([.][0-9]+)?$/) exit 1
            if ($i+0<0 || $i+0>100.5) exit 1
        }
        samples++
    }
    END { if (headers<1 || samples!=2) exit 1 }
' "$work/pidstat-self"
echo M186_SOFTWARE_PIDSTAT_SELF_PROC_FUNCTION_PASS

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

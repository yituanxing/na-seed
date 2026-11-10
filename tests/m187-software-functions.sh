#!/bin/sh
# Real functional acceptance for already-staged, signed Alpine executables.
# This is NOT another entry/--help test and does NOT increase the ELF count.
# Called from tests/m174-software-run.sh in the same actual QEMU NaSeed guest.
set -eu
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy() { /bin/busybox "$@"; }

work=/tmp/m187-software
busy mkdir -p "$work"

fail() {
    stage=$1
    reason=$2
    shift 2
    echo "[M187 SOFTWARE FAIL] stage=$stage $reason" >&2
    for artifact in "$@"; do
        if [ -f "$artifact" ]; then
            echo "[M187 SOFTWARE DIAG] $artifact" >&2
            busy tail -n 24 "$artifact" >&2 || :
        fi
    done
    exit 1
}

# The sar text format has a locale-independent CPU selector `all` followed by
# six CPU percentages. Skip Average, require genuine observation rows, check
# every field and the approximate total (rounding error permitted).
sar_cpu_rows() {
    input=$1
    required=$2
    busy awk -v required="$required" '
        $1=="Average:" || $1=="Summary:" { next }
        {
            for (i=1; i<=NF; i++) {
                if ($i!="all" || i+6>NF) continue
                sum=0
                for (j=i+1; j<=i+6; j++) {
                    if ($j !~ /^[0-9]+([.][0-9]+)?$/ || $j+0>100.5) bad=1
                    sum+=$j
                }
                if (sum<97 || sum>103) bad=1
                rows++
                break
            }
        }
        END { if (bad || rows<required) exit 1 }
    ' "$input"
}

# 1. /usr/bin/sar starts the signed sadc collector and samples real /proc.
busy grep -q '^cpu ' /proc/stat || fail sar-prereq 'missing /proc/stat CPU counters'
if busy timeout 18 /usr/bin/sar -u 1 2 \
    > "$work/sar-live.stdout" 2> "$work/sar-live.stderr"; then
    :
else
    rc=$?
    fail sar-live "exit=$rc" "$work/sar-live.stdout" "$work/sar-live.stderr"
fi
sar_cpu_rows "$work/sar-live.stdout" 2 || \
    fail sar-live-oracle 'expected two numeric all-CPU reports' \
        "$work/sar-live.stdout" "$work/sar-live.stderr"
echo M187_SOFTWARE_SAR_LIVE_CPU_FUNCTION_PASS

# 2. A DIFFERENT sar invocation records the collector result as a binary SA
# file on the guest tmpfs.  Do not assume -o also prints readable reports.
capture="$work/cpu-activity.sa"
if busy timeout 18 /usr/bin/sar -u -o "$capture" 1 2 \
    > "$work/sar-save.stdout" 2> "$work/sar-save.stderr"; then
    :
else
    rc=$?
    fail sar-binary-capture "exit=$rc" \
        "$work/sar-save.stdout" "$work/sar-save.stderr"
fi
[ -s "$capture" ] || fail sar-binary-capture 'binary activity file is empty' \
    "$work/sar-save.stdout" "$work/sar-save.stderr"
echo M187_SOFTWARE_SAR_BINARY_CAPTURE_FUNCTION_PASS

# 3. The same original sar ELF must parse that file rather than /proc data.
if busy timeout 12 /usr/bin/sar -u -f "$capture" \
    > "$work/sar-replay.stdout" 2> "$work/sar-replay.stderr"; then
    :
else
    rc=$?
    fail sar-binary-replay "exit=$rc" \
        "$work/sar-replay.stdout" "$work/sar-replay.stderr"
fi
sar_cpu_rows "$work/sar-replay.stdout" 1 || \
    fail sar-binary-replay-oracle 'no parsed numeric CPU records' \
        "$work/sar-replay.stdout" "$work/sar-replay.stderr"
echo M187_SOFTWARE_SAR_BINARY_REPLAY_FUNCTION_PASS

# 4. A second original signed executable, sadf, independently decodes the
# SA binary. -d emits semicolon-delimited DB-friendly numeric CPU records.
if busy timeout 12 /usr/bin/sadf -d "$capture" -- -u \
    > "$work/sadf-decode.stdout" 2> "$work/sadf-decode.stderr"; then
    :
else
    rc=$?
    fail sadf-binary-decode "exit=$rc" \
        "$work/sadf-decode.stdout" "$work/sadf-decode.stderr"
fi
busy awk -F';' '
    NF>=10 {
        valid=1
        sum=0
        for (i=NF-5; i<=NF; i++) {
            if ($i !~ /^[0-9]+([.][0-9]+)?$/ || $i+0>100.5) valid=0
            sum+=$i
        }
        if (valid && sum>=97 && sum<=103) rows++
    }
    END { if (rows<1) exit 1 }
' "$work/sadf-decode.stdout" || \
    fail sadf-binary-decode-oracle 'no semicolon-delimited numeric CPU records' \
        "$work/sadf-decode.stdout" "$work/sadf-decode.stderr"
echo M187_SOFTWARE_SADF_BINARY_DECODE_FUNCTION_PASS

# 5. The original Alpine inotifywait must receive actual kernel events.
# Use separate one-shot watchers instead of relying on watch -m termination,
# and assert both the event kind and exact basename. A short startup delay
# gives the watcher time to register before the guest mutates the directory.
watch="$work/inotify-watch"
busy mkdir -p "$watch"
watched_file="$watch/m187-event.txt"
[ ! -e "$watched_file" ] || fail inotify-prereq 'unexpected existing test file'
if [ ! -x /usr/bin/inotifywait ]; then
    fail inotify-prereq 'original signed inotifywait ELF missing'
fi
busy timeout 14 /usr/bin/inotifywait -q -e create \
    --format '%e %f' "$watch" \
    > "$work/inotify-create.stdout" 2> "$work/inotify-create.stderr" &
watch_pid=$!
busy sleep 1
busy printf 'created by NaSeed M187\n' > "$watched_file"
if wait "$watch_pid"; then
    :
else
    rc=$?
    fail inotify-create "exit=$rc" \
        "$work/inotify-create.stdout" "$work/inotify-create.stderr"
fi
busy grep -Fxq 'CREATE m187-event.txt' "$work/inotify-create.stdout" || \
    fail inotify-create-oracle 'no matching CREATE event' \
        "$work/inotify-create.stdout" "$work/inotify-create.stderr"

busy timeout 14 /usr/bin/inotifywait -q -e modify \
    --format '%e %f' "$watch" \
    > "$work/inotify-modify.stdout" 2> "$work/inotify-modify.stderr" &
watch_pid=$!
busy sleep 1
busy printf 'modified by NaSeed M187\n' >> "$watched_file"
if wait "$watch_pid"; then
    :
else
    rc=$?
    fail inotify-modify "exit=$rc" \
        "$work/inotify-modify.stdout" "$work/inotify-modify.stderr"
fi
busy grep -Fxq 'MODIFY m187-event.txt' "$work/inotify-modify.stdout" || \
    fail inotify-modify-oracle 'no matching MODIFY event' \
        "$work/inotify-modify.stdout" "$work/inotify-modify.stderr"
busy grep -Fxq 'created by NaSeed M187' "$watched_file" || \
    fail inotify-file-oracle 'watched file lost CREATE payload'
busy grep -Fxq 'modified by NaSeed M187' "$watched_file" || \
    fail inotify-file-oracle 'watched file did not persist both writes'
echo M187_SOFTWARE_INOTIFY_CREATE_MODIFY_FUNCTION_PASS

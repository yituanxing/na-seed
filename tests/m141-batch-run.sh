#!/bin/sh
# M141: collect every real guest behavior failure before exiting. There is no
# fast-fail between independent cases, and no host-generated PASS substitution.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
export PATH HOME LC_ALL
busy=/bin/busybox
work=/tmp/m141-case-logs
"$busy" mkdir -p "$work"

tab=$("$busy" printf '\t')
count=0
failed=0
seen=' '
while IFS="$tab" read -r family case_id marker; do
    case "$family" in ''|'#'*) continue ;; esac
    case "$family" in dns|toolchain|system) ;; *) echo '[M141 BAD FAMILY]' >&2; exit 2 ;; esac
    case "$case_id" in ''|*[!a-zA-Z0-9_-]*) echo '[M141 BAD CASE]' >&2; exit 2 ;; esac
    case "$marker" in M141_*_PASS) ;; *) echo '[M141 BAD MARKER]' >&2; exit 2 ;; esac
    case "$seen" in *" $marker "*) echo '[M141 DUPLICATE MARKER]' >&2; exit 2 ;; esac
    seen="$seen$marker "
    count=$((count + 1))
    log="$work/$family-$case_id.log"
    script="/tests/m141-$family-run.sh"
    if /bin/sh "$script" "$case_id" > "$log" 2>&1; then
        "$busy" printf '%s\n' "$marker"
    else
        failed=$((failed + 1))
        "$busy" printf '%s\n' "${marker%_PASS}_FAIL"
        "$busy" tail -n 40 "$log" || :
    fi
done < /tests/m141-guest-cases.tsv

if [ "$count" -ne 38 ] || [ "$failed" -ne 0 ]; then
    "$busy" printf '[M141 BATCH FAIL] %s executed, %s failed\n' "$count" "$failed" >&2
    exit 1
fi
"$busy" printf 'M141_BATCH_PASS\n'

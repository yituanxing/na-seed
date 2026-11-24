#!/bin/sh
# Shared native guest marker gate. Each caller still owns QEMU, staging,
# per-program manifests, EXEC proof and special workload assertions.
set -eu
[ "$#" -eq 6 ] || exit 2
prefix=$1
port=$2
manifest=$3
log=$4
expected=$5
tail_lines=$6
test -s "$manifest" && test -s "$log" || exit 1
count=0
summary=0
seen=' '
while IFS= read -r marker; do
    case "$marker" in ''|'#'*) continue ;; esac
    case "$marker" in "${prefix}_"*_PASS) ;; *)
        echo "[$prefix BATCH FAIL] invalid marker: $marker" >&2; exit 2 ;;
    esac
    case "$seen" in *" $marker "*)
        echo "[$prefix BATCH FAIL] duplicate marker: $marker" >&2; exit 2 ;;
    esac
    seen="$seen$marker "
    found=$(grep -Fxc -- "$marker" "$log" || :)
    if [ "$found" -ne 1 ]; then
        echo "[$prefix BATCH FAIL] $port $marker attestations=$found (expected 1)" >&2
        if [ "$tail_lines" -gt 0 ]; then tail -n "$tail_lines" "$log" >&2; fi
        exit 1
    fi
    if [ "$marker" = "${prefix}_BATCH_PASS" ]; then
        summary=$((summary+1))
    else
        count=$((count+1))
    fi
done < "$manifest"
[ "$count" -eq "$expected" ] && [ "$summary" -eq 1 ] || {
    echo "[$prefix BATCH FAIL] $port expected $expected markers and 1 summary, got $count/$summary" >&2
    exit 1
}

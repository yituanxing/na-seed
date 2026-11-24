#!/bin/sh
# Hash the staged original files, preserving path order and sha256sum output.
# APK signature/machine validation belongs to the existing overlay importer.
set -eu
[ "$#" -eq 6 ] || exit 2
root=$1
manifest=$2
output=$3
expected=$4
mode=$5
prefix=$6
# Keep historical output pathname (guest path versus resolved file) and
# each caller's original validation policy explicit.
case "$mode" in
    count|unique|alias-path|alias-exec|contained-exec|contained-file|contained-elf|contained-exec-elf) ;;
    *) exit 2 ;;
esac
case "$mode" in contained-*) canonical=$(cd "$root" && pwd -P) ;; esac
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    target="$root/$elf"
    case "$mode" in
        count|unique) ;;
        *)
            resolved=$(readlink -f "$target") || exit 1
            case "$mode" in
                contained-*)
                    case "$resolved" in "$canonical"/*) ;; *)
                        echo "[$prefix HOST STAGE FAIL] ELF escapes root: $elf" >&2; exit 1 ;;
                    esac
                    case "$resolved" in "$canonical/bin/busybox"|"$canonical/usr/bin/busybox") exit 1 ;; esac ;;
                alias-*)
                    # The early callers compared the resolved path against
                    # root as supplied. Preserve this historical behavior.
                    case "$resolved" in "$root/bin/busybox"|"$root/usr/bin/busybox") exit 1 ;; esac ;;
            esac
            case "$mode" in
                alias-path) ;;
                *) test -x "$resolved" || exit 1; target="$resolved" ;;
            esac
            case "$mode" in contained-file|contained-elf) test -f "$resolved" || exit 1 ;; esac
            case "$mode" in
                contained-elf|contained-exec-elf)
                    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1 ;;
            esac ;;
    esac
    sha256sum "$target"
    count=$((count+1))
done < "$manifest" > "$output"
[ "$count" -eq "$expected" ] && test -s "$output" || {
    echo "[$prefix HOST STAGE FAIL] $count ELF paths, expected $expected" >&2
    exit 1
}
if [ "$mode" != count ]; then
    unique=$(awk '{print $1}' "$output" | sort -u | wc -l)
    [ "$unique" -eq "$expected" ] || {
        echo "[$prefix HOST STAGE FAIL] $unique distinct hashes, expected $expected" >&2
        exit 1
    }
fi

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
case "$mode" in count|unique) ;; *) exit 2 ;; esac
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    sha256sum "$root/$elf"
    count=$((count+1))
done < "$manifest" > "$output"
[ "$count" -eq "$expected" ] && test -s "$output" || {
    echo "[$prefix HOST STAGE FAIL] $count ELF paths, expected $expected" >&2
    exit 1
}
if [ "$mode" = unique ]; then
    unique=$(awk '{print $1}' "$output" | sort -u | wc -l)
    [ "$unique" -eq "$expected" ] || {
        echo "[$prefix HOST STAGE FAIL] $unique distinct hashes, expected $expected" >&2
        exit 1
    }
fi

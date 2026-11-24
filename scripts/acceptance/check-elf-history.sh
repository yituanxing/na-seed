#!/bin/sh
# Preserve historical admission policy: no ELF path may be counted in
# another milestone. Local distinctness/count remain explicit per caller.
set -eu
[ "$#" -ge 2 ] && [ "$#" -le 4 ] || exit 2
prefix=$1
current=$2
shift 2
print_count=0
distinct=0
for flag in "$@"; do
    case "$flag" in --count) print_count=1 ;; --distinct) distinct=1 ;; *) exit 2 ;; esac
done
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    if [ "$distinct" -eq 1 ]; then
        case "$seen" in *" $elf "*)
            echo "[$prefix DUPLICATE PATH] $elf" >&2; exit 1 ;;
        esac
        seen="$seen$elf "
    fi
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$current" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[$prefix DUPLICATE ORIGINAL ELF] $elf already in $old" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$current"
if [ "$print_count" -eq 1 ]; then printf '%s\n' "$count"; fi

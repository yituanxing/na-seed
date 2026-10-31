#!/bin/sh
# M115: fifteen unmodified original Alpine Linux ELF applications execute
# real cryptographic, text, patch, archive-sync and filesystem transformations.
# All APKs and their dependencies are installed by a Linux host. No software
# is compiled or installed inside NaSeed during these native QEMU checks.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    [ "$#" -gt 0 ] || return 1
    for candidate do
        [ -x "$candidate" ] || continue
        resolved=$(busy readlink -f "$candidate") || continue
        [ -x "$resolved" ] || continue
        case "$resolved" in /bin/busybox|/usr/bin/busybox) continue ;; esac
        magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        if [ "$magic" = 7f454c46 ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "[M115 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

run_case() {
    app=$1
    work="/tmp/m115-$app"
    busy mkdir -p "$work"
    case "$app" in
      openssl)
        exe=$(original_elf /usr/bin/openssl)
        printf 'NaSeed M115 original OpenSSL AES-CTR roundtrip\n' > "$work/plain"
        key=000102030405060708090a0b0c0d0e0f
        iv=101112131415161718191a1b1c1d1e1f
        "$exe" enc -aes-128-ctr -K "$key" -iv "$iv" \
            -in "$work/plain" -out "$work/cipher"
        [ -s "$work/cipher" ]
        if busy cmp -s "$work/plain" "$work/cipher"; then
            echo '[M115 OPENSSL FAIL] encryption did not change plaintext' >&2
            return 1
        fi
        "$exe" enc -d -aes-128-ctr -K "$key" -iv "$iv" \
            -in "$work/cipher" -out "$work/restored"
        busy cmp "$work/plain" "$work/restored" ;;
      ssh-keygen)
        exe=$(original_elf /usr/bin/ssh-keygen)
        "$exe" -q -t ed25519 -N '' -f "$work/id_ed25519"
        [ -s "$work/id_ed25519" ] && [ -s "$work/id_ed25519.pub" ]
        "$exe" -y -f "$work/id_ed25519" > "$work/derived.pub"
        busy awk '{print $1 " " $2}' "$work/id_ed25519.pub" > "$work/expected.pub"
        busy awk '{print $1 " " $2}' "$work/derived.pub" > "$work/derived.key"
        busy cmp "$work/expected.pub" "$work/derived.key"
        "$exe" -l -f "$work/id_ed25519.pub" > "$work/fingerprint"
        busy grep -Fq ED25519 "$work/fingerprint" ;;
      gawk)
        exe=$(original_elf /usr/bin/gawk)
        printf 'east\t7\nwest\t3\neast\t5\n' > "$work/orders.tsv"
        "$exe" -F '\t' '$1=="east" {total+=$2} END {print total}' \
            "$work/orders.tsv" > "$work/sum"
        printf '12\n' > "$work/expected"
        busy cmp "$work/expected" "$work/sum" ;;
      diff)
        exe=$(original_elf /usr/bin/diff)
        printf 'alpha\nbeta\ngamma\n' > "$work/old"
        printf 'alpha\nBETA\ngamma\n' > "$work/new"
        if "$exe" -u "$work/old" "$work/new" > "$work/patch"; then
            echo '[M115 DIFF FAIL] differing files reported identical' >&2
            return 1
        else
            [ "$?" -eq 1 ] || return 1
        fi
        busy grep -Fxq -- '-beta' "$work/patch"
        busy grep -Fxq -- '+BETA' "$work/patch"
        busy cmp -s "$work/old" "$work/new" && return 1
        [ -s "$work/patch" ] ;;
      diff3)
        exe=$(original_elf /usr/bin/diff3)
        printf 'alpha\nkeep-one\nkeep-two\nomega\n' > "$work/base"
        printf 'ALPHA\nkeep-one\nkeep-two\nomega\n' > "$work/left"
        printf 'alpha\nkeep-one\nkeep-two\nOMEGA\n' > "$work/right"
        "$exe" -m "$work/left" "$work/base" "$work/right" > "$work/merged"
        printf 'ALPHA\nkeep-one\nkeep-two\nOMEGA\n' > "$work/expected"
        busy cmp "$work/expected" "$work/merged" ;;
      patch)
        exe=$(original_elf /usr/bin/patch)
        printf 'red\nblue\ngreen\n' > "$work/colors"
        printf '%s\n' '--- colors' '+++ colors' '@@ -1,3 +1,3 @@' \
            ' red' '-blue' '+BLUE' ' green' > "$work/changes.patch"
        (cd "$work"; "$exe" --batch -p0 < changes.patch) > "$work/report"
        printf 'red\nBLUE\ngreen\n' > "$work/expected"
        busy cmp "$work/expected" "$work/colors" ;;
      rsync)
        exe=$(original_elf /usr/bin/rsync)
        busy mkdir -p "$work/source/nested" "$work/target"
        printf 'NaSeed original rsync nested file\n' > "$work/source/nested/a.txt"
        printf 'second original file\n' > "$work/source/b.txt"
        "$exe" -a "$work/source/" "$work/target/"
        busy cmp "$work/source/nested/a.txt" "$work/target/nested/a.txt"
        busy cmp "$work/source/b.txt" "$work/target/b.txt"
        printf 'NaSeed original rsync incremental content\n' > "$work/source/nested/a.txt"
        "$exe" -a "$work/source/" "$work/target/"
        busy cmp "$work/source/nested/a.txt" "$work/target/nested/a.txt" ;;
      file)
        exe=$(original_elf /usr/bin/file)
        # A real 2x2 GIF87a image, not a renamed text file.
        printf '%s' 'R0lGODdhAgACAIEAAP8AAAD/AAAA////ACwAAAAAAgACAAAIBwABBBAwICAAOw==' |
            busy base64 -d > "$work/pixels.gif"
        "$exe" -b --mime-type "$work/pixels.gif" > "$work/kind"
        printf 'image/gif\n' > "$work/expected"
        busy cmp "$work/expected" "$work/kind" ;;
      find)
        exe=$(original_elf /usr/bin/find)
        busy mkdir -p "$work/root/sub"
        printf 'first\n' > "$work/root/a.txt"
        printf 'second\n' > "$work/root/sub/b.txt"
        printf 'ignored\n' > "$work/root/sub/c.bin"
        "$exe" "$work/root" -type f -name '*.txt' -print > "$work/found"
        [ "$(busy wc -l < "$work/found")" -eq 2 ]
        busy grep -Fxq "$work/root/a.txt" "$work/found"
        busy grep -Fxq "$work/root/sub/b.txt" "$work/found"
        if busy grep -Fq c.bin "$work/found"; then return 1; fi ;;
      xargs)
        exe=$(original_elf /usr/bin/xargs)
        printf 'one two\000three four\000' > "$work/tokens"
        "$exe" -0 -n 1 /bin/busybox printf 'VALUE:%s\n' \
            < "$work/tokens" > "$work/results"
        printf 'VALUE:one two\nVALUE:three four\n' > "$work/expected"
        busy cmp "$work/expected" "$work/results" ;;
      grep)
        exe=$(original_elf /bin/grep)
        printf 'oak:12\nbad:x\npine:4\n9wrong:7\n' > "$work/source"
        "$exe" -E '^[[:alpha:]]+:[0-9]+$' "$work/source" > "$work/matched"
        printf 'oak:12\npine:4\n' > "$work/expected"
        busy cmp "$work/expected" "$work/matched" ;;
      sed)
        exe=$(original_elf /bin/sed)
        printf 'alpha=17\nbeta=3\nnot-matched\n' > "$work/source"
        "$exe" -E 's/^([a-z]+)=([0-9]+)$/\2:\1/' "$work/source" > "$work/output"
        printf '17:alpha\n3:beta\nnot-matched\n' > "$work/expected"
        busy cmp "$work/expected" "$work/output" ;;
      bc)
        exe=$(original_elf /usr/bin/bc)
        printf 'scale=3\n10/4\n(7*8)+9\n' | "$exe" -q > "$work/output"
        printf '2.500\n65\n' > "$work/expected"
        busy cmp "$work/expected" "$work/output" ;;
      dc)
        exe=$(original_elf /usr/bin/dc)
        printf '7 8 * 9 + p\n' | "$exe" > "$work/output"
        printf '65\n' > "$work/expected"
        busy cmp "$work/expected" "$work/output" ;;
      cmp)
        exe=$(original_elf /usr/bin/cmp)
        printf 'abc123\n' > "$work/source"
        busy cp "$work/source" "$work/identical"
        printf 'abX123\n' > "$work/different"
        "$exe" -s "$work/source" "$work/identical"
        if "$exe" -l "$work/source" "$work/different" > "$work/differences"; then
            echo '[M115 CMP FAIL] different bytes reported identical' >&2
            return 1
        else
            [ "$?" -eq 1 ] || return 1
        fi
        busy awk 'NF>=3 && $1==3 {found=1} END {exit !found}' "$work/differences" ;;
      *)
        echo "[M115 UNKNOWN ORIGINAL ELF CASE] $app" >&2
        return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    if [ "${M115_TRACE:-0}" = 1 ]; then
        PS4='[M115 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m115-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in openssl ssh-keygen gawk diff diff3 patch rsync file find xargs \
           grep sed bc dc cmp; do
    echo "[M115 CASE START] $app"
    case_log="/tmp/m115-$app-case.log"
    if M115_TRACE=1 /bin/sh /tests/m115-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
        printf 'M115_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]-' '[:upper:]_')
        echo "[M115 $upper FAILED] original native ELF or strict functional assertion, rc=$rc" >&2
        busy tail -90 "$case_log" >&2
        printf 'M115_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M115_BATCH_PASS
else
    echo "[M115 BATCH FAIL] $failed/15 native original Linux ELF apps" >&2
    echo M115_BATCH_FAIL
    exit 1
fi

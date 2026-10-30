#!/bin/sh
# M111: 15 MORE distinct signed Alpine 3.23 original Linux executables.
# Input fixtures are synthesized inside NaSeed, but never applications.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
export PATH HOME LC_ALL LANG TERM
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
    echo "[M111 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

run_case() {
    app=$1
    work="/tmp/m111-$app"
    busy mkdir -p "$work"
    printf 'alpha one\nbeta two\ngamma three\n' > "$work/data.txt"
    case "$app" in
      fzf)
        exe=$(original_elf /usr/bin/fzf)
        printf 'alpha\nbeta\ngamma\n' | "$exe" --filter=beta --no-sort > "$work/actual"
        printf 'beta\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      bat)
        exe=$(original_elf /usr/bin/bat)
        "$exe" --plain --color=never --paging=never --wrap=never "$work/data.txt" > "$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      eza)
        exe=$(original_elf /usr/bin/eza)
        "$exe" -1 --color=never "$work" > "$work/actual"
        busy grep -Fxq 'data.txt' "$work/actual" ;;
      hexyl)
        exe=$(original_elf /usr/bin/hexyl)
        printf abc > "$work/bytes"
        "$exe" --length 3 --color=never "$work/bytes" > "$work/actual"
        busy grep -Fq '61 62 63' "$work/actual" ;;
      hyperfine)
        exe=$(original_elf /usr/bin/hyperfine)
        "$exe" --warmup 0 --runs 2 --export-json "$work/bench.json" \
            '/bin/busybox true' > "$work/stdout"
        [ -s "$work/bench.json" ]
        busy grep -Fq '"command"' "$work/bench.json"
        busy grep -Fq '"mean"' "$work/bench.json" ;;
      dust)
        exe=$(original_elf /usr/bin/dust)
        printf '0123456789\n' > "$work/bytes"
        "$exe" -b "$work" > "$work/actual"
        [ -s "$work/actual" ]
        busy grep -Fq 'data.txt' "$work/actual" ;;
      sd)
        exe=$(original_elf /usr/bin/sd)
        "$exe" beta BETA "$work/data.txt"
        printf 'alpha one\nBETA two\ngamma three\n' > "$work/expected"
        busy cmp "$work/data.txt" "$work/expected" ;;
      choose)
        exe=$(original_elf /usr/bin/choose)
        printf 'a:42:b\n' | "$exe" -f ':' 1 > "$work/actual"
        printf '42\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      miller)
        exe=$(original_elf /usr/bin/mlr)
        printf 'name,score\nalpha,42\n' > "$work/data.csv"
        "$exe" --csv put '$score=$score+1' "$work/data.csv" > "$work/actual"
        busy grep -Fxq 'name,score' "$work/actual"
        busy grep -Fxq 'alpha,43' "$work/actual" ;;
      dasel)
        exe=$(original_elf /usr/bin/dasel)
        printf '{"value":42}\n' > "$work/data.json"
        "$exe" -r json -f "$work/data.json" '.value' > "$work/actual"
        busy grep -Fxq '42' "$work/actual" ;;
      cmark)
        exe=$(original_elf /usr/bin/cmark)
        printf '# NaSeed\n' > "$work/data.md"
        "$exe" "$work/data.md" > "$work/actual"
        busy grep -Fq '<h1>NaSeed</h1>' "$work/actual" ;;
      lowdown)
        exe=$(original_elf /usr/bin/lowdown)
        printf '# NaSeed\n' > "$work/data.md"
        "$exe" -T html "$work/data.md" > "$work/actual"
        busy grep -Fq '<h1' "$work/actual"
        busy grep -Fq 'NaSeed' "$work/actual" ;;
      htmlq)
        exe=$(original_elf /usr/bin/htmlq)
        printf '<main><p class="msg">alpha</p></main>\n' > "$work/data.html"
        "$exe" -t 'p.msg' < "$work/data.html" > "$work/actual"
        busy grep -Fxq 'alpha' "$work/actual" ;;
      pup)
        exe=$(original_elf /usr/bin/pup)
        printf '<main><p class="msg">alpha</p></main>\n' > "$work/data.html"
        "$exe" 'p.msg text{}' < "$work/data.html" > "$work/actual"
        busy grep -Fxq 'alpha' "$work/actual" ;;
      just)
        exe=$(original_elf /usr/bin/just)
        printf 'run:\n    @echo JUST_OK\n' > "$work/justfile"
        "$exe" --justfile "$work/justfile" run > "$work/actual"
        printf 'JUST_OK\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      *)
        echo "[M111 UNKNOWN APPLICATION] $app" >&2
        return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m111-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in fzf bat eza hexyl hyperfine dust sd choose miller dasel \
           cmark lowdown htmlq pup just; do
    echo "[M111 CASE START] $app"
    if /bin/sh /tests/m111-batch-run.sh --case "$app"; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M111_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M111 $upper FAILED] original Linux ELF or functional oracle, rc=$rc" >&2
        printf 'M111_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M111_BATCH_PASS
else
    echo "[M111 BATCH FAIL] $failed/15 signed original Linux program checks" >&2
    echo M111_BATCH_FAIL
    exit 1
fi

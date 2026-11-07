#!/bin/sh
# Functional translation-catalog oracles for FIFTEEN genuine GNU gettext ELF
# executables. Linux HOST stages signed packages and all input fixtures.
# No compiler, package manager or test-fixture build runs in NaSeed.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() {
    if [ -x /bin/busybox ]; then /bin/busybox "$@"; else "$@"; fi
}

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
    echo "[M119 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

inputs=${M119_TEST_DIR:-/tests}
po="$inputs/m119-catalog.po"
second="$inputs/m119-second.po"
template="$inputs/m119-template.pot"
added="$inputs/m119-template-added.pot"
duplicates="$inputs/m119-duplicates.po"
source="$inputs/m119-extract.c"

check_input() {
    for input in "$po" "$second" "$template" "$added" "$duplicates" "$source"; do
        [ -s "$input" ] || { echo "[M119 MISSING FIXTURE] $input" >&2; return 1; }
    done
}

run_case() {
    app=$1
    work="/tmp/m119-$app"
    busy mkdir -p "$work"
    case "$app" in
      msgmerge)
        exe=$(original_elf /usr/bin/msgmerge)
        "$exe" --quiet -o "$work/merged.po" "$po" "$added"
        busy grep -Fq 'msgid "M119_ADDED"' "$work/merged.po"
        busy grep -Fq 'msgstr "bonjour_m119"' "$work/merged.po" ;;
      msgattrib)
        exe=$(original_elf /usr/bin/msgattrib)
        "$exe" --translated -o "$work/translated.po" "$po"
        busy grep -Fq 'msgid "M119_HELLO"' "$work/translated.po"
        if busy grep -Fq 'msgid "M119_UNTRANSLATED"' "$work/translated.po"; then return 1; fi ;;
      msgcat)
        exe=$(original_elf /usr/bin/msgcat)
        "$exe" --use-first -o "$work/combined.po" "$po" "$second"
        busy grep -Fq 'msgid "M119_NEW"' "$work/combined.po"
        busy grep -Fq 'msgid "M119_WORLD"' "$work/combined.po"
        busy grep -Fq 'msgstr "nouveau_m119"' "$work/combined.po" ;;
      msgcomm)
        exe=$(original_elf /usr/bin/msgcomm)
        "$exe" --more-than=1 -o "$work/common.po" "$po" "$second"
        busy grep -Fq 'msgid "M119_HELLO"' "$work/common.po"
        if busy grep -Fq 'msgid "M119_WORLD"' "$work/common.po"; then return 1; fi
        if busy grep -Fq 'msgid "M119_NEW"' "$work/common.po"; then return 1; fi ;;
      msgcmp)
        exe=$(original_elf /usr/bin/msgcmp)
        "$exe" "$po" "$template" > "$work/validated" 2>&1
        if "$exe" "$second" "$template" > "$work/missing" 2>&1; then return 1; fi
        busy grep -Eiq 'not defined|fatal errors|fatal error|missing|untranslated' "$work/missing" ;;
      msgconv)
        exe=$(original_elf /usr/bin/msgconv)
        "$exe" --to-code=ISO-8859-1 -o "$work/latin1.po" "$po"
        "$exe" --to-code=UTF-8 -o "$work/roundtrip.po" "$work/latin1.po"
        busy grep -Fq 'msgstr "café_m119"' "$work/roundtrip.po"
        busy grep -Fq 'charset=UTF-8' "$work/roundtrip.po" ;;
      msgen)
        exe=$(original_elf /usr/bin/msgen)
        "$exe" -o "$work/english.po" "$template"
        busy grep -Fq 'msgid "M119_HELLO"' "$work/english.po"
        busy grep -Fq 'msgstr "M119_HELLO"' "$work/english.po" ;;
      msgexec)
        exe=$(original_elf /usr/bin/msgexec)
        # GNU msgexec built-in 0 emits NUL-delimited translations: genuine
        # catalog traversal even when the guest cannot execute child commands.
        "$exe" -i "$po" 0 > "$work/translations.nul"
        busy tr '\000' '\n' < "$work/translations.nul" > "$work/translations.txt"
        busy grep -Fxq 'bonjour_m119' "$work/translations.txt"
        busy grep -Fxq 'monde_m119' "$work/translations.txt" ;;
      msgfilter)
        exe=$(original_elf /usr/bin/msgfilter)
        if [ -x /bin/busybox ]; then
            "$exe" -i "$second" -o "$work/upper.po" /bin/busybox tr a-z A-Z
        else
            "$exe" -i "$second" -o "$work/upper.po" /usr/bin/tr a-z A-Z
        fi
        busy grep -Fq 'msgstr "BONJOUR_M119"' "$work/upper.po"
        busy grep -Fq 'msgstr "NOUVEAU_M119"' "$work/upper.po" ;;
      msggrep)
        exe=$(original_elf /usr/bin/msggrep)
        "$exe" --msgid --fixed-strings -e M119_WORLD -o "$work/selected.po" "$po"
        busy grep -Fq 'msgid "M119_WORLD"' "$work/selected.po"
        if busy grep -Fq 'msgid "M119_HELLO"' "$work/selected.po"; then return 1; fi ;;
      msginit)
        exe=$(original_elf /usr/bin/msginit)
        "$exe" --no-translator -l fr -i "$template" -o "$work/initialized.po"
        busy grep -Fq 'msgid "M119_HELLO"' "$work/initialized.po"
        busy grep -Fq 'Language: fr' "$work/initialized.po" ;;
      msguniq)
        exe=$(original_elf /usr/bin/msguniq)
        "$exe" --use-first -o "$work/unique.po" "$duplicates"
        occurrences=$(busy grep -Fxc 'msgid "M119_REPEAT"' "$work/unique.po" || true)
        [ "$occurrences" -eq 1 ]
        busy grep -Fq 'msgstr "repeat_m119"' "$work/unique.po" ;;
      msgunfmt)
        exe=$(original_elf /usr/bin/msgunfmt)
        # .mo comes from the Linux HOST; M109 already counted msgfmt and
        # nothing compiles inside NaSeed.
        [ -s "$inputs/m119-catalog.mo" ]
        "$exe" -o "$work/decoded.po" "$inputs/m119-catalog.mo"
        busy grep -Fq 'msgid "M119_HELLO"' "$work/decoded.po"
        busy grep -Fq 'msgstr "bonjour_m119"' "$work/decoded.po" ;;
      xgettext)
        exe=$(original_elf /usr/bin/xgettext)
        "$exe" --language=C --no-location -o "$work/extracted.pot" "$source"
        busy grep -Fq 'msgid "M119_EXTRACTED_MESSAGE"' "$work/extracted.pot" ;;
      envsubst)
        exe=$(original_elf /usr/bin/envsubst)
        M119_NAME=NaSeed
        export M119_NAME
        printf 'welcome-$M119_NAME\n' | "$exe" > "$work/substituted.txt"
        busy grep -Fxq 'welcome-NaSeed' "$work/substituted.txt" ;;
      *) echo "[M119 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    check_input
    if [ "${M119_TRACE:-0}" = 1 ]; then
        PS4='[M119 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m119-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in msgmerge msgattrib msgcat msgcomm msgcmp msgconv msgen msgexec \
           msgfilter msggrep msginit msguniq msgunfmt xgettext envsubst; do
    echo "[M119 CASE START] $app"
    case_log="/tmp/m119-$app-case.log"
    if M119_TRACE=1 /bin/sh "$inputs/m119-batch-run.sh" --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M119_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M119 $upper FAILED] genuine original Linux ELF, rc=$rc" >&2
        busy tail -100 "$case_log" >&2
        printf 'M119_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M119_BATCH_PASS
else
    echo "[M119 BATCH FAIL] $failed/15 original Linux ELF translation applications" >&2
    echo M119_BATCH_FAIL
    exit 1
fi

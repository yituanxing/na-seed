#!/bin/sh
# M117 original signed Linux ELF program behavior checks, not version smoke.
# Host resolves and verifies Alpine packages; NaSeed guest never compiles.
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
    echo "[M117 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

make_payload() {
    # Include binary bytes, text and repeated blocks; compare actual data.
    printf 'NaSeed M117 original Linux ELF functional roundtrip: %s\n' "$1" > "$2"
    n=0
    while [ "$n" -lt 128 ]; do
        printf '\000\001\002binary\377%s\012' "$1" >> "$2"
        n=$((n + 1))
    done
    [ "$(busy wc -c < "$2")" -gt 1000 ]
}

signed_elf_fixture() {
    # ELF manipulation operates on COPIES of signed binaries.
    original_elf /usr/bin/ar
}

run_case() {
    app=$1
    work="/tmp/m117-$app"
    busy mkdir -p "$work"
    case "$app" in
      ar)
        exe=$(original_elf /usr/bin/ar)
        make_payload ar "$work/a"
        make_payload second "$work/b"
        (cd "$work"; "$exe" qc archive.a a b)
        [ -s "$work/archive.a" ]
        "$exe" t "$work/archive.a" > "$work/members"
        printf 'a\nb\n' > "$work/expected-members"
        busy cmp "$work/members" "$work/expected-members"
        "$exe" p "$work/archive.a" a > "$work/recovered-a"
        "$exe" p "$work/archive.a" b > "$work/recovered-b"
        busy cmp "$work/a" "$work/recovered-a"
        busy cmp "$work/b" "$work/recovered-b" ;;
      cxxfilt)
        exe=$(original_elf /usr/bin/c++filt)
        printf '_Z3fooi\n' | "$exe" > "$work/demangled"
        printf 'foo(int)\n' > "$work/expected"
        busy cmp "$work/expected" "$work/demangled" ;;
      elfedit)
        exe=$(original_elf /usr/bin/elfedit)
        elf=$(signed_elf_fixture)
        reader=$(original_elf /usr/bin/readelf)
        busy cp "$elf" "$work/edited.elf"
        "$exe" --output-osabi GNU "$work/edited.elf"
        "$reader" -h "$work/edited.elf" > "$work/header"
        busy grep -Eq 'OS/ABI:[[:space:]]*UNIX - GNU' "$work/header"
        "$reader" -S "$work/edited.elf" > "$work/sections"
        busy grep -Fq '.text' "$work/sections" ;;
      nm)
        exe=$(original_elf /usr/bin/nm)
        elf=$(signed_elf_fixture)
        "$exe" -D "$elf" > "$work/dynsym"
        [ -s "$work/dynsym" ]
        busy grep -Eq '[[:space:]]U[[:space:]]' "$work/dynsym" ;;
      objcopy)
        exe=$(original_elf /usr/bin/objcopy)
        elf=$(signed_elf_fixture)
        reader=$(original_elf /usr/bin/readelf)
        printf 'M117_BFD_ADDED_SECTION\n' > "$work/contents"
        "$exe" --add-section ".naseed=$work/contents" "$elf" "$work/copied.elf"
        [ -s "$work/copied.elf" ]
        "$reader" -SW "$work/copied.elf" > "$work/sections"
        busy grep -Fq '.naseed' "$work/sections"
        "$exe" --dump-section ".naseed=$work/recovered" "$work/copied.elf"
        busy cmp "$work/contents" "$work/recovered" ;;
      objdump)
        exe=$(original_elf /usr/bin/objdump)
        elf=$(signed_elf_fixture)
        "$exe" -f "$elf" > "$work/format"
        "$exe" -h "$elf" > "$work/sections"
        busy grep -Eq 'file format elf64' "$work/format"
        busy grep -Eq '[[:space:]]\.text[[:space:]]' "$work/sections" ;;
      size)
        exe=$(original_elf /usr/bin/size)
        elf=$(signed_elf_fixture)
        "$exe" -A "$elf" > "$work/sizes"
        busy awk '$1 == ".text" && $2 + 0 > 0 { ok = 1 } END { exit !ok }' "$work/sizes" ;;
      strings)
        exe=$(original_elf /usr/bin/strings)
        printf '\001\002\003M117_UNIQUE_BINUTILS_STRING\000\004\005\006' > "$work/input.bin"
        "$exe" -n 8 "$work/input.bin" > "$work/extracted"
        printf 'M117_UNIQUE_BINUTILS_STRING\n' > "$work/expected"
        busy cmp "$work/expected" "$work/extracted" ;;
      strip)
        exe=$(original_elf /usr/bin/strip)
        section_writer=$(original_elf /usr/bin/objcopy)
        reader=$(original_elf /usr/bin/readelf)
        elf=$(signed_elf_fixture)
        printf 'M117_STRIP_SECTION_REMOVAL\n' > "$work/contents"
        "$section_writer" --add-section ".naseed=$work/contents" "$elf" "$work/with-section.elf"
        "$reader" -SW "$work/with-section.elf" > "$work/before"
        busy grep -Fq '.naseed' "$work/before"
        "$exe" --remove-section=.naseed "$work/with-section.elf"
        "$reader" -SW "$work/with-section.elf" > "$work/after"
        if busy grep -Fq '.naseed' "$work/after"; then return 1; fi
        "$work/with-section.elf" --version > "$work/version"
        busy grep -Fq 'GNU ar' "$work/version" ;;
      rg)
        exe=$(original_elf /usr/bin/rg)
        printf 'noise\nNeedle target 14\nneedle skipped\n' > "$work/input"
        "$exe" -n '^Needle target 14$' "$work/input" > "$work/matches"
        printf '2:Needle target 14\n' > "$work/expected"
        busy cmp "$work/expected" "$work/matches" ;;
      fd)
        exe=$(original_elf /usr/bin/fd)
        busy mkdir -p "$work/tree/sub"
        printf 'alpha\n' > "$work/tree/a.nseed"
        printf 'beta\n' > "$work/tree/sub/b.nseed"
        printf 'ignored\n' > "$work/tree/sub/ignored.log"
        (cd "$work/tree"; "$exe" -j 1 -t f -e nseed .) |
            busy sed 's|^\./||' | busy sort > "$work/results"
        printf 'a.nseed\nsub/b.nseed\n' > "$work/expected"
        busy cmp "$work/expected" "$work/results" ;;
      tree)
        exe=$(original_elf /usr/bin/tree)
        busy mkdir -p "$work/root/nested"
        printf 'alpha\n' > "$work/root/alpha.nseed"
        printf 'beta\n' > "$work/root/nested/beta.nseed"
        "$exe" -J --noreport "$work/root" > "$work/listing.json"
        busy grep -Eq '"name"[[:space:]]*:[[:space:]]*"alpha.nseed"' "$work/listing.json"
        busy grep -Eq '"name"[[:space:]]*:[[:space:]]*"beta.nseed"' "$work/listing.json"
        busy grep -Eq '"type"[[:space:]]*:[[:space:]]*"directory"' "$work/listing.json" ;;
      xz)
        exe=$(original_elf /usr/bin/xz)
        make_payload xz "$work/input"
        "$exe" -6 -c "$work/input" > "$work/packed.xz"
        [ -s "$work/packed.xz" ]
        "$exe" -t "$work/packed.xz"
        "$exe" -d -c "$work/packed.xz" > "$work/unpacked"
        busy cmp "$work/input" "$work/unpacked" ;;
      bzip2)
        exe=$(original_elf /usr/bin/bzip2)
        make_payload bzip2 "$work/input"
        "$exe" -6 -c "$work/input" > "$work/packed.bz2"
        [ -s "$work/packed.bz2" ]
        "$exe" -t "$work/packed.bz2"
        "$exe" -d -c "$work/packed.bz2" > "$work/unpacked"
        busy cmp "$work/input" "$work/unpacked" ;;
      tar)
        exe=$(original_elf /bin/tar)
        busy mkdir -p "$work/source" "$work/restored"
        make_payload tar-a "$work/source/one"
        make_payload tar-b "$work/source/two"
        "$exe" -cf "$work/data.tar" -C "$work/source" one two
        [ -s "$work/data.tar" ]
        "$exe" -tf "$work/data.tar" > "$work/members"
        printf 'one\ntwo\n' > "$work/expected-members"
        busy cmp "$work/expected-members" "$work/members"
        "$exe" -xf "$work/data.tar" -C "$work/restored"
        busy cmp "$work/source/one" "$work/restored/one"
        busy cmp "$work/source/two" "$work/restored/two" ;;
      *) echo "[M117 UNKNOWN CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -ne 0 ] && [ "$1" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    if [ "${M117_TRACE:-0}" = 1 ]; then
        PS4='[M117 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || { echo 'usage: m117-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in ar cxxfilt elfedit nm objcopy objdump size strings strip \
           rg fd tree xz bzip2 tar; do
    echo "[M117 CASE START] $app"
    case_log="/tmp/m117-$app-case.log"
    if M117_TRACE=1 /bin/sh /tests/m117-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M117_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M117 $upper FAILED] genuine Linux ELF functional oracle, rc=$rc" >&2
        busy tail -110 "$case_log" >&2
        printf 'M117_%s_FAIL\n' "$upper"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M117_BATCH_PASS
else
    echo "[M117 BATCH FAIL] $failed/15 original Linux ELFs" >&2
    echo M117_BATCH_FAIL
    exit 1
fi

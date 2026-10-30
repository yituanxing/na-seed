#!/bin/sh
# M110: twenty more UNMODIFIED original signed Alpine Linux executables.
# Every case is a real functional oracle; no applications are built in NaSeed.
# The outer runner collects all case results without hiding an early failure.
PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
GIT_CONFIG_NOSYSTEM=1
export PATH HOME LC_ALL LANG TERM GIT_CONFIG_NOSYSTEM

busy() { /bin/busybox "$@"; }

# The guest must run a genuine target ELF from the signed APK, not an alias,
# script wrapper, or a BusyBox applet that only shares the utility's name.
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
    echo "[M110 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

run_case() {
    app=$1
    work="${M110_WORK_ROOT:-/tmp}/m110-$app"
    busy mkdir -p "$work"
    printf 'alpha one\nbeta two\ngamma three\n' > "$work/data.txt"
    case "$app" in
      node)
        program=$(original_elf /usr/bin/node)
        "$program" -e 'const fs=require("fs"); const s=fs.readFileSync(process.argv[1],"utf8"); fs.writeFileSync(process.argv[2], s.replace("beta","BETA"));' \
            "$work/data.txt" "$work/actual"
        printf 'alpha one\nBETA two\ngamma three\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      age)
        program=$(original_elf /usr/bin/age)
        keygen=$(original_elf /usr/bin/age-keygen)
        "$keygen" -o "$work/identity.txt" > "$work/keygen.log" 2>&1
        recipient=$(busy awk '/^# public key:/ {print $4}' "$work/identity.txt")
        [ -n "$recipient" ]
        "$program" -r "$recipient" -o "$work/data.age" "$work/data.txt"
        [ -s "$work/data.age" ]
        "$program" -d -i "$work/identity.txt" -o "$work/actual" "$work/data.age"
        busy cmp "$work/actual" "$work/data.txt" ;;
      graphviz)
        program=$(original_elf /usr/bin/dot)
        printf 'digraph G { Alpha -> Beta; }\n' > "$work/graph.dot"
        "$program" -Tplain "$work/graph.dot" > "$work/actual"
        busy grep -Fq 'node Alpha ' "$work/actual"
        busy grep -Fq 'node Beta ' "$work/actual"
        busy grep -Fq 'edge Alpha Beta ' "$work/actual" ;;
      xdelta3)
        program=$(original_elf /usr/bin/xdelta3)
        printf 'alpha one\nBETA two\ngamma three\n' > "$work/target.txt"
        "$program" -e -s "$work/data.txt" "$work/target.txt" "$work/delta.bin"
        [ -s "$work/delta.bin" ]
        "$program" -d -s "$work/data.txt" "$work/delta.bin" "$work/actual"
        busy cmp "$work/actual" "$work/target.txt" ;;
      patchelf)
        program=$(original_elf /usr/bin/patchelf)
        small_elf=$(original_elf /usr/bin/dos2unix)
        # The signed dos2unix PIE is only ~47 KiB on x86, and its real
        # PT_INTERP uses musl on all target ISAs. Never copy multi-MB Node
        # merely to exercise read/write of an ELF dynamic section.
        busy cp "$small_elf" "$work/elf-copy"
        "$program" --print-interpreter "$work/elf-copy" > "$work/interpreter"
        busy grep -Fq 'ld-musl' "$work/interpreter"
        "$program" --set-rpath /opt/naseed/m110 "$work/elf-copy"
        "$program" --print-rpath "$work/elf-copy" > "$work/actual"
        printf '/opt/naseed/m110\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      readelf)
        program=$(original_elf /usr/bin/readelf)
        "$program" -h /usr/bin/node > "$work/actual"
        busy grep -Fq 'ELF64' "$work/actual"
        busy grep -Fq 'Entry point address:' "$work/actual" ;;
      eureadelf)
        program=$(original_elf /usr/bin/eu-readelf)
        "$program" -h /usr/bin/node > "$work/actual"
        busy grep -Fq 'ELF Header:' "$work/actual"
        busy grep -Fq 'ELF64' "$work/actual" ;;
      pigz)
        program=$(original_elf /usr/bin/pigz)
        "$program" -c "$work/data.txt" > "$work/data.gz"
        [ -s "$work/data.gz" ]
        "$program" -dc "$work/data.gz" > "$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      rhash)
        program=$(original_elf /usr/bin/rhash)
        busy cp "$work/data.txt" "$work/clone.txt"
        first=$("$program" --sha256 "$work/data.txt" | busy awk '{print $1}')
        second=$("$program" --sha256 "$work/clone.txt" | busy awk '{print $1}')
        [ -n "$first" ] && [ "$first" = "$second" ]
        printf 'different content\n' > "$work/clone.txt"
        changed=$("$program" --sha256 "$work/clone.txt" | busy awk '{print $1}')
        [ "$first" != "$changed" ] ;;
      socat)
        program=$(original_elf /usr/bin/socat)
        "$program" -u "OPEN:$work/data.txt,rdonly" "CREATE:$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      dos2unix)
        program=$(original_elf /usr/bin/dos2unix)
        printf 'alpha\r\nbeta\r\n' > "$work/windows.txt"
        "$program" -n "$work/windows.txt" "$work/actual"
        printf 'alpha\nbeta\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      pcre2grep)
        program=$(original_elf /usr/bin/pcre2grep)
        printf 'id=42 tag=hello\nid=77 tag=world\n' > "$work/records.txt"
        "$program" -o '(?<=id=)[0-9]+' "$work/records.txt" > "$work/actual"
        printf '42\n77\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      bsdtar)
        program=$(original_elf /usr/bin/bsdtar)
        (cd "$work" && "$program" -cf archive.tar data.txt)
        [ -s "$work/archive.tar" ]
        busy mkdir -p "$work/out"
        (cd "$work/out" && "$program" -xf ../archive.tar)
        busy cmp "$work/data.txt" "$work/out/data.txt" ;;
      time)
        program=$(original_elf /usr/bin/time)
        "$program" -f 'EXIT:%x' -o "$work/actual" /bin/busybox true
        busy grep -Fxq 'EXIT:0' "$work/actual" ;;
      units)
        program=$(original_elf /usr/bin/units)
        "$program" -t '1 meter' 'centimeter' > "$work/actual"
        busy grep -Fxq '100' "$work/actual" ;;
      ag)
        program=$(original_elf /usr/bin/ag)
        "$program" -l '^beta two$' "$work/data.txt" > "$work/actual"
        busy grep -Fxq "$work/data.txt" "$work/actual" ;;
      zsh)
        program=$(original_elf /bin/zsh /usr/bin/zsh)
        "$program" -fc 'word=beta; print -r -- "${(U)word}"' > "$work/actual"
        printf 'BETA\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      ctags)
        program=$(original_elf /usr/bin/ctags)
        printf 'int answer(void) { return 42; }\n' > "$work/sample.c"
        "$program" -f "$work/tags" "$work/sample.c"
        busy grep -q '^answer[[:space:]]' "$work/tags" ;;
      b3sum)
        program=$(original_elf /usr/bin/b3sum)
        busy cp "$work/data.txt" "$work/clone.txt"
        first=$("$program" "$work/data.txt" | busy awk '{print $1}')
        second=$("$program" "$work/clone.txt" | busy awk '{print $1}')
        [ -n "$first" ] && [ "$first" = "$second" ]
        printf 'modified\n' > "$work/clone.txt"
        changed=$("$program" "$work/clone.txt" | busy awk '{print $1}')
        [ "$first" != "$changed" ] ;;
      xmlstarlet)
        program=$(original_elf /usr/bin/xmlstarlet /usr/bin/xml)
        printf '<root><item id="42">alpha</item></root>\n' > "$work/data.xml"
        "$program" sel -t -v '/root/item/@id' -n "$work/data.xml" > "$work/actual"
        printf '42\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      *)
        echo "[M110 UNKNOWN APPLICATION] $app" >&2
        return 2 ;;
    esac
}

if [ "${1:-}" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    run_case "$2"
    exit 0
fi

[ "$#" -eq 0 ] || { echo 'usage: m110-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in node age graphviz xdelta3 patchelf readelf eureadelf pigz rhash socat \
           dos2unix pcre2grep bsdtar time units ag zsh ctags b3sum xmlstarlet; do
    echo "[M110 CASE START] $app"
    if /bin/sh /tests/m110-batch-run.sh --case "$app"; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "M110_${upper}_PASS"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M110 ${upper} FAILED] original ELF or functional oracle, rc=$rc" >&2
        echo "M110_${upper}_FAIL"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M110_BATCH_PASS
else
    echo "[M110 BATCH FAIL] $failed/20 signed original Linux program cases" >&2
    echo M110_BATCH_FAIL
    exit 1
fi

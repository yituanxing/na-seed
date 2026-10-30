#!/bin/sh
# M109: fifteen new original, signed Alpine programs, one NaSeed guest boot.
# The outer process deliberately runs every case and reports each failure.
# Individual --case processes use errexit, so a broken oracle cannot pass.
# The host stages packages and ELFs; no application builds happen in NaSeed.

PATH=/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
TERM=vt100
GIT_CONFIG_NOSYSTEM=1
export PATH HOME LC_ALL LANG TERM GIT_CONFIG_NOSYSTEM

busy() { /bin/busybox "$@"; }

# Return the first actual ELF executable, not a BusyBox multicall alias or
# a wrapper script. Host staging separately verifies signed APK ownership,
# the target ELF machine and each dependency closure.
original_elf() {
    [ "$#" -gt 0 ] || return 1
    for candidate do
        [ -x "$candidate" ] || continue
        resolved=$(busy readlink -f "$candidate") || continue
        [ -x "$resolved" ] || continue
        case "$resolved" in
          /bin/busybox|/usr/bin/busybox) continue ;;
        esac
        magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        if [ "$magic" = 7f454c46 ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    echo "[M109 MISSING ORIGINAL ELF] $*" >&2
    return 1
}

run_case() {
    app=$1
    work="${M109_WORK_ROOT:-/tmp}/m109-$app"
    busy mkdir -p "$work"
    printf 'alpha one\nbeta two\ngamma three\n' > "$work/data.txt"
    case "$app" in
      m4)
        program=$(original_elf /usr/bin/m4 /bin/m4)
        busy cat > "$work/expressions.m4" <<'M109_M4'
define(`DOUBLE', `($1+$1)')dnl
DOUBLE(21)
M109_M4
        "$program" "$work/expressions.m4" > "$work/actual"
        printf '(21+21)\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      bison)
        program=$(original_elf /usr/bin/bison /bin/bison)
        busy cat > "$work/grammar.y" <<'M109_BISON'
%token NUMBER
%%
expr: NUMBER ;
%%
M109_BISON
        "$program" --no-lines -o "$work/parser.c" "$work/grammar.y"
        [ -s "$work/parser.c" ]
        busy grep -Fq 'yyparse (' "$work/parser.c"
        busy grep -Fq 'NUMBER' "$work/parser.c" ;;
      lz4)
        program=$(original_elf /usr/bin/lz4 /bin/lz4)
        "$program" -q -f "$work/data.txt" "$work/data.lz4"
        [ -s "$work/data.lz4" ]
        "$program" -q -d -c "$work/data.lz4" > "$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      brotli)
        program=$(original_elf /usr/bin/brotli /bin/brotli)
        "$program" --quality=5 --output="$work/data.br" "$work/data.txt"
        [ -s "$work/data.br" ]
        "$program" --decompress --stdout "$work/data.br" > "$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      xxhash)
        program=$(original_elf /usr/bin/xxhsum /bin/xxhsum)
        busy cp "$work/data.txt" "$work/clone.txt"
        first=$("$program" "$work/data.txt" | busy cut -d ' ' -f 1)
        second=$("$program" "$work/clone.txt" | busy cut -d ' ' -f 1)
        [ -n "$first" ] && [ "$first" = "$second" ]
        printf 'different input\n' > "$work/clone.txt"
        changed=$("$program" "$work/clone.txt" | busy cut -d ' ' -f 1)
        [ "$first" != "$changed" ] ;;
      pv)
        program=$(original_elf /usr/bin/pv /bin/pv)
        "$program" -q "$work/data.txt" > "$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      xmllint)
        program=$(original_elf /usr/bin/xmllint /bin/xmllint)
        printf '<root><item id="42">alpha</item></root>\n' > "$work/data.xml"
        "$program" --nonet --xpath 'string(/root/item/@id)' "$work/data.xml" > "$work/actual"
        # libxml2's XPath STRING dumper deliberately emits a trailing LF.
        printf '42\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      xsltproc)
        program=$(original_elf /usr/bin/xsltproc /bin/xsltproc)
        printf '<root><item id="42">alpha</item></root>\n' > "$work/data.xml"
        busy cat > "$work/style.xsl" <<'M109_XSLT'
<?xml version="1.0"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="text"/>
  <xsl:template match="/"><xsl:value-of select="/root/item/@id"/></xsl:template>
</xsl:stylesheet>
M109_XSLT
        "$program" --nonet "$work/style.xsl" "$work/data.xml" > "$work/actual"
        printf 42 > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      curl)
        program=$(original_elf /usr/bin/curl /bin/curl)
        "$program" --fail --silent --show-error --max-time 15 "file://$work/data.txt" > "$work/actual"
        busy cmp "$work/data.txt" "$work/actual" ;;
      cpio)
        program=$(original_elf /usr/bin/cpio /bin/cpio)
        (cd "$work" && printf 'data.txt\n' | "$program" -o -H newc --quiet) > "$work/archive.cpio"
        [ -s "$work/archive.cpio" ]
        busy mkdir -p "$work/out"
        (cd "$work/out" && "$program" -id --quiet < "$work/archive.cpio")
        busy cmp "$work/data.txt" "$work/out/data.txt" ;;
      7zip)
        program=$(original_elf /usr/bin/7z /usr/bin/7zz /bin/7z /bin/7zz)
        (cd "$work" && "$program" a -y -bd archive.7z data.txt > /dev/null)
        [ -s "$work/archive.7z" ]
        busy mkdir -p "$work/out"
        (cd "$work/out" && "$program" x -y -bd ../archive.7z > /dev/null)
        busy cmp "$work/data.txt" "$work/out/data.txt" ;;
      msgfmt)
        program=$(original_elf /usr/bin/msgfmt /bin/msgfmt)
        busy cat > "$work/messages.po" <<'M109_PO'
msgid ""
msgstr ""
"Content-Type: text/plain; charset=UTF-8\n"

msgid "hello"
msgstr "bonjour"
M109_PO
        "$program" -o "$work/messages.mo" "$work/messages.po"
        [ -s "$work/messages.mo" ]
        header=$(busy dd if="$work/messages.mo" bs=1 count=4 2>/dev/null |
                 busy od -An -tx1 | busy tr -d ' \n')
        [ "$header" = de120495 ]
        # The MO must contain the actual compiled translation, not merely a
        # syntactically valid empty header produced by a broken msgfmt.
        busy grep -aFq 'bonjour' "$work/messages.mo" ;;
      gpg)
        program=$(original_elf /usr/bin/gpg /bin/gpg)
        busy mkdir -p "$work/gnupg"
        busy chmod 700 "$work/gnupg"
        GNUPGHOME="$work/gnupg" "$program" --batch --no-tty --no-options --yes \
            --enarmor --output "$work/data.asc" "$work/data.txt"
        [ -s "$work/data.asc" ]
        GNUPGHOME="$work/gnupg" "$program" --batch --no-tty --no-options --yes \
            --dearmor --output "$work/actual" "$work/data.asc"
        busy cmp "$work/data.txt" "$work/actual" ;;
      perl)
        program=$(original_elf /usr/bin/perl /bin/perl)
        "$program" -e 'open my $f, "<", $ARGV[0] or die $!; local $/; my $s=<$f>; $s =~ s/beta/BETA/; print $s;' \
            "$work/data.txt" > "$work/actual"
        printf 'alpha one\nBETA two\ngamma three\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      ruby)
        program=$(original_elf /usr/bin/ruby /bin/ruby)
        if "$program" -e 's = File.read(ARGV[0]); File.write(ARGV[1], s.upcase)' \
             "$work/data.txt" "$work/actual"; then
            :
        else
            original_rc=$?
            echo "[M109 RUBY DIAG] original runtime rc=$original_rc; retrying same functionality with Ruby -d -w diagnostics" >&2
            # The debugging run can explain a silent Ruby startup failure,
            # but may NEVER change the original functional gate's result.
            "$program" -d -w -e 's = File.read(ARGV[0]); File.write(ARGV[1], s.upcase)' \
                "$work/data.txt" "$work/diagnostic-actual" >&2 || :
            return "$original_rc"
        fi
        printf 'ALPHA ONE\nBETA TWO\nGAMMA THREE\n' > "$work/expected"
        busy cmp "$work/actual" "$work/expected" ;;
      *)
        echo "[M109 UNKNOWN APPLICATION] $app" >&2
        return 2 ;;
    esac
}

if [ "${1:-}" = --case ]; then
    [ "$#" -eq 2 ] || exit 2
    set -eu
    run_case "$2"
    exit 0
fi

[ "$#" -eq 0 ] || { echo 'usage: m109-batch-run.sh [--case APP]' >&2; exit 2; }
failed=0
for app in m4 bison lz4 brotli xxhash pv xmllint xsltproc \
           curl cpio 7zip msgfmt gpg perl ruby; do
    if /bin/sh /tests/m109-batch-run.sh --case "$app"; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "M109_${upper}_PASS"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M109 ${upper} FAILED] original ELF or functional oracle, rc=$rc" >&2
        echo "M109_${upper}_FAIL"
        failed=$((failed + 1))
    fi
done
if [ "$failed" -eq 0 ]; then
    echo M109_BATCH_PASS
else
    echo "[M109 BATCH FAIL] $failed/15 signed original Linux program cases" >&2
    echo M109_BATCH_FAIL
    exit 1
fi

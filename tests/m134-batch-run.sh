#!/bin/sh
# M134: census of 15 new signed native Alpine 3.23 ELF programs per ISA.
# Test actual transformations and file I/O, retain ALL failing case traces.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LC_ALL=C
LANG=C
export PATH HOME LC_ALL LANG
busy() { /bin/busybox "$@"; }

original_elf() {
    candidate=$1
    [ -x "$candidate" ] || return 1
    resolved=$(busy readlink -f "$candidate") || return 1
    case "$resolved" in /bin/busybox|/usr/bin/busybox) return 1 ;; esac
    magic=$(busy dd if="$resolved" bs=1 count=4 2>/dev/null |
            busy od -An -tx1 | busy tr -d ' \n')
    [ "$magic" = 7f454c46 ] || return 1
    printf '%s\n' "$candidate"
}

graph_counts() {
    file=$1
    nodes=$2
    edges=$3
    /usr/bin/gc -n "$file" > "$work/gc.nodes"
    /usr/bin/gc -e "$file" > "$work/gc.edges"
    busy awk -v expected="$nodes" '$1 == expected {found=1} END {exit !found}' "$work/gc.nodes"
    busy awk -v expected="$edges" '$1 == expected {found=1} END {exit !found}' "$work/gc.edges"
}

seed_lua() {
    busy printf 'alpha\nbeta\n' > "$work/input"
    busy printf 'local f=assert(io.open(arg[1], "rb"))\nlocal s=f:read("*a")\nf:close()\nlocal sum=0\nfor i=1,#s do sum=sum+s:byte(i) end\nprint(sum)\n' > "$work/sum.lua"
}

run_case() {
    app=$1
    work="/tmp/m134-$app"
    busy mkdir -p "$work"
    case "$app" in
      graphml2gv)
        exe=$(original_elf /usr/bin/graphml2gv)
        busy printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<graphml xmlns="http://graphml.graphdrawing.org/xmlns">' '<graph id="G" edgedefault="directed">' '<node id="alpha"/><node id="beta"/>' '<edge id="e1" source="alpha" target="beta"/>' '</graph></graphml>' > "$work/input.graphml"
        "$exe" "$work/input.graphml" > "$work/result.gv"
        graph_counts "$work/result.gv" 2 1
        busy grep -Fq 'alpha' "$work/result.gv"
        busy grep -Fq 'beta' "$work/result.gv" ;;
      gvgen)
        exe=$(original_elf /usr/bin/gvgen)
        "$exe" -c 5 > "$work/cycle.gv"
        graph_counts "$work/cycle.gv" 5 5 ;;
      gvpack)
        exe=$(original_elf /usr/bin/gvpack)
        busy printf 'digraph A {a->b;}\n' > "$work/one.gv"
        busy printf 'digraph B {c->d;}\n' > "$work/two.gv"
        /usr/bin/dot "$work/one.gv" > "$work/one.layout"
        /usr/bin/dot "$work/two.gv" > "$work/two.layout"
        "$exe" -g "$work/one.layout" "$work/two.layout" > "$work/merged.gv"
        graph_counts "$work/merged.gv" 4 2 ;;
      gvpr)
        exe=$(original_elf /usr/bin/gvpr)
        busy printf 'digraph G {a [color=blue]; b [color=green]; a->b;}\n' > "$work/colors.gv"
        "$exe" -c 'N[color=="blue"]{color="red"}' "$work/colors.gv" > "$work/recolored.gv"
        busy grep -Fq 'color=red' "$work/recolored.gv"
        busy grep -Fq 'color=green' "$work/recolored.gv"
        graph_counts "$work/recolored.gv" 2 1 ;;
      gxl2gv)
        exe=$(original_elf /usr/bin/gxl2gv)
        busy printf 'digraph G {alpha->beta;beta->gamma;}\n' > "$work/source.gv"
        "$exe" -g "$work/source.gv" > "$work/converted.gxl"
        busy grep -Fq '<gxl' "$work/converted.gxl"
        "$exe" -d "$work/converted.gxl" > "$work/restored.gv"
        graph_counts "$work/restored.gv" 3 2 ;;
      nop)
        exe=$(original_elf /usr/bin/nop)
        busy printf 'digraph G{ a->b; b->c; }\n' > "$work/ugly.gv"
        "$exe" "$work/ugly.gv" > "$work/pretty.gv"
        graph_counts "$work/pretty.gv" 3 2
        "$exe" -p "$work/pretty.gv" > "$work/check"
        [ ! -s "$work/check" ] ;;
      sccmap)
        exe=$(original_elf /usr/bin/sccmap)
        busy printf 'digraph G {a->b;b->a;b->c;c->d;d->c;}\n' > "$work/cyclic.gv"
        "$exe" -S "$work/cyclic.gv" > "$work/components.gv"
        [ -s "$work/components.gv" ]
        /usr/bin/nop -p "$work/components.gv" > "$work/checked"
        busy grep -Eq 'a[[:space:]]*->[[:space:]]*b' "$work/components.gv"
        busy grep -Eq 'c[[:space:]]*->[[:space:]]*d' "$work/components.gv" ;;
      lua54)
        exe=$(original_elf /usr/bin/lua5.4)
        seed_lua
        "$exe" "$work/sum.lua" "$work/input" > "$work/result"
        busy grep -Fxq 950 "$work/result" ;;
      luac54)
        exe=$(original_elf /usr/bin/luac5.4)
        seed_lua
        "$exe" -o "$work/compiled.luac" "$work/sum.lua"
        [ -s "$work/compiled.luac" ]
        magic=$(busy dd if="$work/compiled.luac" bs=1 count=4 2>/dev/null | busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 1b4c7561 ]
        /usr/bin/lua5.4 "$work/compiled.luac" "$work/input" > "$work/result"
        busy grep -Fxq 950 "$work/result" ;;
      dash)
        exe=$(original_elf /usr/bin/dash)
        busy printf 'set -eu\nn=0\nfor k in 1 2 3 4 5; do n=$((n+k)); done\nprintf "sum=%%s\\n" "$n" > "$1"\n' > "$work/sum.sh"
        "$exe" "$work/sum.sh" "$work/result"
        busy grep -Fxq 'sum=15' "$work/result" ;;
      mawk)
        exe=$(original_elf /usr/bin/mawk)
        busy printf 'alpha,7\nbeta,11\nalpha,5\n' > "$work/rows.csv"
        "$exe" -F, '$1=="alpha"{s+=$2;n++} END{printf "%d:%d\n",n,s}' "$work/rows.csv" > "$work/result"
        busy grep -Fxq '2:12' "$work/result" ;;
      xxd)
        exe=$(original_elf /usr/bin/xxd)
        busy printf '\000\001\177\200\377NaSeed\n' > "$work/blob"
        "$exe" -p "$work/blob" > "$work/hex"
        busy grep -Fq '00017f80ff4e61536565640a' "$work/hex"
        "$exe" -r -p "$work/hex" > "$work/decoded"
        busy cmp -s "$work/blob" "$work/decoded" ;;
      gperf)
        exe=$(original_elf /usr/bin/gperf)
        busy printf 'alpha\nbeta\ngamma\n' > "$work/words"
        "$exe" -N m134_lookup "$work/words" > "$work/lookup.c"
        busy grep -Fq 'm134_lookup' "$work/lookup.c"
        busy grep -Fq 'alpha' "$work/lookup.c"
        busy grep -Fq 'beta' "$work/lookup.c"
        busy grep -Fq 'gamma' "$work/lookup.c" ;;
      ninja)
        exe=$(original_elf /usr/lib/ninja-build/bin/ninja)
        busy printf 'rule cp\n  command = /bin/busybox cp $in $out\nbuild out.txt: cp in.txt\ndefault out.txt\n' > "$work/build.ninja"
        busy printf 'M134 real dependency graph\n' > "$work/in.txt"
        "$exe" -C "$work" > "$work/first"
        busy cmp -s "$work/in.txt" "$work/out.txt"
        "$exe" -C "$work" > "$work/second"
        busy grep -Fq 'no work to do' "$work/second" ;;
      fish)
        exe=$(original_elf /usr/bin/fish)
        "$exe" -c 'set n (math 6 \* 7); test "$n" -eq 42; and string match -r dog "nice dog"' > "$work/result"
        busy grep -Fxq dog "$work/result" ;;
      *) echo "[M134 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M134_TRACE:-0}" = 1 ]; then
        PS4='[M134 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in graphml2gv gvgen gvpack gvpr gxl2gv nop sccmap \
           lua54 luac54 dash mawk xxd gperf ninja fish; do
    echo "[M134 CASE START] $app"
    case_log="/tmp/m134-$app-case.log"
    if M134_TRACE=1 /bin/sh /tests/m134-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M134_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M134 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -140 "$case_log" >&2 || :
        for detail in result.gv merged.gv recolored.gv converted.gxl restored.gv \
                      components.gv gc.nodes gc.edges result pretty.gv \
                      compiled.luac hex lookup.c first second; do
            if [ -s "/tmp/m134-$app/$detail" ]; then
                echo "[M134 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 550 "/tmp/m134-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M134_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M134 BATCH FAIL] $failed/15 original ELFs (full census)" >&2
    echo M134_BATCH_FAIL
    exit 1
fi
echo M134_BATCH_PASS

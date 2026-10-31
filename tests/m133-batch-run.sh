#!/bin/sh
# M133: 15 independently signed original Alpine v3.23 ELF cases, with real
# graph algorithms, stream decoding, text editing and writable FAT semantics.
# Run ALL cases in one guest census and retain every failed original trace.
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

seed_graphs() {
    busy printf 'digraph A {a->b;b->c;c->a;}\n' > "$work/cycle.gv"
    busy printf 'graph B {a--b;b--c;c--a;c--d;d--e;e--c;}\n' > "$work/blocks.gv"
    busy printf 'graph C {a--b;c--d;e;}\n' > "$work/components.gv"
    busy printf 'digraph D {a->b [len=2];b->c [len=3];a->c [len=10];}\n' > "$work/path.gv"
    busy printf 'digraph T {a->b;b->c;a->c;}\n' > "$work/redundant.gv"
}

seed_compressed() {
    busy printf 'NaSeed M133: native decoder parity. abcabcabc\n' > "$work/plain"
    /usr/bin/xz --format=lzma -c "$work/plain" > "$work/payload.lzma"
    /usr/bin/xz -c "$work/plain" > "$work/payload.xz"
    [ -s "$work/payload.lzma" ] && [ -s "$work/payload.xz" ]
}

run_case() {
    app=$1
    work="/tmp/m133-$app"
    busy mkdir -p "$work"
    case "$app" in
      acyclic)
        exe=$(original_elf /usr/bin/acyclic)
        seed_graphs
        # Acyclic's documented rc=1 on an INPUT cycle means edges were
        # reversed successfully. Rechecking the output must now return 0.
        rc=0
        "$exe" "$work/cycle.gv" > "$work/dag.gv" || rc=$?
        [ "$rc" -eq 1 ] && [ -s "$work/dag.gv" ]
        "$exe" -n "$work/dag.gv" > "$work/verified.gv"
        /usr/bin/gc -e "$work/dag.gv" > "$work/edges"
        busy awk '$1==3 {ok=1} END {exit !ok}' "$work/edges" ;;
      bcomps)
        exe=$(original_elf /usr/bin/bcomps)
        seed_graphs
        rc=0
        "$exe" "$work/blocks.gv" > "$work/blocks.out" || rc=$?
        [ "$rc" -le 2 ] && [ -s "$work/blocks.out" ]
        busy grep -Fq 'B_bcc_0' "$work/blocks.out"
        busy grep -Fq 'B_bcc_1' "$work/blocks.out" ;;
      ccomps)
        exe=$(original_elf /usr/bin/ccomps)
        seed_graphs
        "$exe" -X a "$work/components.gv" > "$work/picked.gv"
        [ -s "$work/picked.gv" ]
        busy grep -Eq 'a[[:space:]]*--[[:space:]]*b' "$work/picked.gv"
        ! busy grep -Eq 'c[[:space:]]*--|d[[:space:]]*--' "$work/picked.gv" ;;
      dijkstra)
        exe=$(original_elf /usr/bin/dijkstra)
        seed_graphs
        "$exe" -dp a "$work/path.gv" > "$work/distances.gv"
        busy grep -Eq 'maxdist[[:space:]]*=[[:space:]]*5' "$work/distances.gv"
        busy grep -Eq 'dist[[:space:]]*=[[:space:]]*5' "$work/distances.gv"
        busy grep -Eq 'prev[[:space:]]*=[[:space:]]*b' "$work/distances.gv" ;;
      gc)
        exe=$(original_elf /usr/bin/gc)
        seed_graphs
        "$exe" -n "$work/components.gv" > "$work/nodes"
        "$exe" -e "$work/components.gv" > "$work/edges"
        "$exe" -c "$work/components.gv" > "$work/components"
        busy awk '$1==5 {ok=1} END {exit !ok}' "$work/nodes"
        busy awk '$1==2 {ok=1} END {exit !ok}' "$work/edges"
        busy awk '$1==3 {ok=1} END {exit !ok}' "$work/components" ;;
      gml2gv)
        exe=$(original_elf /usr/bin/gml2gv)
        busy printf 'graph [\n directed 1\n node [ id 0 label "alpha" ]\n node [ id 1 label "beta" ]\n edge [ source 0 target 1 ]\n]\n' > "$work/seed.gml"
        "$exe" "$work/seed.gml" > "$work/back.gv"
        [ -s "$work/back.gv" ]
        busy grep -Fq 'alpha' "$work/back.gv"
        busy grep -Fq 'beta' "$work/back.gv"
        /usr/bin/gc -n "$work/back.gv" > "$work/count"
        busy awk '$1==2 {ok=1} END {exit !ok}' "$work/count" ;;
      gv2gml)
        exe=$(original_elf /usr/bin/gv2gml)
        seed_graphs
        "$exe" "$work/path.gv" > "$work/converted.gml"
        busy grep -Fq 'graph [' "$work/converted.gml"
        busy grep -Fq 'edge [' "$work/converted.gml"
        /usr/bin/gml2gv "$work/converted.gml" > "$work/roundtrip.gv"
        /usr/bin/gc -n "$work/roundtrip.gv" > "$work/nodes"
        /usr/bin/gc -e "$work/roundtrip.gv" > "$work/edges"
        busy awk '$1==3 {ok=1} END {exit !ok}' "$work/nodes"
        busy awk '$1==3 {ok=1} END {exit !ok}' "$work/edges" ;;
      tred)
        exe=$(original_elf /usr/bin/tred)
        seed_graphs
        "$exe" "$work/redundant.gv" > "$work/reduced.gv"
        busy grep -Eq 'a[[:space:]]*->[[:space:]]*b' "$work/reduced.gv"
        busy grep -Eq 'b[[:space:]]*->[[:space:]]*c' "$work/reduced.gv"
        ! busy grep -Eq 'a[[:space:]]*->[[:space:]]*c' "$work/reduced.gv"
        /usr/bin/gc -e "$work/reduced.gv" > "$work/edges"
        busy awk '$1==2 {ok=1} END {exit !ok}' "$work/edges" ;;
      lzmadec)
        exe=$(original_elf /usr/bin/lzmadec)
        seed_compressed
        "$exe" "$work/payload.lzma" > "$work/restored"
        busy cmp -s "$work/plain" "$work/restored" ;;
      lzmainfo)
        exe=$(original_elf /usr/bin/lzmainfo)
        seed_compressed
        "$exe" "$work/payload.lzma" > "$work/info"
        busy grep -Fq 'Dictionary size:' "$work/info"
        busy grep -Fq 'Literal context bits:' "$work/info" ;;
      xzdec)
        exe=$(original_elf /usr/bin/xzdec)
        seed_compressed
        "$exe" "$work/payload.xz" > "$work/restored"
        busy cmp -s "$work/plain" "$work/restored" ;;
      sdiff)
        exe=$(original_elf /usr/bin/sdiff)
        busy printf 'alpha\nshared\ngamma\n' > "$work/left"
        busy printf 'beta\nshared\ndelta\n' > "$work/right"
        rc=0
        "$exe" -w 80 "$work/left" "$work/right" > "$work/differences" || rc=$?
        [ "$rc" -eq 1 ]
        busy grep -Fq 'alpha' "$work/differences"
        busy grep -Fq 'beta' "$work/differences"
        busy grep -Fq '|' "$work/differences" ;;
      ed)
        exe=$(original_elf /bin/ed)
        busy printf 'alpha\nbeta\ngamma\n' > "$work/source"
        busy printf '2s/beta/BETA/\n$a\nomega\n.\nw\nq\n' |
            "$exe" -s "$work/source"
        busy printf 'alpha\nBETA\ngamma\nomega\n' > "$work/expected"
        busy cmp -s "$work/source" "$work/expected" ;;
      mtools)
        exe=$(original_elf /usr/bin/mtools)
        busy printf 'NaSeed FAT data round trip M133\n' > "$work/payload.txt"
        "$exe" -c mformat -C -f 1440 -i "$work/fat.img" ::
        [ -s "$work/fat.img" ]
        "$exe" -c mcopy -i "$work/fat.img" "$work/payload.txt" ::PAYLOAD.TXT
        "$exe" -c mdir -i "$work/fat.img" :: > "$work/listing"
        busy grep -Fq 'PAYLOAD' "$work/listing"
        "$exe" -c mtype -i "$work/fat.img" ::PAYLOAD.TXT > "$work/recovered"
        busy cmp -s "$work/payload.txt" "$work/recovered" ;;
      mkmanifest)
        exe=$(original_elf /usr/bin/mkmanifest)
        (cd "$work" &&
            "$exe" myverylongname.txt > restore.sh &&
            busy grep -Fq 'mv myverylo.txt myverylongname.txt' restore.sh &&
            busy printf 'NaSeed M133 long FAT filename restoration\n' > myverylo.txt &&
            /bin/sh restore.sh &&
            busy printf 'NaSeed M133 long FAT filename restoration\n' > expected &&
            busy cmp -s myverylongname.txt expected) ;;
      *) echo "[M133 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M133_TRACE:-0}" = 1 ]; then
        PS4='[M133 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in acyclic bcomps ccomps dijkstra gc gml2gv gv2gml tred \
           lzmadec lzmainfo xzdec sdiff ed mtools mkmanifest; do
    echo "[M133 CASE START] $app"
    case_log="/tmp/m133-$app-case.log"
    if M133_TRACE=1 /bin/sh /tests/m133-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M133_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M133 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -120 "$case_log" >&2 || :
        for detail in dag.gv blocks.out picked.gv distances.gv nodes edges \
                      back.gv converted.gml roundtrip.gv info differences \
                      listing restore.sh; do
            if [ -s "/tmp/m133-$app/$detail" ]; then
                echo "[M133 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 550 "/tmp/m133-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M133_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M133 BATCH FAIL] $failed/15 original ELFs (full census)" >&2
    echo M133_BATCH_FAIL
    exit 1
fi
echo M133_BATCH_PASS

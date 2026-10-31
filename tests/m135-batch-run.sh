#!/bin/sh
# M135: 15 newly signed original Alpine ELF programs, all functional oracles.
# Each original is checked as a real ELF. Keep every failed case's trace.
PATH=/usr/sbin:/sbin:/usr/bin:/bin
HOME=/tmp
LANG=C
LC_ALL=C
export PATH HOME LANG LC_ALL
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
    busy awk -v n="$nodes" '$1==n{ok=1} END{exit !ok}' "$work/gc.nodes"
    busy awk -v n="$edges" '$1==n{ok=1} END{exit !ok}' "$work/gc.edges"
}

seed_jpeg() {
    busy printf 'P3\n2 1\n255\n255 0 0 0 255 0\n' > "$work/source.ppm"
    /usr/bin/cjpeg -quality 85 "$work/source.ppm" > "$work/source.jpg"
    [ -s "$work/source.jpg" ]
}

seed_gdbm() {
    busy printf 'store alpha one\nstore beta two\nquit\n' |
        /usr/bin/gdbmtool -N "$work/source.gdbm" > "$work/created.log"
    [ -s "$work/source.gdbm" ]
}

seed_pnm() {
    busy printf 'P3\n2 2\n255\n255 0 0\n0 255 0\n0 0 255\n255 255 255\n' > "$work/source.ppm"
}

run_case() {
    app=$1
    work="/tmp/m135-$app"
    busy mkdir -p "$work"
    case "$app" in
      cmake)
        exe=$(original_elf /usr/bin/cmake)
        busy printf '%s\n' 'file(WRITE "${CMAKE_CURRENT_LIST_DIR}/payload.txt" "M135-CMAKE-FILE-IO\\n")' 'file(SHA256 "${CMAKE_CURRENT_LIST_DIR}/payload.txt" digest)' 'message(STATUS "HASH=${digest}")' > "$work/script.cmake"
        "$exe" -P "$work/script.cmake" > "$work/output"
        [ -s "$work/payload.txt" ]
        expected=$(busy sha256sum "$work/payload.txt" | busy awk '{print $1}')
        busy grep -Fq "HASH=$expected" "$work/output" ;;
      cpack)
        exe=$(original_elf /usr/bin/cpack)
        busy mkdir -p "$work/input" "$work/archive"
        busy printf 'Native NaSeed signed CPack archive M135\n' > "$work/input/payload.txt"
        busy printf 'set(CPACK_PACKAGE_NAME "naseed")\nset(CPACK_PACKAGE_VERSION "1.0")\nset(CPACK_PACKAGE_FILE_NAME "naseed-m135")\nset(CPACK_PACKAGE_DESCRIPTION_SUMMARY "M135 native test")\nset(CPACK_PACKAGE_DESCRIPTION "NaSeed M135 real native file archive")\nset(CPACK_GENERATOR "TGZ")\nset(CPACK_INSTALLED_DIRECTORIES "%s/input;share")\n' "$work" > "$work/CPackConfig.cmake"
        if ! "$exe" -G TGZ --config "$work/CPackConfig.cmake" \
             -B "$work/archive" > "$work/pack.log" 2>&1; then
            # The native QEMU serial can exceed the host's failure tail.
            # Prefix the original CPack diagnostic so the host grep always
            # captures the real error, even after all 15 cases have run.
            busy sed 's/^/[M135 CPACK LOG] /' "$work/pack.log" >&2 || :
            return 1
        fi
        archive="$work/archive/naseed-m135.tar.gz"
        [ -s "$archive" ]
        busy tar -tzf "$archive" > "$work/members"
        busy grep -Fq 'share/payload.txt' "$work/members" ;;
      ctest)
        exe=$(original_elf /usr/bin/ctest)
        busy printf 'add_test(m135_positive "/bin/busybox" "true")\n' > "$work/CTestTestfile.cmake"
        "$exe" --test-dir "$work" --output-on-failure > "$work/tests.log"
        busy grep -Fq '100% tests passed' "$work/tests.log"
        busy grep -Fq '1/1' "$work/tests.log" ;;
      wrjpgcom)
        exe=$(original_elf /usr/bin/wrjpgcom)
        seed_jpeg
        "$exe" -comment 'NaSeed-M135-COM' "$work/source.jpg" > "$work/tagged.jpg"
        [ -s "$work/tagged.jpg" ]
        /usr/bin/rdjpgcom "$work/tagged.jpg" > "$work/comment"
        busy grep -Fq 'NaSeed-M135-COM' "$work/comment" ;;
      rdjpgcom)
        exe=$(original_elf /usr/bin/rdjpgcom)
        seed_jpeg
        /usr/bin/wrjpgcom -comment 'M135-original-JPEG-comment' "$work/source.jpg" > "$work/tagged.jpg"
        "$exe" "$work/tagged.jpg" > "$work/comment"
        busy grep -Fxq 'M135-original-JPEG-comment' "$work/comment" ;;
      tidy)
        exe=$(original_elf /usr/bin/tidy)
        busy printf '%s\n' '<!DOCTYPE html>' '<html><head><title>NaSeed M135</title></head>' '<body><p>native HTML normalization</p><ul><li>one</li><li>two</li></ul></body></html>' > "$work/compact.html"
        rc=0
        "$exe" -asxhtml -utf8 -quiet -indent "$work/compact.html" > "$work/normalized.xhtml" 2> "$work/warnings" || rc=$?
        [ "$rc" -le 1 ] && [ -s "$work/normalized.xhtml" ]
        busy grep -Fq '<html' "$work/normalized.xhtml"
        busy grep -Fq 'native HTML normalization' "$work/normalized.xhtml"
        busy grep -Fq '<li>two</li>' "$work/normalized.xhtml" ;;
      gdbmtool)
        exe=$(original_elf /usr/bin/gdbmtool)
        seed_gdbm
        busy printf 'fetch alpha\nfetch beta\ncount\nquit\n' |
            "$exe" -N "$work/source.gdbm" > "$work/read.log"
        busy grep -Fq 'one' "$work/read.log"
        busy grep -Fq 'two' "$work/read.log" ;;
      gdbm_dump)
        exe=$(original_elf /usr/bin/gdbm_dump)
        seed_gdbm
        "$exe" "$work/source.gdbm" "$work/export.dump"
        [ -s "$work/export.dump" ]
        /usr/bin/gdbm_load -n "$work/export.dump" "$work/restored.gdbm"
        busy printf 'fetch alpha\nquit\n' |
            /usr/bin/gdbmtool -N "$work/restored.gdbm" > "$work/read.log"
        busy grep -Fq 'one' "$work/read.log" ;;
      gdbm_load)
        exe=$(original_elf /usr/bin/gdbm_load)
        seed_gdbm
        /usr/bin/gdbm_dump "$work/source.gdbm" "$work/export.dump"
        "$exe" -n "$work/export.dump" "$work/restored.gdbm"
        [ -s "$work/restored.gdbm" ]
        busy printf 'fetch alpha\nfetch beta\nquit\n' |
            /usr/bin/gdbmtool -N "$work/restored.gdbm" > "$work/read.log"
        busy grep -Fq 'one' "$work/read.log"
        busy grep -Fq 'two' "$work/read.log" ;;
      mm2gv)
        exe=$(original_elf /usr/bin/mm2gv)
        busy printf '%%%%MatrixMarket matrix coordinate real general\n3 3 2\n1 2 1.0\n2 3 2.0\n' > "$work/adjacency.mtx"
        "$exe" -U0 "$work/adjacency.mtx" > "$work/result.gv"
        graph_counts "$work/result.gv" 3 2 ;;
      prune)
        exe=$(original_elf /usr/bin/prune)
        busy printf 'digraph G {A->B;A->C;B->D;B->E;}\n' > "$work/branches.gv"
        "$exe" -n B -N color=red "$work/branches.gv" > "$work/result.gv"
        graph_counts "$work/result.gv" 3 2
        busy grep -Fq 'color=red' "$work/result.gv"
        ! busy grep -Eq 'B[[:space:]]*->[[:space:]]*[DE]' "$work/result.gv" ;;
      unflatten)
        exe=$(original_elf /usr/bin/unflatten)
        busy printf 'digraph G {root->a;root->b;root->c;root->d;}\n' > "$work/fan.gv"
        "$exe" -l 3 "$work/fan.gv" > "$work/result.gv"
        graph_counts "$work/result.gv" 5 4
        busy grep -Fq 'minlen=' "$work/result.gv" ;;
      pnmpsnr)
        exe=$(original_elf /usr/bin/pnmpsnr)
        seed_pnm
        "$exe" -target=100 "$work/source.ppm" "$work/source.ppm" > "$work/score"
        busy grep -Fxq match "$work/score"
        busy printf 'P3\n2 2\n255\n0 0 0\n0 255 0\n0 0 255\n255 255 255\n' > "$work/changed.ppm"
        "$exe" -target=100 "$work/source.ppm" "$work/changed.ppm" > "$work/different"
        busy grep -Fxq nomatch "$work/different" ;;
      pnmtile)
        exe=$(original_elf /usr/bin/pnmtile)
        seed_pnm
        "$exe" 5 3 "$work/source.ppm" > "$work/tiled.ppm"
        [ -s "$work/tiled.ppm" ]
        /usr/bin/pamfile "$work/tiled.ppm" > "$work/dimensions"
        busy grep -Fq '5 by 3' "$work/dimensions"
        /usr/bin/pamcut -left=0 -top=0 -width=2 -height=2 "$work/tiled.ppm" > "$work/cropped.ppm"
        /usr/bin/pnmpsnr -target=100 "$work/source.ppm" "$work/cropped.ppm" > "$work/score"
        busy grep -Fxq match "$work/score" ;;
      pnmtorast)
        exe=$(original_elf /usr/bin/pnmtorast)
        seed_pnm
        "$exe" -standard "$work/source.ppm" > "$work/converted.ras"
        [ -s "$work/converted.ras" ]
        magic=$(busy dd if="$work/converted.ras" bs=1 count=4 2>/dev/null |
                busy od -An -tx1 | busy tr -d ' \n')
        [ "$magic" = 59a66a95 ]
        /usr/bin/rasttopnm "$work/converted.ras" > "$work/restored.ppm"
        /usr/bin/pnmpsnr -target=100 "$work/source.ppm" "$work/restored.ppm" > "$work/score"
        busy grep -Fxq match "$work/score" ;;
      *) echo "[M135 UNKNOWN ORIGINAL ELF CASE] $app" >&2; return 2 ;;
    esac
}

if [ "$#" -eq 2 ] && [ "$1" = --case ]; then
    set -eu
    if [ "${M135_TRACE:-0}" = 1 ]; then
        PS4='[M135 TRACE] '
        set -x
    fi
    run_case "$2"
    exit 0
fi
[ "$#" -eq 0 ] || exit 2
failed=0
for app in cmake cpack ctest wrjpgcom rdjpgcom tidy \
           gdbmtool gdbm_dump gdbm_load mm2gv prune unflatten \
           pnmpsnr pnmtile pnmtorast; do
    echo "[M135 CASE START] $app"
    case_log="/tmp/m135-$app-case.log"
    if M135_TRACE=1 /bin/sh /tests/m135-batch-run.sh --case "$app" > "$case_log" 2>&1; then
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        printf 'M135_%s_PASS\n' "$upper"
    else
        rc=$?
        upper=$(printf '%s' "$app" | busy tr '[:lower:]' '[:upper:]')
        echo "[M135 $upper FAILED] original signed ELF rc=$rc" >&2
        busy tail -130 "$case_log" >&2 || :
        for detail in output pack.log members tests.log comment normalized.xhtml \
                      warnings created.log read.log export.dump result.gv gc.nodes \
                      gc.edges score different dimensions tiled.ppm converted.ras; do
            if [ -s "/tmp/m135-$app/$detail" ]; then
                echo "[M135 FAILED OUTPUT $app/$detail]" >&2
                busy head -c 500 "/tmp/m135-$app/$detail" >&2 || :
                echo >&2
            fi
        done
        printf 'M135_%s_FAIL\n' "$upper"
        failed=$((failed+1))
    fi
done
if [ "$failed" -ne 0 ]; then
    echo "[M135 BATCH FAIL] $failed/15 original ELFs (full census)" >&2
    echo M135_BATCH_FAIL
    exit 1
fi
echo M135_BATCH_PASS

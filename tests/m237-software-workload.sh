#!/bin/sh
# One 15-entrypoint official Alpine cohort: ORIGINAL ELF, actual operation,
# durable first-QEMU ext4 input, independent second-QEMU fresh output.
set -eu
test "$#" -eq 3 && test "$1" = --exercise || exit 2
case "${NAS_M237_BOOT_PHASE:-}" in first|reopen) ;; *) exit 2 ;; esac
phase=$NAS_M237_BOOT_PHASE case_id=$2 elf=$3
busy=/bin/busybox
test -x "/$elf"
test "$($busy od -An -tx1 -N4 "/$elf" | $busy tr -d ' \t\r\n')" = 7f454c46 || exit 1
state=/root/m237-state
target="$state/$case_id.$phase"
test ! -e "$target" || { echo 'M237: preseeded semantic result' >&2; exit 1; }
mkdir -p "$target"

fail() {
    # dash/BusyBox ash with set -e does NOT exit on an early failed `&&`
    # command. Every multi-part positive semantic check must explicitly fail.
    echo "[M237 GUEST SEMANTIC FAIL] case=$case_id phase=$phase original=/$elf" >&2
    exit 1
}

rec_fixture() {
    test -s "$state/contacts.rec" && test -s "$state/contacts.csv" || return 1
    /usr/bin/recsel -t Contact -c "$state/contacts.rec" > "$target/source.count"
    $busy grep -Fxq 2 "$target/source.count"
}

case "$case_id:$elf" in
    rec-csv-import:usr/bin/csv2rec)
        if [ "$phase" = first ]; then
            test ! -e "$state/contacts.rec" && test ! -e "$state/contacts.csv" || fail
            printf 'Name,Role\nAda,Engineer\nLin,Researcher\n' > "$state/contacts.csv"
            printf '%%rec: Contact\n\nName: Ada\nRole: Engineer\n\nName: Lin\nRole: Researcher\n' > "$state/contacts.rec"
        fi
        rec_fixture
        /usr/bin/csv2rec -t Contact "$state/contacts.csv" > "$target/imported.rec" 2> "$target/import.err"
        /usr/bin/recsel -t Contact -c "$target/imported.rec" > "$target/import.count"
        $busy grep -Fxq 2 "$target/import.count" &&
            $busy grep -Fq 'Name: Ada' "$target/imported.rec" &&
            $busy grep -Fq 'Name: Lin' "$target/imported.rec" || fail
        ;;
    rec-csv-export:usr/bin/rec2csv)
        rec_fixture
        /usr/bin/rec2csv -t Contact "$state/contacts.rec" > "$target/export.csv" 2> "$target/export.err"
        # Original signed recutils 1.9-r2 CSV quotes EVERY field; a bare
        # Ada,Engineer search is incorrect (host Linux control #42452263322).
        $busy grep -Fxq '"Name","Role"' "$target/export.csv" &&
            $busy grep -Fxq '"Ada","Engineer"' "$target/export.csv" &&
            $busy grep -Fxq '"Lin","Researcher"' "$target/export.csv" || fail
        ;;
    rec-remove-selected:usr/bin/recdel)
        rec_fixture
        $busy cp "$state/contacts.rec" "$target/deleted.rec"
        /usr/bin/recdel -t Contact -e "Name = 'Lin'" "$target/deleted.rec" > "$target/delete.log" 2>&1
        /usr/bin/recsel -t Contact -c "$target/deleted.rec" > "$target/count.txt"
        $busy grep -Fxq 1 "$target/count.txt" &&
            ! $busy grep -Fq 'Name: Lin' "$target/deleted.rec" &&
            $busy grep -Fq 'Name: Ada' "$target/deleted.rec" || fail
        ;;
    rec-structural-check:usr/bin/recfix)
        rec_fixture
        /usr/bin/recfix --check "$state/contacts.rec" > "$target/valid.log" 2>&1
        $busy sed 's/Name: Ada/Name Ada/' "$state/contacts.rec" > "$target/invalid.rec"
        $busy cmp -s "$state/contacts.rec" "$target/invalid.rec" && exit 1
        if /usr/bin/recfix --check "$target/invalid.rec" > "$target/invalid.log" 2>&1; then
            echo 'M237 recfix incorrectly accepted a structurally invalid record' >&2
            exit 1
        fi
        test -s "$target/invalid.log"
        ;;
    rec-template-render:usr/bin/recfmt)
        rec_fixture
        /usr/bin/recsel -t Contact "$state/contacts.rec" |
            /usr/bin/recfmt 'CONTACT={{Name}}|{{Role}}' > "$target/rendered.txt" 2> "$target/render.err"
        $busy grep -Fq 'CONTACT=Ada|Engineer' "$target/rendered.txt" &&
            $busy grep -Fq 'CONTACT=Lin|Researcher' "$target/rendered.txt" || fail
        ;;
    rec-record-count:usr/bin/recinf)
        rec_fixture
        /usr/bin/recinf "$state/contacts.rec" > "$target/info.txt" 2> "$target/info.err"
        $busy grep -Eq '^2[[:space:]]+Contact$' "$target/info.txt"
        ;;
    rec-insert-record:usr/bin/recins)
        rec_fixture
        $busy cp "$state/contacts.rec" "$target/inserted.rec"
        /usr/bin/recins -t Contact -f Name -v Grace -f Role -v Scientist \
            "$target/inserted.rec" > "$target/insert.log" 2>&1
        /usr/bin/recsel -t Contact -c "$target/inserted.rec" > "$target/count.txt"
        $busy grep -Fxq 3 "$target/count.txt" &&
            $busy grep -Fq 'Name: Grace' "$target/inserted.rec" &&
            $busy grep -Fq 'Role: Scientist' "$target/inserted.rec" || fail
        ;;
    rec-select-expression:usr/bin/recsel)
        rec_fixture
        /usr/bin/recsel -t Contact -e "Name = 'Lin'" "$state/contacts.rec" > "$target/selected.rec" 2> "$target/select.err"
        $busy grep -Fq 'Name: Lin' "$target/selected.rec" &&
            $busy grep -Fq 'Role: Researcher' "$target/selected.rec" &&
            ! $busy grep -Fq 'Name: Ada' "$target/selected.rec" || fail
        ;;
    rec-set-field:usr/bin/recset)
        rec_fixture
        $busy cp "$state/contacts.rec" "$target/updated.rec"
        /usr/bin/recset -t Contact -e "Name = 'Ada'" -f Role -s ResearchLead \
            "$target/updated.rec" > "$target/set.log" 2>&1
        /usr/bin/recsel -t Contact -e "Name = 'Ada'" "$target/updated.rec" > "$target/selected.rec"
        $busy grep -Fq 'Role: ResearchLead' "$target/selected.rec" &&
            ! $busy grep -Fq 'Role: Engineer' "$target/selected.rec" &&
            $busy grep -Fq 'Role: Researcher' "$target/updated.rec" || fail
        ;;
    yajl-parse-reformat:usr/bin/json_reformat)
        if [ "$phase" = first ]; then
            test ! -e "$state/structure.json"
            printf '{"name":"NaSeed","items":[3,5],"ready":true}\n' > "$state/structure.json"
        fi
        test -s "$state/structure.json"
        /usr/bin/json_reformat < "$state/structure.json" > "$target/reformatted.json" 2> "$target/reformat.err"
        /usr/bin/json_verify < "$target/reformatted.json" > "$target/verify.log" 2>&1
        $busy grep -Fq '"NaSeed"' "$target/reformatted.json" &&
            $busy grep -Fq 'true' "$target/reformatted.json" &&
            $busy grep -Fq '5' "$target/reformatted.json" || fail
        ;;
    yajl-valid-invalid:usr/bin/json_verify)
        test -s "$state/structure.json"
        /usr/bin/json_verify < "$state/structure.json" > "$target/valid.log" 2>&1
        printf '{"broken":}\n' > "$target/invalid.json"
        if /usr/bin/json_verify < "$target/invalid.json" > "$target/invalid.log" 2>&1; then
            echo 'M237 YAJL original parser accepted malformed JSON' >&2; exit 1
        fi
        test -s "$target/invalid.log"
        ;;
    minisign-persistent-signature:usr/bin/minisign)
        if [ "$phase" = first ]; then
            test ! -e "$state/minisign.secret" && test ! -e "$state/minisign.pub" || fail
            printf 'M237_ORIGINAL_MINISIGN_DURABLE_SIGNATURE\n' > "$state/signed.message"
            /usr/bin/minisign -G -W -p "$state/minisign.pub" -s "$state/minisign.secret" > "$target/keygen.log" 2>&1
            /usr/bin/minisign -S -m "$state/signed.message" -s "$state/minisign.secret" \
                -x "$state/message.minisig" > "$target/sign.log" 2>&1
        fi
        test -s "$state/minisign.secret" && test -s "$state/minisign.pub" && test -s "$state/message.minisig" || fail
        /usr/bin/minisign -V -m "$state/signed.message" -p "$state/minisign.pub" \
            -x "$state/message.minisig" > "$target/verified.log" 2>&1
        $busy sed s/DURABLE/TAMPERED/ "$state/signed.message" > "$target/tampered.message"
        $busy cmp -s "$state/signed.message" "$target/tampered.message" && exit 1
        if /usr/bin/minisign -V -m "$target/tampered.message" -p "$state/minisign.pub" \
                -x "$state/message.minisig" > "$target/forged.log" 2>&1; then
            echo 'M237 minisign incorrectly accepted modified signed bytes' >&2; exit 1
        fi
        ;;
    geos-binary-distance-intersects:usr/bin/geosop)
        # The original Alpine 3.23 GEOS 3.14.1 geosop has an upstream
        # executeUnary null-B dereference, independently reproduced on Linux.
        # Its original signed binary ops work: test both numeric geometry and
        # positive/negative topology against persisted real ext4 WKT files.
        if [ "$phase" = first ]; then
            test ! -e "$state/geos-origin.wkt" && test ! -e "$state/geos-diagonal.wkt" || fail
            printf 'POINT (0 0)\n' > "$state/geos-origin.wkt"
            printf 'POINT (3 4)\n' > "$state/geos-diagonal.wkt"
        fi
        test -s "$state/geos-origin.wkt" && test -s "$state/geos-diagonal.wkt" || fail
        /usr/bin/geosop -a "$state/geos-origin.wkt" -b "$state/geos-diagonal.wkt" \
            --format=txt distance > "$target/distance.txt" 2> "$target/distance.err"
        /usr/bin/geosop -a "$state/geos-origin.wkt" -b "$state/geos-origin.wkt" \
            --format=txt intersects > "$target/intersects-true.txt" 2> "$target/intersects-true.err"
        /usr/bin/geosop -a "$state/geos-origin.wkt" -b "$state/geos-diagonal.wkt" \
            --format=txt intersects > "$target/intersects-false.txt" 2> "$target/intersects-false.err"
        $busy grep -Eq '^5(\.0+)?$' "$target/distance.txt" &&
            $busy grep -Fxq true "$target/intersects-true.txt" &&
            $busy grep -Fxq false "$target/intersects-false.txt" || {
                echo 'M237 GEOS original binary geometry returned wrong distance/topology' >&2
                exit 1
            }
        ;;
    proj-crs-info:usr/bin/projinfo)
        /usr/bin/projinfo EPSG:4326 > "$target/crs.txt" 2> "$target/proj.err"
        $busy grep -Ei '(WGS 84|longlat|geographic)' "$target/crs.txt" >/dev/null &&
            $busy grep -Eq '(EPSG|4326)' "$target/crs.txt" || fail
        ;;
    proj-mercator-coordinates:usr/bin/cs2cs)
        if [ "$phase" = first ]; then
            printf '10 20\n' > "$state/longitude-latitude.txt"
        fi
        test -s "$state/longitude-latitude.txt"
        PROJ_NETWORK=OFF /usr/bin/cs2cs +proj=longlat +datum=WGS84 +to +proj=merc +datum=WGS84 \
            < "$state/longitude-latitude.txt" > "$target/mercator.xy" 2> "$target/cs2cs.err"
        $busy awk 'NF >= 2 && $1+0 > 1113194 && $1+0 < 1113196 &&
                   $2+0 > 2258423 && $2+0 < 2258425 {good=1} END {exit !good}' "$target/mercator.xy"
        ;;
    *) echo "M237 unknown actual original ELF: $case_id:$elf" >&2; exit 2 ;;
esac
echo "M237_REAL_FUNCTION_OK case=$case_id phase=$phase original=/$elf"

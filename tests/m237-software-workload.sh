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

rec_fixture() {
    test -s "$state/contacts.rec" && test -s "$state/contacts.csv" || return 1
    /usr/bin/recsel -t Contact -c "$state/contacts.rec" > "$target/source.count"
    $busy grep -Fxq 2 "$target/source.count"
}

case "$case_id:$elf" in
    rec-csv-import:usr/bin/csv2rec)
        if [ "$phase" = first ]; then
            test ! -e "$state/contacts.rec" && test ! -e "$state/contacts.csv"
            printf 'Name,Role\nAda,Engineer\nLin,Researcher\n' > "$state/contacts.csv"
            printf '%%rec: Contact\n\nName: Ada\nRole: Engineer\n\nName: Lin\nRole: Researcher\n' > "$state/contacts.rec"
        fi
        rec_fixture
        /usr/bin/csv2rec -t Contact "$state/contacts.csv" > "$target/imported.rec" 2> "$target/import.err"
        /usr/bin/recsel -t Contact -c "$target/imported.rec" > "$target/import.count"
        $busy grep -Fxq 2 "$target/import.count" &&
            $busy grep -Fq 'Name: Ada' "$target/imported.rec" &&
            $busy grep -Fq 'Name: Lin' "$target/imported.rec"
        ;;
    rec-csv-export:usr/bin/rec2csv)
        rec_fixture
        /usr/bin/rec2csv -t Contact "$state/contacts.rec" > "$target/export.csv" 2> "$target/export.err"
        $busy grep -Fq 'Ada,Engineer' "$target/export.csv" &&
            $busy grep -Fq 'Lin,Researcher' "$target/export.csv"
        ;;
    rec-remove-selected:usr/bin/recdel)
        rec_fixture
        $busy cp "$state/contacts.rec" "$target/deleted.rec"
        /usr/bin/recdel -t Contact -e "Name = 'Lin'" "$target/deleted.rec" > "$target/delete.log" 2>&1
        /usr/bin/recsel -t Contact -c "$target/deleted.rec" > "$target/count.txt"
        $busy grep -Fxq 1 "$target/count.txt" &&
            ! $busy grep -Fq 'Name: Lin' "$target/deleted.rec" &&
            $busy grep -Fq 'Name: Ada' "$target/deleted.rec"
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
            $busy grep -Fq 'CONTACT=Lin|Researcher' "$target/rendered.txt"
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
            $busy grep -Fq 'Role: Scientist' "$target/inserted.rec"
        ;;
    rec-select-expression:usr/bin/recsel)
        rec_fixture
        /usr/bin/recsel -t Contact -e "Name = 'Lin'" "$state/contacts.rec" > "$target/selected.rec" 2> "$target/select.err"
        $busy grep -Fq 'Name: Lin' "$target/selected.rec" &&
            $busy grep -Fq 'Role: Researcher' "$target/selected.rec" &&
            ! $busy grep -Fq 'Name: Ada' "$target/selected.rec"
        ;;
    rec-set-field:usr/bin/recset)
        rec_fixture
        $busy cp "$state/contacts.rec" "$target/updated.rec"
        /usr/bin/recset -t Contact -e "Name = 'Ada'" -f Role -s ResearchLead \
            "$target/updated.rec" > "$target/set.log" 2>&1
        /usr/bin/recsel -t Contact -e "Name = 'Ada'" "$target/updated.rec" > "$target/selected.rec"
        $busy grep -Fq 'Role: ResearchLead' "$target/selected.rec" &&
            ! $busy grep -Fq 'Role: Engineer' "$target/selected.rec" &&
            $busy grep -Fq 'Role: Researcher' "$target/updated.rec"
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
            $busy grep -Fq '5' "$target/reformatted.json"
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
            test ! -e "$state/minisign.secret" && test ! -e "$state/minisign.pub"
            printf 'M237_ORIGINAL_MINISIGN_DURABLE_SIGNATURE\n' > "$state/signed.message"
            /usr/bin/minisign -G -W -p "$state/minisign.pub" -s "$state/minisign.secret" > "$target/keygen.log" 2>&1
            /usr/bin/minisign -S -m "$state/signed.message" -s "$state/minisign.secret" \
                -x "$state/message.minisig" > "$target/sign.log" 2>&1
        fi
        test -s "$state/minisign.secret" && test -s "$state/minisign.pub" && test -s "$state/message.minisig"
        /usr/bin/minisign -V -m "$state/signed.message" -p "$state/minisign.pub" \
            -x "$state/message.minisig" > "$target/verified.log" 2>&1
        $busy sed s/DURABLE/TAMPERED/ "$state/signed.message" > "$target/tampered.message"
        $busy cmp -s "$state/signed.message" "$target/tampered.message" && exit 1
        if /usr/bin/minisign -V -m "$target/tampered.message" -p "$state/minisign.pub" \
                -x "$state/message.minisig" > "$target/forged.log" 2>&1; then
            echo 'M237 minisign incorrectly accepted modified signed bytes' >&2; exit 1
        fi
        ;;
    geos-polygon-area:usr/bin/geosop)
        if [ "$phase" = first ]; then
            printf 'POLYGON ((0 0, 4 0, 4 3, 0 3, 0 0))\n' > "$state/rectangle.wkt"
        fi
        test -s "$state/rectangle.wkt"
        geos_rc=0
        /usr/bin/geosop -a "$state/rectangle.wkt" --format=txt area > "$target/area.txt" 2> "$target/geos.err" || geos_rc=$?
        area_ok=0
        if [ "$geos_rc" -eq 0 ] &&
                $busy awk '{for(i=1;i<=NF;i++) if($i+0 >= 11.999 && $i+0 <= 12.001) found=1} END{exit !found}' "$target/area.txt"; then
            area_ok=1
        fi
        if [ "$area_ok" -ne 1 ]; then
            # The authoritative test remains the genuine 3x4 GEOS area.
            # Keep strict 3x4 area acceptance; isolate geometry parser,
            # stream input, ext4 file input and GEOS operation independently.
            # Each probe uses the same original signed Alpine geosop ELF.
            literal='POLYGON ((0 0, 4 0, 4 3, 0 3, 0 0))'
            lit_rc=0
            $busy timeout 8 /usr/bin/geosop -a "$literal" --format=txt area > "$target/literal-area.txt" 2> "$target/literal-area.err" || lit_rc=$?
            stdin_rc=0
            $busy timeout 8 /usr/bin/geosop -a stdin --format=txt area < "$state/rectangle.wkt" > "$target/stdin-area.txt" 2> "$target/stdin-area.err" || stdin_rc=$?
            file_copy_rc=0
            $busy timeout 8 /usr/bin/geosop -a "$state/rectangle.wkt" --format=txt copy > "$target/file-copy.txt" 2> "$target/file-copy.err" || file_copy_rc=$?
            lit_copy_rc=0
            $busy timeout 8 /usr/bin/geosop -a "$literal" --format=txt copy > "$target/literal-copy.txt" 2> "$target/literal-copy.err" || lit_copy_rc=$?
            geos_err=$($busy head -c 120 "$target/geos.err" | $busy tr '\t\r\n' '   ' | $busy tr -cd '\040-\176')
            lit_err=$($busy head -c 80 "$target/literal-area.err" | $busy tr '\t\r\n' '   ' | $busy tr -cd '\040-\176')
            lit_out=$($busy head -c 36 "$target/literal-area.txt" | $busy tr '\t\r\n' '   ' | $busy tr -cd '\040-\176')
            stdin_out=$($busy head -c 36 "$target/stdin-area.txt" | $busy tr '\t\r\n' '   ' | $busy tr -cd '\040-\176')
            printf 'M237_GEOS_DIAG phase=%s area_rc=%s stderr=%s\n' "$phase" "$geos_rc" "$geos_err"
            printf 'M237_GEOS_DIFF phase=%s literal_area=%s stdin_area=%s file_copy=%s literal_copy=%s\n' "$phase" "$lit_rc" "$stdin_rc" "$file_copy_rc" "$lit_copy_rc"
            printf 'M237_GEOS_DIFF_OUTPUT phase=%s literal=%s stdin=%s literal_err=%s\n' "$phase" "$lit_out" "$stdin_out" "$lit_err"
            exit 1
        fi
        ;;
    proj-crs-info:usr/bin/projinfo)
        /usr/bin/projinfo EPSG:4326 > "$target/crs.txt" 2> "$target/proj.err"
        $busy grep -Ei '(WGS 84|longlat|geographic)' "$target/crs.txt" >/dev/null &&
            $busy grep -Eq '(EPSG|4326)' "$target/crs.txt"
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

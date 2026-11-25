#!/bin/sh
# Original signed APK functional batches; one owner for standard and extended profiles. Each profile keeps
# its own real guest script, exact markers, staging and serial evidence.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: probe-alpine-signed-batch.sh PORT PROFILE' >&2; exit 2; }
port=$1
profile=$2
ram=1536M
disk=768
timeout=
exec_proof=0
case "$profile" in
    m109) count=15; fail_tail=360; marker_tail=360 ;;
    m110) count=20; fail_tail=450; marker_tail=450 ;;
    m11[1-9]|m12[0-9]|m130|m131) count=15; fail_tail=450; marker_tail=450 ;;
    m13[2-5]) count=15; fail_tail=500; marker_tail=500 ;;
    m136) count=20; fail_tail=500; marker_tail=500 ;;
    m137) count=20; fail_tail=10000; marker_tail=2000 ;;
    m138|m139) count=20; fail_tail=4000; marker_tail=2000 ;;
    m140) count=39; fail_tail=4000; marker_tail=2000 ;;
    m146) count=24; fail_tail=2500; marker_tail=0; ram=2048M; disk=1024; timeout=600; exec_proof=0 ;;
    m148) count=35; fail_tail=2500; marker_tail=0; ram=2048M; disk=1024; timeout=660; exec_proof=1 ;;
    m150) count=26; fail_tail=2500; marker_tail=0; ram=2048M; disk=1024; timeout=650; exec_proof=0 ;;
    m151) count=35; fail_tail=3000; marker_tail=0; ram=2048M; disk=1536; timeout=900; exec_proof=1 ;;
    m153) count=36; fail_tail=2500; marker_tail=0; ram=2048M; disk=1024; timeout=700; exec_proof=0 ;;
    m154) count=30; fail_tail=900; marker_tail=0; ram=2048M; disk=1024; timeout=700; exec_proof=1 ;;
    m155) count=30; fail_tail=2500; marker_tail=0; ram=2048M; disk=1024; timeout=800; exec_proof=1 ;;
    m157) count=30; fail_tail=900; marker_tail=0; ram=2048M; disk=1024; timeout=700; exec_proof=1 ;;
    m158) count=30; fail_tail=900; marker_tail=0; ram=2048M; disk=1024; timeout=700; exec_proof=1 ;;
    m159) count=30; fail_tail=850; marker_tail=0; ram=2048M; disk=1024; timeout=900; exec_proof=1 ;;
    m163) count=33; fail_tail=700; marker_tail=0; ram=2048M; disk=1024; timeout=900; exec_proof=1 ;;
    m165) count=30; fail_tail=700; marker_tail=0; ram=2048M; disk=1024; timeout=900; exec_proof=1 ;;
    *) exit 2 ;;
esac
. scripts/native-qemu-target.sh
sh scripts/stage-alpine-signed-batch.sh "$port" "$profile"
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
work="build/external/$profile-batch/$port"
export BUSYBOX_PARITY_BINARY="$work/rootfs/bin/busybox"
export NAS_ACCEPTANCE_CPIO="$work/rootfs.cpio"
export BUSYBOX_PARITY_EXPECTED_COUNT=1
export NAS_ACCEPTANCE_QEMU_RAM="$ram"
export NAS_ACCEPTANCE_DISK_MB="$disk"
if [ -n "$timeout" ]; then export NAS_APP_BATCH_QEMU_TIMEOUT="$timeout"; fi
if [ "$profile" = m110 ]; then export NAS_SYSCALL_TRACE=1; fi
BUSYBOX_PARITY_MANIFEST=$(sh scripts/acceptance/batch-manifest.sh "$profile")
export BUSYBOX_PARITY_MANIFEST
log="$logs/$profile-batch.clean.log"
if ! sh "$runner"; then
    echo "[$label BATCH FAIL] $port original signed Alpine runtime" >&2
    if test -s "$log"; then
        grep -aE "^(${label}_[A-Z0-9_]+_(PASS|FAIL)|\\[$label )" "$log" >&2 || true
        tail -n "$fail_tail" "$log" >&2
    fi
    exit 1
fi
test -s "$log" || { echo "[$label BATCH FAIL] $port missing guest serial" >&2; exit 1; }
# Marker identity has one canonical owner: signed original ELF names when the
# historically separate marker list was exactly derived from them. Retain all
# explicitly authored exception manifests, including extra semantic markers.
manifest="scripts/$profile-expected-markers.txt"
case "$profile" in
    m112|m113|m114|m115|m116|m119|m120|m121|m122|m124|m125|m126|m127|m128|m129|m130|m131|m132|m133|m135|m136|m139|m146|m148|m150|m153|m154|m157|m158|m159|m163|m165)
        mkdir -p "$work"
        manifest="$work/derived-expected-markers.txt"
        # Single host-side marker authority; independent signed ELF identities.
        python3 scripts/acceptance/profile_markers.py "$profile" > "$manifest" || exit 2 ;;
esac
sh scripts/acceptance/check-batch-markers.sh "$label" "$port" "$manifest" "$log" "$count" "$marker_tail"
case "$profile" in
    m151)
        roundtrips=$(grep -Fc '[M151 ROUNDTRIP]' "$log" || :)
        [ "$roundtrips" -eq 23 ] || {
            echo "[$label NATIVE QEMU FAIL] $port only $roundtrips/23 real roundtrips" >&2
            exit 1
        } ;;
    m155)
        starts=$(grep -Fc '[M155 ELF STARTUP ONLY]' "$log" || :)
        [ "$starts" -eq 8 ] || {
            echo "[$label NATIVE QEMU FAIL] $port only $starts/8 startup-only ELF probes" >&2
            exit 1
        } ;;
esac
if [ "$exec_proof" -eq 1 ]; then
    execs=0
    while IFS= read -r elf; do
        case "$elf" in ''|'#'*) continue ;; esac
        # The kernel emits exactly one line ending with pending_exec.filename.
        # A prefix (svn vs svnadmin) is not evidence of the requested ELF.
        grep -Fxq "[EXEC] replaced process with CPIO ELF: /$elf" "$log" || {
            echo "[$label NATIVE QEMU FAIL] $port missing genuine exec /$elf" >&2
            exit 1
        }
        execs=$((execs+1))
    done < "scripts/$profile-original-elfs.txt"
    if [ "$profile" = m155 ]; then [ "$execs" -eq 33 ] || exit 1; fi
fi
case "$profile" in
    m163) echo "[M163 THREE ISA PASS] $port 33 new signed Alpine ELFs: pixels, transforms, PICT/ICO, terminal" ;;
    m165) echo "[M165 NATIVE PASS] $port 30 new signed Alpine image and actual codec operations" ;;
    m146|m148|m150|m151|m153|m154|m155|m157|m158|m159)
        echo "[$label NATIVE QEMU PASS] $port $count original-program guest checks in one native QEMU boot" ;;
    *) echo "[$label BATCH PASS] $port $count original-program guest checks in one native QEMU boot" ;;
esac

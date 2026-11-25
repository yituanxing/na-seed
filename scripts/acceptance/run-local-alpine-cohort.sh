#!/bin/sh
# Host-only by default. Native QEMU/APK download is an explicit opt-in;
# never pushes, edits remote branches or triggers GitHub Actions.
set -eu
[ "$#" -ge 1 ] || exit 2
profile=$1
shift
case "$profile" in
    m235) count=12; label=M235; count_word=twelve ;;
    m236) count=7; label=M236; count_word=seven ;;
    m237) count=15; label=M237; count_word=fifteen ;;
    m238) count=15; label=M238; count_word=fifteen ;;
    *) exit 2 ;;
esac
mode=${1:---host-only}
case "$mode" in --host-only|--native) ;; *) exit 2 ;; esac
export PYTHONDONTWRITEBYTECODE=1
python3 scripts/check-m141-candidates.py --profile ${profile}
python3 scripts/test-${profile}-alpine-host.py
[ "$profile" = m235 ] || python3 scripts/test-m235-alpine-host.py
python3 scripts/test-m223-http-host.py
python3 scripts/test-m205-alpine-host.py
python3 scripts/test-m206-alpine-host.py
if [ "$profile" != m235 ]; then
    python3 scripts/check-three-isa-core.py
    python3 scripts/check-architecture-boundaries.py
fi
for path in scripts/probe-m197-alpine-software-cohort.sh \
            scripts/inventory-m141-signed-apks.sh \
            scripts/probe-alpine-profile.sh \
            tests/m205-software-workload.sh \
            tests/${profile}-software-workload.sh; do
    sh -n "$path"
done
python3 -c 'from pathlib import Path
import sys
for path in (Path("scripts") / (sys.argv[1] + "-alpine-disk-check.py"),
             Path("scripts/alpine-first-uart.py"), Path("scripts/alpine-compat-report.py")):
    compile(path.read_bytes(), str(path), "exec")' "$profile"
if [ "$profile" = m235 ]; then
    python3 -c 'from pathlib import Path; p = Path("scripts/m235-http-proof.py"); compile(p.read_bytes(), str(p), "exec")'
fi
if [ "$mode" = --host-only ]; then
    echo "[${label} HOST ONLY PASS] strict manifest, original APK SHA and ledger negative cases; no native-QEMU acceptance claimed"
    exit 0
fi
test "$#" -eq 2 || { echo "usage: sh scripts/acceptance/run-local-alpine-cohort.sh ${profile} --native amd64|arm64|riscv64" >&2; exit 2; }
port=$2
case "$port" in amd64|arm64|riscv64) ;; *) exit 2 ;; esac
# Match M235's successful Ubuntu 24.04 signed-APK and native-QEMU toolchain
# preflight. Host-only tests above never install packages or start QEMU.
if [ "$profile" = m235 ]; then
    sh scripts/ci-install-signed-alpine-qemu-deps.sh "$port" --cohort-tools
else
    sh scripts/ci-install-signed-alpine-qemu-deps.sh "$port" --cohort-tools \
        "[${label} NATIVE PREREQUISITE FAIL] Docker required for signed APK inventory"
fi
sh scripts/inventory-m141-signed-apks.sh "$port" ${profile}
tsv="build/external/${profile}-candidate-inventory/$port/candidates.tsv"
awk -F '\t' -v expected="$count" 'NR > 1 { n++; if ($3 != "NATIVE_ELF") bad++ }
    END { if (n != expected || bad) {
        if (expected == 12) print "[M235 FAIL] wrong signed native ELF census";
        exit 1
    } }' "$tsv"
unique=$(awk -F '\t' 'NR > 1 { print $4 }' "$tsv" | sort -u | wc -l)
test "$unique" -eq "$count" || {
    if [ "$profile" = m235 ]; then
        echo '[M235 FAIL] duplicate signed ELF SHA' >&2
    else
        echo "[${label} FAIL] signed APK original native ELFs share identical SHA; refuse inflated ${count_word}" >&2
    fi
    exit 1
}
if [ "$profile" = m235 ]; then
    echo "[M235 SIGNED APK PASS] $port twelve newly signed target-native unique ELF hashes"
fi
sh scripts/probe-alpine-profile.sh "$profile" "$port"

#!/bin/sh
# M50: same twelve strict native QEMU probes, amortize per-ISA dependency
# install and kernel build. No synthetic or stubbed acceptance; any error
# aborts the suite. Other suites continue independently in separate CI jobs.
set -eu
[ "$#" -eq 2 ] || { echo 'usage: probe-alpine-batch.sh foundation|pthread|lifecycle|parallel amd64|riscv64|arm64' >&2; exit 2; }
suite=$1
port=$2
case "$suite" in
  foundation) expected=4 ;;
  pthread) expected=3 ;;
  lifecycle) expected=3 ;;
  parallel) expected=2 ;;
  *) echo "[M50 BAD SUITE] $suite" >&2; exit 2 ;;
esac
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/native-alpine-matrix.tsv
test -s "$manifest"
count=0
tab=$(printf '\t')
while IFS="$tab" read -r name group probe; do
    case "$name" in ''|\#*) continue ;; esac
    [ "$group" = "$suite" ] || continue
    case "$probe" in scripts/probe-m[34][0-9]-*.sh) ;; *)
        echo "[M50 FAIL] noncanonical acceptance: $probe" >&2
        exit 1 ;;
    esac
    test -s "$probe"
    count=$((count + 1))
    echo "[M50 START] $port / $suite / $name / $probe"
    if ! sh "$probe" "$port"; then
        echo "[M50 FAIL] $port / $suite / $name; kernel/ELF evidence retained" >&2
        exit 1
    fi
    echo "[M50 CHECK PASS] $port / $suite / $name"
done < "$manifest"
[ "$count" -eq "$expected" ] || {
    echo "[M50 FAIL] suite $suite required $expected cases, got $count" >&2
    exit 1
}
echo "[M50 SUITE PASS] $port / $suite / $count real native QEMU workloads"

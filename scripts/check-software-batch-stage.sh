#!/bin/sh
# Native original ELF admission for M174/M187/M188, retaining cumulative SHA gates.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 profile=$2
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
    *) exit 2 ;;
esac
work="build/external/m174-software-batch/$port"
root="$work/rootfs"
installed="$root/lib/apk/db/installed"
case "$profile" in
    m174) manifest=scripts/m174-software-original-elfs.txt; expected=20; cumulative=20
        required='fbgrab imagemagick evtest libevdev-tools dialog links w3m lynx mc dbus inotify-tools sysstat tig'
        fixtures='tests/m167-desktop-fixture.ppm tests/m168-home.html tests/m174-software-run.sh'
        output="$work/m174-software-original-signed-elfs.sha256" ;;
    m187) manifest=scripts/m187-original-elfs.txt; expected=26; cumulative=46
        required='util-linux-misc ncurses-terminfo-base'
        fixtures='tests/m187-original-elfs.txt tests/m187-util-linux-run.sh tests/m187-software-functions.sh'
        output="$work/m187-software-new-signed-elfs.sha256" ;;
    m188) manifest=scripts/m188-original-elfs.txt; expected=2; cumulative=48
        required='vim nano mg rdfind'
        fixtures='tests/m188-editor-check.sh tests/m188-software-entry.sh tests/m188-original-elfs.txt tests/m189-editor-check.sh tests/m190-durable-check.sh'
        output="$work/m188-software-new-signed-elfs.sha256" ;;
    *) exit 2 ;;
esac
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
fail() { echo "[$label SOFTWARE STAGE FAIL] $port $*" >&2; exit 1; }
test -s "$work/rootfs.cpio" && test -s "$installed" || fail 'missing CPIO or installed package database'
for package in $required; do
    grep -Fxq "P:$package" "$installed" || fail "missing signed APK: $package"
done
if [ "$profile" = m187 ]; then
    test -s "$root/etc/terminfo/v/vt100" || test -s "$root/usr/share/terminfo/v/vt100" || fail 'signed VT100 terminfo data missing'
fi
for fixture in $fixtures; do
    test -s "$root/$fixture" || fail "fixture missing: $fixture"
done
cmp -s "$manifest" "$root/tests/${manifest##*/}" || fail 'guest ELF list differs from source'
canonical=$(cd "$root" && pwd -P)
# Same containment, alias, executable, magic and target-machine proof for every entry.
check_elf() {
    resolved=$(readlink -f "$root/$1") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) fail "ELF escaped root: $1" ;; esac
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox) fail "BusyBox alias: $1" ;; esac
    test -f "$resolved" && test -x "$resolved" || fail "missing executable: $1"
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || fail "non-ELF: $1"
    readelf -h "$resolved" | grep -Fq "$machine" || fail "wrong ISA: $1"
}
if [ "$profile" = m188 ]; then
    for fixture in tests/m188-editor-check.sh tests/m188-software-entry.sh tests/m189-editor-check.sh tests/m190-durable-check.sh; do
        cmp -s "$fixture" "$root/$fixture" || fail "fixture differs: $fixture"
    done
    for elf in usr/bin/vim usr/bin/nano; do check_elf "$elf"; done
fi
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    check_elf "$elf"
    sha256sum "$resolved"
    count=$((count+1))
done < "$manifest" > "$output"
[ "$count" -eq "$expected" ] || fail "expected $expected ELF paths"
set -- "$output"
if [ "$profile" != m174 ]; then
    prior="$work/m174-software-original-signed-elfs.sha256"
    test -s "$prior" || fail 'missing M174 hashes'
    set -- "$prior" "$@"
fi
if [ "$profile" = m188 ]; then
    prior="$work/m187-software-new-signed-elfs.sha256"
    test -s "$prior" || fail 'missing M187 hashes'
    set -- "$prior" "$@"
fi
unique=$(awk '{print $1}' "$@" | LC_ALL=C sort -u | wc -l)
[ "$unique" -eq "$cumulative" ] || fail "only $unique/$cumulative distinct original ELF SHA256"
case "$profile" in
    m174) echo "[M174 SOFTWARE SIGNED ELF PASS] $port 20 original Alpine ELF SHA256; QEMU guest semantics pending" ;;
    m187) echo "[M187 SOFTWARE SIGNED ELF PASS] $port +26 genuinely new SHA256 ELFs, 46 total distinct M174+M187, same CPIO; QEMU guest semantics pending" ;;
    m188) echo "[M188 SOFTWARE SIGNED ELF PASS] $port M79 Vim/M80 Nano reused; +2 native ELF mg/rdfind; 48 distinct M174+M187+M188; live Vim QEMU verification pending" ;;
esac

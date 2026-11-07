#!/bin/sh
# Signed Alpine APK installed into the original M174 CPIO root, not a sidecar.
# Fail closed unless 26 NEW native ELF payloads have 26 distinct actual SHA256
# and none of those hashes aliases one of M174's original 20 signed binaries.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
    *) exit 2 ;;
esac
work="build/external/m174-software-batch/$port"
root="$work/rootfs"
manifest=scripts/m187-original-elfs.txt
test -s "$work/rootfs.cpio" && test -s "$root/lib/apk/db/installed"
grep -Fxq 'P:util-linux-misc' "$root/lib/apk/db/installed" || {
    echo "[M187 SOFTWARE STAGE FAIL] $port unsigned/missing util-linux-misc APK" >&2
    exit 1
}
grep -Fxq 'P:ncurses-terminfo-base' "$root/lib/apk/db/installed" || {
    echo "[M187 SOFTWARE STAGE FAIL] $port missing Alpine VT100 terminfo APK" >&2
    exit 1
}
if ! test -s "$root/etc/terminfo/v/vt100" &&
   ! test -s "$root/usr/share/terminfo/v/vt100"; then
    echo "[M187 SOFTWARE STAGE FAIL] $port signed VT100 terminfo data missing" >&2
    exit 1
fi
for fixture in tests/m187-original-elfs.txt tests/m187-util-linux-run.sh \
               tests/m187-software-functions.sh; do
    test -s "$root/$fixture" || {
        echo "[M187 SOFTWARE STAGE FAIL] $port missing same-CPIO fixture $fixture" >&2
        exit 1
    }
done
cmp -s "$manifest" "$root/tests/m187-original-elfs.txt" || {
    echo "[M187 SOFTWARE STAGE FAIL] $port M187 guest ELF list altered" >&2
    exit 1
}
canonical=$(cd "$root" && pwd -P)
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in
        "$canonical"/*) ;;
        *) echo "[M187 SOFTWARE STAGE FAIL] $port ELF escaped root $elf" >&2; exit 1 ;;
    esac
    case "$resolved" in
        "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
            echo "[M187 SOFTWARE STAGE FAIL] $port BusyBox alias $elf" >&2
            exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || {
        echo "[M187 SOFTWARE STAGE FAIL] $port missing/unsigned executable $elf" >&2
        exit 1
    }
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || {
        echo "[M187 SOFTWARE STAGE FAIL] $port non-ELF $elf" >&2
        exit 1
    }
    readelf -h "$resolved" | grep -Fq "$machine" || {
        echo "[M187 SOFTWARE STAGE FAIL] $port ISA mismatch $elf" >&2
        exit 1
    }
    sha256sum "$resolved"
    count=$((count+1))
done < "$manifest" > "$work/m187-software-new-signed-elfs.sha256"
[ "$count" -eq 26 ] || exit 1
test -s "$work/m174-software-original-signed-elfs.sha256" || exit 1
unique=$(awk '{print $1}' "$work/m174-software-original-signed-elfs.sha256" \
    "$work/m187-software-new-signed-elfs.sha256" | LC_ALL=C sort -u | wc -l)
[ "$unique" -eq 46 ] || {
    echo "[M187 SOFTWARE STAGE FAIL] $port only $unique/46 independently hashed signed Alpine ELFs" >&2
    exit 1
}
echo "[M187 SOFTWARE SIGNED ELF PASS] $port +26 genuinely new SHA256 ELFs, 46 total distinct M174+M187, same CPIO; QEMU guest semantics pending"

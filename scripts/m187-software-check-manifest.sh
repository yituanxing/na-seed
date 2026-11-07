#!/bin/sh
# Independent M187 delta gate. Never re-count or rewrite M174's 20/13 gate.
set -eu
manifest=scripts/m187-original-elfs.txt
packages=rootfs/alpine/packages-m174-software-batch.txt
copies=scripts/m174-software-rootfs-copies.tsv
test -s "$manifest" && test -s "$packages" && test -s "$copies"
grep -Fxq util-linux-misc "$packages" || {
    echo '[M187 SOFTWARE MANIFEST FAIL] signed util-linux-misc package absent' >&2
    exit 1
}
grep -Fxq ncurses-terminfo-base "$packages" || {
    echo '[M187 SOFTWARE MANIFEST FAIL] original VT100 terminfo APK absent' >&2
    exit 1
}
seen=' '
count=0
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$elf" in bin/*|sbin/*|usr/bin/*|usr/sbin/*) ;;
        *) echo "[M187 SOFTWARE MANIFEST FAIL] invalid ELF path $elf" >&2; exit 1 ;;
    esac
    case "$seen" in *" $elf "*)
        echo "[M187 SOFTWARE MANIFEST FAIL] duplicate candidate $elf" >&2; exit 1 ;;
    esac
    seen="$seen$elf "
    for previous in scripts/m[0-9]*-original-elfs.txt \
                    scripts/m[0-9]*-software-original-elfs.txt; do
        [ -f "$previous" ] || continue
        [ "$previous" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$previous"; then
            echo "[M187 SOFTWARE MANIFEST FAIL] historical duplicate $elf in $previous" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 26 ] || {
    echo "[M187 SOFTWARE MANIFEST FAIL] new candidate paths $count != 26" >&2
    exit 1
}
tab=$(printf '\t')
for source in tests/m187-util-linux-run.sh tests/m187-software-functions.sh; do
    destination="$source"
    grep -Fxq "$source$tab$destination" "$copies" || {
        echo "[M187 SOFTWARE MANIFEST FAIL] same-CPIO copy missing: $source" >&2
        exit 1
    }
done
grep -Fq 'scripts/m187-original-elfs.txt' scripts/host-configure-m174-software-batch.sh || {
    echo '[M187 SOFTWARE MANIFEST FAIL] host hook does not stage ELF list in same CPIO' >&2
    exit 1
}
sh -n tests/m187-util-linux-run.sh
sh -n tests/m187-software-functions.sh
grep -Fq '"/$elf" --help' tests/m187-util-linux-run.sh
grep -Fq '"/$elf" --help' tests/m174-software-run.sh
if grep -Eq '(^|[[:space:]])(busy|/bin/busybox)[[:space:]]+timeout[[:space:]]+[0-9]+[[:space:]]+"/\$elf"' \
        tests/m174-software-run.sh tests/m187-util-linux-run.sh; then
    echo '[M187 SOFTWARE MANIFEST FAIL] 46 entry probes spawn duplicate timeout watchdogs' >&2
    exit 1
fi
grep -Fq '[EXEC] replaced process with CPIO ELF: /$elf' scripts/probe-m174-batch.sh
echo '[M187 SOFTWARE MANIFEST PASS] +26 historically new util-linux-misc ELF paths; original M174 20/13 unchanged'

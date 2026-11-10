#!/bin/sh
# Independent M188 +2 manifest. Preserve M174 20/13 and M187 +26 gates.
set -eu
manifest=scripts/m188-original-elfs.txt
packages=rootfs/alpine/packages-m174-software-batch.txt
copies=scripts/m174-software-rootfs-copies.tsv
test -s "$manifest" && test -s "$packages" && test -s "$copies"
sh scripts/m174-software-check-manifest.sh
sh scripts/m187-software-check-manifest.sh
for package in vim nano mg rdfind; do
    grep -Fxq "$package" "$packages" || {
        echo "[M188 SOFTWARE MANIFEST FAIL] missing Alpine v3.23 package $package" >&2
        exit 1
    }
done
# Vim/Nano were each already counted once as signed original ELFs in M79/M80.
grep -Fxq usr/bin/vim scripts/m79-original-elfs.txt
grep -Fxq usr/bin/nano scripts/m80-original-elfs.txt
if grep -Eq '^(usr/bin/vim|usr/bin/nano)$' "$manifest"; then
    echo '[M188 SOFTWARE MANIFEST FAIL] duplicate historical Vim/Nano' >&2
    exit 1
fi
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$elf" in usr/bin/mg|usr/bin/rdfind) ;;
        *) echo "[M188 SOFTWARE MANIFEST FAIL] non-approved new ELF $elf" >&2; exit 1 ;;
    esac
    case "$seen" in *" $elf "*)
        echo "[M188 SOFTWARE MANIFEST FAIL] duplicate ELF $elf" >&2; exit 1 ;;
    esac
    seen="$seen$elf "
    for previous in scripts/m[0-9]*-original-elfs.txt \
                    scripts/m[0-9]*-software-original-elfs.txt; do
        [ -f "$previous" ] || continue
        [ "$previous" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$previous"; then
            echo "[M188 SOFTWARE MANIFEST FAIL] historical duplicate $elf in $previous" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 2 ] && grep -Fxq usr/bin/mg "$manifest" && \
    grep -Fxq usr/bin/rdfind "$manifest" || {
    echo '[M188 SOFTWARE MANIFEST FAIL] expected exactly mg and rdfind' >&2
    exit 1
}
tab=$(printf '\t')
grep -Fxq "tests/m188-editor-check.sh${tab}tests/m188-editor-check.sh" "$copies" || {
    echo '[M188 SOFTWARE MANIFEST FAIL] live Vim verifier not in same CPIO' >&2
    exit 1
}
grep -Fxq "tests/m188-software-entry.sh${tab}tests/m188-software-entry.sh" "$copies" || {
    echo '[M188 SOFTWARE MANIFEST FAIL] signed-entry script not in same CPIO' >&2
    exit 1
}
grep -Fxq '/bin/sh /tests/m188-software-entry.sh' tests/m174-software-run.sh || {
    echo '[M188 SOFTWARE MANIFEST FAIL] guest signed-entry script not called' >&2
    exit 1
}
grep -Fq 'scripts/m188-original-elfs.txt' scripts/host-configure-m174-software-batch.sh || exit 1
sh -n tests/m188-editor-check.sh
sh -n tests/m188-software-entry.sh
grep -Fxq 'echo M188_VIM_EDITED_CONTENT_VERIFIED' tests/m188-editor-check.sh || exit 1
echo '[M188 SOFTWARE MANIFEST PASS] M79 Vim and M80 Nano reused; +2 new mg/rdfind; historical M174 20 and M187 26 unchanged'

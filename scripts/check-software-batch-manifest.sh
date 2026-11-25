#!/bin/sh
# M174/M187/M188 share one signed CPIO. Keep their independent admission counts.
set -eu
[ "$#" -eq 1 ] || exit 2
profile=$1
packages=rootfs/alpine/packages-m174-software-batch.txt
copies=scripts/m174-software-rootfs-copies.tsv
case "$profile" in
    m174) manifest=scripts/m174-software-original-elfs.txt; count_expected=20
        required='fbgrab imagemagick evtest libevdev-tools dialog links w3m lynx mc dbus inotify-tools sysstat tig'
        fixtures='tests/m167-desktop-fixture.ppm tests/m168-home.html tests/m174-software-run.sh' ;;
    m187) manifest=scripts/m187-original-elfs.txt; count_expected=26
        required='util-linux-misc ncurses-terminfo-base'
        fixtures='tests/m187-util-linux-run.sh tests/m187-software-functions.sh' ;;
    m188) manifest=scripts/m188-original-elfs.txt; count_expected=2
        required='vim nano mg rdfind'
        fixtures='tests/m188-editor-check.sh tests/m188-software-entry.sh'
        sh "$0" m174; sh "$0" m187 ;;
    *) exit 2 ;;
esac
label=$(printf '%s' "$profile" | tr '[:lower:]' '[:upper:]')
fail() { echo "[$label SOFTWARE MANIFEST FAIL] $*" >&2; exit 1; }
test -s "$manifest" && test -s "$packages" && test -s "$copies" || fail 'missing manifest, packages or copies'
for package in $required; do
    grep -Fxq "$package" "$packages" || fail "package missing: $package"
done
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$profile:$elf" in
        m174:usr/bin/*|m174:bin/*|m187:bin/*|m187:sbin/*|m187:usr/bin/*|m187:usr/sbin/*|m188:usr/bin/mg|m188:usr/bin/rdfind) ;;
        *) fail "invalid candidate path: $elf" ;;
    esac
    case "$seen" in *" $elf "*) fail "repeated path: $elf" ;; esac
    seen="$seen$elf "
    for previous in scripts/m[0-9]*-original-elfs.txt scripts/m[0-9]*-software-original-elfs.txt; do
        [ -f "$previous" ] || continue
        [ "$previous" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$previous"; then fail "historical duplicate: $elf in $previous"; fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq "$count_expected" ] || fail "candidate paths $count != $count_expected"
tab=$(printf '\t')
for fixture in $fixtures; do
    grep -Fxq "$fixture$tab$fixture" "$copies" || fail "fixture mapping missing: $fixture"
    case "$fixture" in *.sh) sh -n "$fixture" ;; esac
done
case "$profile" in
    m174) echo '[M174 SOFTWARE MANIFEST PASS] 20 historically new candidate paths, 13 signed APK packages, M168 assets preserved' ;;
    m187)
        grep -Fq 'scripts/m187-original-elfs.txt' scripts/host-configure-m174-software-batch.sh
        grep -Fq '"/$elf" --help' tests/m187-util-linux-run.sh
        grep -Fq '"/$elf" --help' tests/m174-software-run.sh
        if grep -Eq '(^|[[:space:]])(busy|/bin/busybox)[[:space:]]+timeout[[:space:]]+[0-9]+[[:space:]]+"/\$elf"' tests/m174-software-run.sh tests/m187-util-linux-run.sh; then
            fail '46 entry probes spawn duplicate timeout watchdogs'
        fi
        grep -Fq '[EXEC] replaced process with CPIO ELF: /$elf' scripts/probe-m174-batch.sh
        echo '[M187 SOFTWARE MANIFEST PASS] +26 historically new util-linux-misc ELF paths; original M174 20/13 unchanged' ;;
    m188)
        grep -Fxq usr/bin/vim scripts/m79-original-elfs.txt
        grep -Fxq usr/bin/nano scripts/m80-original-elfs.txt
        grep -Fxq usr/bin/mg "$manifest" && grep -Fxq usr/bin/rdfind "$manifest" || fail 'expected exactly mg and rdfind'
        grep -Fxq '/bin/sh /tests/m188-software-entry.sh' tests/m174-software-run.sh
        grep -Fq 'scripts/m188-original-elfs.txt' scripts/host-configure-m174-software-batch.sh
        grep -Fxq 'echo M188_VIM_EDITED_CONTENT_VERIFIED' tests/m188-editor-check.sh
        echo '[M188 SOFTWARE MANIFEST PASS] M79 Vim and M80 Nano reused; +2 new mg/rdfind; historical M174 20 and M187 26 unchanged' ;;
esac

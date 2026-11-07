#!/bin/sh
# Static gate: historical names, signed profile packages and desktop fixtures.
set -eu
manifest=scripts/m174-software-original-elfs.txt
packages=rootfs/alpine/packages-m174-software-batch.txt
copies=scripts/m174-software-rootfs-copies.tsv
test -s "$manifest" && test -s "$packages" && test -s "$copies"

count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$elf" in
        # Alpine 3.23 deliberately has not completed /usr-merge. Original
        # sysstat installs iostat/mpstat in /bin, not /usr/bin.
        usr/bin/*|bin/*) ;;
        *) echo "[M174 SOFTWARE INVENTORY FAIL] noncanonical path: $elf" >&2; exit 1 ;;
    esac
    case "$seen" in
        *" $elf "*) echo "[M174 SOFTWARE INVENTORY FAIL] repeated path: $elf" >&2; exit 1 ;;
    esac
    seen="$seen$elf "
    for previous in scripts/m[0-9]*-original-elfs.txt \
                    scripts/m[0-9]*-software-original-elfs.txt; do
        [ -f "$previous" ] || continue
        [ "$previous" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$previous"; then
            echo "[M174 SOFTWARE INVENTORY FAIL] historical duplicate: $elf in $previous" >&2
            exit 1
        fi
    done
    count=$((count+1))
done < "$manifest"
[ "$count" -eq 20 ] || {
    echo "[M174 SOFTWARE INVENTORY FAIL] candidate paths $count != 20" >&2
    exit 1
}
for package in fbgrab imagemagick evtest libevdev-tools dialog links w3m \
               lynx mc dbus inotify-tools sysstat tig; do
    grep -Fxq "$package" "$packages" || {
        echo "[M174 SOFTWARE INVENTORY FAIL] package missing: $package" >&2
        exit 1
    }
done
tab=$(printf '\t')
for fixture in tests/m167-desktop-fixture.ppm tests/m168-home.html \
               tests/m174-software-run.sh; do
    grep -Fxq "$fixture$tab$fixture" "$copies" || {
        echo "[M174 SOFTWARE INVENTORY FAIL] fixture mapping missing: $fixture" >&2
        exit 1
    }
done
sh -n tests/m174-software-run.sh
echo '[M174 SOFTWARE MANIFEST PASS] 20 historically new candidate paths, 13 signed APK packages, M168 assets preserved'

#!/bin/sh
# Signed TLS, image and desktop overlays. Original application proofs remain
# separate; this owns only the common staging and binary admission policy.
set -eu
[ "$#" -eq 2 ] || exit 2
port=$1 profile=$2
case "$port" in
    amd64) machine='Advanced Micro Devices X86-64' ;;
    riscv64) machine='RISC-V' ;;
    arm64) machine='AArch64' ;;
    *) exit 2 ;;
esac
case "$profile" in
    m156-https) ;;
    m160-batch|m161-batch|m162-batch|m164-batch|m166-batch|m167-batch|m168-batch) ;;
    *) exit 2 ;;
esac
milestone=${profile%%-*}
label=$(printf '%s' "$milestone" | tr m M)
case "$milestone" in
    m160)
        manifest=scripts/m160-reused-elfs.txt
        count=0
        while IFS= read -r elf; do
            case "$elf" in ''|'#'*) continue ;; esac
            found=0
            for old in scripts/m[0-9]*-original-elfs.txt; do
                if grep -Fxq "$elf" "$old"; then found=$((found + 1)); fi
            done
            [ "$found" -gt 0 ] || { echo "[M160 INVENTORY FAIL] not historically admitted: $elf" >&2; exit 1; }
            count=$((count + 1))
        done < "$manifest"
        [ "$count" -eq 6 ] ;;
    m161)
        grep -Fxq 'usr/bin/fbgrab' scripts/m161-original-elfs.txt
        grep -Fxq 'usr/bin/magick' scripts/m146-original-elfs.txt
        for old in scripts/m[0-9]*-original-elfs.txt; do
            [ "$old" = scripts/m161-original-elfs.txt ] && continue
            if grep -Fxq 'usr/bin/fbgrab' "$old"; then
                echo "[M161 STAGE FAIL] fbgrab was previously admitted in $old" >&2; exit 1
            fi
        done ;;
    m166|m167|m168)
        manifest="scripts/$milestone-original-elfs.txt"
        case "$milestone" in
            m166) expected=4; distinct=--distinct ;;
            m167) expected=3; distinct=--distinct ;;
            m168) expected=2; distinct= ;;
        esac
        count=$(sh scripts/acceptance/check-elf-history.sh "$label" "$manifest" --count $distinct)
        [ "$count" -eq "$expected" ] ;;
esac
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" "$profile"
work="build/external/$profile/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio"
if [ "$milestone" = m156 ]; then
    test -s "$root/tests/m156-https-ca.pem"
    test -s "$work/host-ca.crt"
    echo "[M156 STAGED] $port existing signed curl/wget, independent TLS CA oracle; guest proof pending"
    exit 0
fi
# All of these profiles require the original package database.
test -s "$root/lib/apk/db/installed"
case "$milestone" in
    m160) required='curl wget netpbm chafa'; fixtures='tests/m160-image.sha256 tests/m156-https-ca.pem tests/m160-batch-run.sh' ;;
    m161) required='fbgrab imagemagick'; fixtures='tests/m161-stripes.ppm tests/m161-batch-run.sh' ;;
    m162) required='curl wget netpbm chafa imagemagick fbgrab'; fixtures='tests/m162-batch-run.sh tests/m162-wire-image.sha256 tests/m156-https-ca.pem' ;;
    m164) required='fbgrab imagemagick'; fixtures='tests/m164-input-display tests/m164-input-display.c' ;;
    m166) required='evtest libevdev-tools fbgrab imagemagick'; fixtures='tests/m166-input-display.c'; test -x "$root/tests/m166-input-display" ;;
    m167) required='evtest libevdev-tools fbgrab imagemagick dialog links w3m'; fixtures='tests/m167-desktop-fixture.ppm' ;;
    m168) required='lynx mc dialog links w3m imagemagick evtest libevdev-tools fbgrab'; fixtures='tests/m167-desktop-fixture.ppm tests/m168-home.html' ;;
esac
for package in $required; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed" || { echo "[$label STAGE FAIL] missing signed APK $port $package" >&2; exit 1; }
done
for fixture in $fixtures; do
    test -s "$root/$fixture" || { echo "[$label STAGE FAIL] missing guest fixture $fixture" >&2; exit 1; }
done
case "$milestone" in
    m160|m162)
        case "$milestone" in
            m160) test -s "$work/host-m160-image.png"; test -s "$work/host-ca.crt"; test -s "$work/host-tls.crt" ;;
            m162) test -s "$work/host-m162-display.png"; test ! -e "$root/host-m162-display.png" ;;
        esac
        test -s "$work/host-ca.key"; test -s "$work/host-tls.key"
        test ! -e "$root/host-ca.key"; test ! -e "$root/host-tls.key" ;;
    m167|m168)
        apps='naseed-desktop naseed-image naseed-terminal'
        [ "$milestone" != m168 ] || apps="$apps naseed-browser"
        for app in $apps; do
            test -x "$root/usr/bin/$app" || { echo "[$label STAGE FAIL] missing separately executable $app" >&2; exit 1; }
            readelf -h "$root/usr/bin/$app" | grep -Fq "$machine" || exit 1
            if readelf -l "$root/usr/bin/$app" | grep -Fq INTERP; then
                echo "[$label STAGE FAIL] $app unexpectedly dynamic" >&2; exit 1
            fi
        done ;;
esac
# Retain the digest filenames consumed by the native proof runners.
output="$work/$milestone-original-signed-elfs.sha256"
case "$milestone" in
    m160) sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$output" 6 contained-exec-elf "$label" ;;
    m161|m164)
        [ "$milestone" != m161 ] || output="$work/m161-signed-original-elfs.sha256"
        # fbgrab + already admitted magick; the latter is not a new ELF identity.
        printf '%s\n' usr/bin/fbgrab usr/bin/magick | sh scripts/acceptance/hash-original-elfs.sh "$root" /dev/stdin "$output" 2 contained-exec-elf "$label" ;;
    m162) sh scripts/acceptance/hash-original-elfs.sh "$root" scripts/m162-reused-elfs.txt "$output" 7 contained-elf "$label" "$machine" ;;
    m166) sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$output" 4 contained-exec-elf "$label" ;;
    m167|m168) sh scripts/acceptance/hash-original-elfs.sh "$root" "$manifest" "$output" "$expected" contained-exec-elf "$label" "$machine" ;;
esac
case "$milestone" in
    m160) echo "[M160 APK STAGE PASS] $port six already-counted native signed ELF clients and image consumers" ;;
    m161) echo "[M161 SIGNED STAGE PASS] $port original fbgrab + ImageMagick" ;;
    m162) echo "[M162 SIGNED STAGE PASS] $port 7 authentic existing ELFs, host-only PNG and keys" ;;
    m164) echo "[M164 SIGNED STAGE PASS] $port original Alpine framebuffer tools + separate native C app" ;;
    m166) echo "[M166 SIGNED STAGE PASS] $port four new independent original Alpine evdev ELFs" ;;
    m167) echo "[M167 SIGNED STAGE PASS] $port three new independent original Alpine terminal ELFs" ;;
    m168) echo "[M168 STAGE PASS] $port two new original signed Alpine browser/filemanager ELFs" ;;
esac

#!/bin/sh
# Stage four NEW signed Alpine ELF programs alongside the genuine evdev test
# executable.  The test app is compiled separately and is never counted as
# an original package binary.  No guest compilation or APK scripts execute.
set -eu
[ "$#" -eq 1 ] || exit 2
port=$1
case "$port" in amd64|riscv64|arm64) ;; *) exit 2 ;; esac
manifest=scripts/m166-original-elfs.txt
count=0
seen=' '
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    case "$seen" in *" $elf "*)
        echo "[M166 STAGE FAIL] repeated ELF $elf" >&2; exit 1 ;;
    esac
    seen="$seen$elf "
    for old in scripts/m[0-9]*-original-elfs.txt; do
        [ "$old" = "$manifest" ] && continue
        if grep -Fxq "$elf" "$old"; then
            echo "[M166 STAGE FAIL] previously counted $elf" >&2
            exit 1
        fi
    done
    count=$((count + 1))
done < "$manifest"
[ "$count" -eq 4 ] || exit 1
sh scripts/stage-alpine-prebuilt-overlay.sh "$port" m166-batch
work="build/external/m166-batch/$port"
root="$work/rootfs"
test -s "$work/rootfs.cpio" && test -x "$root/tests/m166-input-display"
test -s "$root/tests/m166-input-display.c"
for package in evtest libevdev-tools fbgrab imagemagick; do
    grep -Fxq "P:$package" "$root/lib/apk/db/installed"
done
canonical=$(cd "$root" && pwd -P)
while IFS= read -r elf; do
    case "$elf" in ''|'#'*) continue ;; esac
    resolved=$(readlink -f "$root/$elf") || exit 1
    case "$resolved" in "$canonical"/*) ;; *) exit 1 ;; esac
    test -x "$resolved" || exit 1
    # Executable ELF only, never a BusyBox multicall alias, text script,
    # copied compiler output, or a host-supplied unsigned fixture.
    case "$resolved" in "$canonical"/bin/busybox|"$canonical"/usr/bin/busybox)
        exit 1 ;;
    esac
    [ "$(od -An -tx1 -N4 "$resolved" | tr -d ' \n')" = 7f454c46 ] || exit 1
    sha256sum "$resolved"
done < "$manifest" > "$work/m166-original-signed-elfs.sha256"
unique=$(awk '{print $1}' "$work/m166-original-signed-elfs.sha256" | sort -u | wc -l)
[ "$unique" -eq 4 ] || exit 1
echo "[M166 SIGNED STAGE PASS] $port four new independent original Alpine evdev ELFs"

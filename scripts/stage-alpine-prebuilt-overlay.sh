#!/bin/sh
# Shared host-only original signed APK overlay. No compiler or installation
# script is executed inside NaSeed guests. One staging owner for M58+.
set -eu
[ "$#" -eq 2 ] || [ "$#" -eq 5 ] || {
    echo 'usage: stage-alpine-prebuilt-overlay.sh PORT PROFILE [PKGS ELFS COPIES]' >&2
    exit 2
}
port=$1
profile=$2
case "$profile" in
    ''|*[!a-z0-9-]*) echo "[ALPINE OVERLAY FAIL] unsafe profile: $profile" >&2; exit 2 ;;
esac

# Profile identity, input manifests and repository selection have one owner.
# Keep the five-argument interface for external legacy callers, but only if
# their inputs exactly match the authoritative catalog row.
catalog=scripts/prebuilt-profile-catalog.tsv
test -s "$catalog" || exit 2
entry="$(awk -F '\t' -v profile="$profile" '
    /^#/ || $1 == "profile" || NF == 0 { next }
    $1 == profile {
        found++
        if (NF != 5 || $2 == "" || $3 == "" || $4 == "" || $5 == "") bad = 1
        print $2 "\t" $3 "\t" $4 "\t" $5
    }
    END { if (found != 1 || bad) exit 2 }
' "$catalog")" || {
    echo "[ALPINE OVERLAY FAIL] unknown or duplicate profile: $profile" >&2
    exit 2
}
tab="$(printf '\t')"
IFS="$tab" read -r package_file elf_file copies_file repositories <<EOF
$entry
EOF
for manifest in "$package_file" "$elf_file" "$copies_file"; do
    case "$manifest" in
        ''|/*|../*|*/../*|*/..|*[!a-zA-Z0-9_./-]*)
            echo "[ALPINE OVERLAY FAIL] unsafe manifest path: $manifest" >&2
            exit 2 ;;
    esac
done
case "$package_file" in rootfs/alpine/packages-*.txt) ;; *) exit 2 ;; esac
case "$elf_file" in scripts/m*-original-elfs.txt) ;; *) exit 2 ;; esac
case "$copies_file" in scripts/m*-rootfs-copies.tsv|tests/*) ;; *) exit 2 ;; esac
case "$repositories" in main|main+community) ;; *) exit 2 ;; esac
if [ "$#" -eq 5 ]; then
    legacy_copies=$copies_file
    case "$copies_file" in
        tests/*)
            case "$profile" in
                m156-https) legacy_copies=scripts/m156-https-rootfs-copies.tsv ;;
                *) legacy_copies="scripts/${profile%%-*}-rootfs-copies.tsv" ;;
            esac ;;
    esac
    if [ "$3" != "$package_file" ] || [ "$4" != "$elf_file" ] ||
       { [ "$5" != "$copies_file" ] && [ "$5" != "$legacy_copies" ]; }; then
        echo "[ALPINE OVERLAY FAIL] legacy manifests disagree with $profile catalog" >&2
        exit 2
    fi
fi
case "$port" in
    amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) arch=riscv64; machine='RISC-V' ;;
    arm64) arch=aarch64; machine='AArch64' ;;
    *) exit 2 ;;
esac
for manifest in "$package_file" "$elf_file"; do
    test -s "$manifest" || exit 1
done
case "$copies_file" in
    tests/*) test -f "$copies_file" || exit 1 ;;
    *) test -s "$copies_file" || exit 1 ;;
esac

NAS_ALPINE_PROFILE=runtime sh rootfs/alpine/build.sh "$port"
base="build/external/m39-packages/$port"
work="build/external/$profile/$port"
mkdir -p "$work"
cp -a "$base/rootfs" "$work/rootfs"
root="$work/rootfs"
root_canonical=$(cd "$root" && pwd -P)
# Every Linux rootfs requires the parent device directory even when its
# device nodes are supplied virtually by the kernel, not host mknod.
mkdir -p "$root/dev"
target="$(pwd)/$root"
packages="$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$package_file")"
test -n "$packages"
if ! docker run --rm --volume "$target:/target" \
    --env APK_ARCH="$arch" --env APK_PACKAGES="$packages" \
    --env APK_REPOSITORIES="$repositories" \
    alpine:3.23 sh -ec '
        cp -a /target/etc/apk/keys/. /etc/apk/keys/
        set -- apk --arch "$APK_ARCH" --root /target --keys-dir /etc/apk/keys \
            --no-scripts --quiet \
            -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main
        case "$APK_REPOSITORIES" in
            main) ;;
            main+community)
                set -- "$@" -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community ;;
            *) exit 2 ;;
        esac
        "$@" add $APK_PACKAGES
    ' > "$work/apk-install.log" 2>&1; then
    echo "[ALPINE OVERLAY FAIL] $profile $port" >&2
    tail -90 "$work/apk-install.log" >&2
    exit 1
fi
sudo chown -R "$(id -u):$(id -g)" "$root"

# Some signed packages deliberately defer generated *metadata* (not ELF
# binaries) to Alpine install scripts.  Our APK import intentionally uses
# --no-scripts, so allow an explicitly named, profile-local HOST hook before
# the one canonical rootfs archive is produced.  No code runs in NaSeed.
case "$profile" in
    m119-batch|m12[0-8]-batch|m143-batch|m144-batch|m147-batch|m149-batch|m152-batch|m163-batch|m165-batch|m16[6-8]-batch)
        sh scripts/host-configure-alpine-fixtures.sh "$port" "$root" "${profile%-batch}" ;;
    *)
        host_hook="scripts/host-configure-$profile.sh"
        if test -f "$host_hook"; then
            sh "$host_hook" "$port" "$root"
        fi ;;
esac

while IFS= read -r program; do
    case "$program" in ''|'#'*) continue ;; esac
    case "$program" in /*|*../*|*/../*|*' '*) exit 2 ;; esac
    binary="$root/$program"
    resolved=$(readlink -f "$binary") || {
        echo "[ALPINE OVERLAY FAIL] unresolvable $port ELF $program" >&2
        exit 1
    }
    # readlink -f follows absolute symlinks on the HOST. Never authorize a
    # host binary as an APK-installed native guest executable by accident.
    case "$resolved" in "$root_canonical"/*) ;;
        *) echo "[ALPINE OVERLAY FAIL] $port ELF escapes signed root: $program -> $resolved" >&2
           exit 1 ;;
    esac
    test -f "$resolved" && test -x "$resolved" || {
        echo "[ALPINE OVERLAY FAIL] missing $port ELF $program" >&2
        exit 1
    }
    readelf -h "$resolved" | grep -Fq "$machine" || {
        echo "[ALPINE OVERLAY FAIL] wrong ISA $port ELF $program" >&2
        exit 1
    }
done < "$elf_file"

copy_fixture() {
    case "$2" in tests/*) ;; *) exit 2 ;; esac
    case "$2" in *../*|*/../*|*' '*) exit 2 ;; esac
    test -f "$1" || exit 1
    cp "$1" "$root/$2"
}
case "$copies_file" in
    tests/*) copy_fixture "$copies_file" "$copies_file" ;;
    *)
        while IFS="$tab" read -r source destination; do
            case "$source" in ''|'#'*) continue ;; esac
            copy_fixture "$source" "$destination"
        done < "$copies_file" ;;
esac

# Shared guest identity check used by the original software workload family.
case "$profile" in
    m10[9]-batch|m11[0-9]-batch|m12[0-9]-batch|m13[0-9]-batch|m140-batch|m146-batch|m148-batch|m150-batch|m153-batch|m154-batch)
        copy_fixture tests/original-elf-common.sh tests/original-elf-common.sh ;;
esac

(
    cd "$root"
    find . \( -type f -o -type d -o -type l \) -print |
        LC_ALL=C sort |
        while IFS= read -r path; do
            [ "${#path}" -le 254 ] || {
                echo "[ALPINE OVERLAY FAIL] CPIO path too long: $path" >&2
                exit 1
            }
            printf '%s\n' "$path"
        done | cpio -o -H newc --quiet --owner=0:0
) > "$work/rootfs.cpio.tmp"
mv "$work/rootfs.cpio.tmp" "$work/rootfs.cpio"
test -s "$work/rootfs.cpio"
printf '%s\n' "$packages" > "$work/requested-packages.txt"
echo "[ALPINE OVERLAY PASS] $port $profile original signed target ELF, host-installed"

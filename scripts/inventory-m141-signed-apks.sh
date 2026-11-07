#!/bin/sh
# Host-only candidate census; this does NOT run or accept guest programs.
# Usage: sh scripts/inventory-m141-signed-apks.sh amd64|riscv64|arm64 [m141|m205|m206|m213|m215|m218|m219|m220]
# Download official signed Alpine 3.23 APKs once and classify candidate paths.
set -eu

[ "$#" -ge 1 ] && [ "$#" -le 2 ] || exit 2
port=$1
profile=${2:-m141}
case "$profile" in
    m141|m205|m206|m213|m215|m218|m219|m220) ;;
    *) echo '[CANDIDATE ERROR] unknown pool profile' >&2; exit 2 ;;
esac
case "$port" in
    amd64) arch=x86_64; machine='Advanced Micro Devices X86-64' ;;
    riscv64) arch=riscv64; machine='RISC-V' ;;
    arm64) arch=aarch64; machine='AArch64' ;;
    *) echo '[M141 CANDIDATE ERROR] unknown ISA' >&2; exit 2 ;;
esac

python3 scripts/check-m141-candidates.py --profile "$profile"
command -v docker >/dev/null || { echo '[CANDIDATE ERROR] docker required' >&2; exit 2; }
command -v readelf >/dev/null || { echo '[CANDIDATE ERROR] readelf required' >&2; exit 2; }

manifest="scripts/$profile-candidate-elfs.tsv"
packages="rootfs/alpine/packages-$profile-candidates.txt"
work="build/external/$profile-candidate-inventory/$port"
package_sha=$(sha256sum "$packages" | awk '{print $1}')
root="$work/packages-$package_sha/rootfs"
# Unlike the normal overlay, this discovery root starts empty. apk --root
# expects a writable package database directory before its first install.
mkdir -p "$root/lib/apk/db" "$root/etc/apk"
root_canonical=$(cd "$root" && pwd -P)
package_list=$(sed -e 's/#.*//' -e '/^[[:space:]]*$/d' "$packages")
test -n "$package_list"

# M205's guest otherwise downloads the same large original Alpine APKs a
# second time over the emulated network. Retain precisely the packages that
# the HOST already obtains from Alpine's signed repository/index closure.
# APK v3.0 (Alpine 3.23) supports --cache-predownload and recognizes this
# conventional ext4-backed /etc/apk/cache symlink. Never use --allow-untrusted.
cache_flags=
if [ "$profile" = m205 ] || [ "$profile" = m206 ] || [ "$profile" = m213 ] || [ "$profile" = m215 ] || [ "$profile" = m218 ] || [ "$profile" = m219 ] || [ "$profile" = m220 ]; then
    # The guest's genuine PTY/metadata regression also installs the signed
    # util-linux-misc helper. It is not one of the 20 candidate entrypoints,
    # but must be present in the offline package closure.
    if [ "$profile" = m205 ]; then
        package_list="$package_list
util-linux-misc"
    fi
    cache_flags='--cache-dir /var/cache/apk --cache-packages --cache-predownload'
fi

# Reuse the canonical Alpine runtime builder, which SHA256-verifies the
# target-ISA minirootfs and extracts its architecture-specific signing keys.
# The amd64 Docker image's own keys do NOT verify riscv64/aarch64 indexes.
# Keeping one key owner is essential: never turn off APK signature checks.
if [ ! -f "$work/packages-$package_sha/signed-install.ok" ]; then
    NAS_ALPINE_PROFILE=runtime sh rootfs/alpine/build.sh "$port"
    cp -a "build/external/m39-packages/$port/rootfs/." "$root/"
    if [ "$profile" = m205 ] || [ "$profile" = m206 ] || [ "$profile" = m213 ] || [ "$profile" = m215 ] || [ "$profile" = m218 ] || [ "$profile" = m219 ] || [ "$profile" = m220 ]; then
        # The cached M39 minirootfs was copied only NOW. Configure the cache
        # after copying it, so no base-root entry can replace the link.
        mkdir -p "$root/var/cache/apk"
        if [ ! -L "$root/etc/apk/cache" ]; then
            [ ! -e "$root/etc/apk/cache" ] || {
                echo '[M205 CACHE FAIL] APK cache path was not a symlink' >&2
                exit 1
            }
            ln -s /var/cache/apk "$root/etc/apk/cache"
        fi
        [ "$(readlink "$root/etc/apk/cache")" = /var/cache/apk ] || {
            echo '[M205 CACHE FAIL] unexpected APK cache symlink target' >&2
            exit 1
        }
    fi
    if ! docker run --rm --volume "$root_canonical:/target" \
        --env APK_ARCH="$arch" --env APK_PACKAGES="$package_list" \
        --env APK_CACHE_FLAGS="$cache_flags" \
        alpine:3.23 sh -ec '
            cp -a /target/etc/apk/keys/. /etc/apk/keys/
            apk --arch "$APK_ARCH" --root /target --initdb \
                --keys-dir /etc/apk/keys \
                $APK_CACHE_FLAGS \
                --no-scripts \
                -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
                -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
                add $APK_PACKAGES
        ' > "$work/apk-install.log" 2>&1; then
        echo "[$profile CANDIDATE APK FAIL] $port" >&2
        tail -100 "$work/apk-install.log" >&2
        exit 1
    fi
    touch "$work/packages-$package_sha/signed-install.ok"
fi

if [ "$profile" = m205 ] || [ "$profile" = m206 ] || [ "$profile" = m213 ] || [ "$profile" = m215 ] || [ "$profile" = m218 ] || [ "$profile" = m219 ] || [ "$profile" = m220 ]; then
    # The guest M191 disk already pins BOTH official repositories. Mirror
    # that exact configuration in this host-only cross-ISA root as well:
    # apk-tools 3's index cache update follows persistent repository config,
    # while transient -X flags alone need not leave two reusable indexes.
    printf '%s\n' \
        'https://dl-cdn.alpinelinux.org/alpine/v3.23/main' \
        'https://dl-cdn.alpinelinux.org/alpine/v3.23/community' \
        > "$root/etc/apk/repositories"
    cache="$root/var/cache/apk"
    # Alpine apk-tools 3 may finish a --root installation without writing
    # package archives into an implicitly selected cache. Explicitly fetch
    # the *official repository closure* into the native cache directory and
    # refresh both signed repository indexes there. The guest still checks
    # signatures and installs everything itself; no binary is substituted.
    # This also repairs a previous interrupted host census whose signed
    # install marker exists but whose archive cache was incomplete.
    # Only a checksum-suffixed filename is a valid apk-tools package cache
    # entry. Plain output from `apk fetch` is not usable by --no-network.
    canonical_count=$(find "$cache" -maxdepth 1 -type f -name '*.apk' -printf '%f\n' |
        grep -Ec '\.[0-9a-f]{8}\.apk$' || :)
    index_count=$(find "$cache" -maxdepth 1 -type f -name 'APKINDEX*' | wc -l)
    min_archives=16
    { [ "$profile" = m215 ] || [ "$profile" = m218 ] || [ "$profile" = m219 ] || [ "$profile" = m220 ]; } && min_archives=2
    # A two-package pilot can exceed its archive-count floor even if a
    # previous interrupted fetch omitted one transitive dependency. Until
    # an independent empty-root offline install passed, refresh the complete
    # signed closure rather than repeatedly failing from a partial cache.
    m215_verified="$work/packages-$package_sha/offline-verified.ok"
    if [ "$canonical_count" -lt "$min_archives" ] || [ "$index_count" -lt 2 ] || \
       { { [ "$profile" = m215 ] || [ "$profile" = m218 ] || [ "$profile" = m219 ] || [ "$profile" = m220 ]; } && [ ! -f "$m215_verified" ]; }; then
        if ! docker run --rm --volume "$root_canonical:/target" \
            --volume "$root_canonical/var/cache/apk:/var/cache/apk" \
            --env APK_ARCH="$arch" --env APK_PACKAGES="$package_list" \
            alpine:3.23 sh -ec '
                cp -a /target/etc/apk/keys/. /etc/apk/keys/
                # For repository indexes, point the container apk client
                # DIRECTLY at the mounted output directory. The --root
                # cache selection left these indexes in a different root,
                # although its package fetch correctly wrote 76 archives.
                apk --arch "$APK_ARCH" \
                    --cache-dir /target/var/cache/apk \
                    --cache-max-age 0 --keys-dir /etc/apk/keys \
                    -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
                    -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
                    update
                # `apk fetch --output` creates name-version.apk; those are
                # NOT valid cache entries for --no-network add. apk cache
                # download writes the signed metadata-hash-suffixed names
                # which the native guest solver recognizes as cached packages.
                # Use the TARGET ISA database and signing keys, but bind
                # the process-local cache path to the guest ext4 source.
                # Without this second bind apk-tools 3 downloaded into the
                # ephemeral Docker cache; without --root the x86 Docker DB
                # could select host-ISA packages in RV64/ARM64 builds.
                apk --arch "$APK_ARCH" --root /target \
                    --cache-dir /var/cache/apk --keys-dir /etc/apk/keys \
                    -X https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
                    -X https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
                    cache download --available $APK_PACKAGES
                ls -la /target/var/cache/apk | head -n 15
            ' > "$work/apk-cache.log" 2>&1; then
            echo '[M205 CACHE FAIL] canonical signed index/archive cache download' >&2
            tail -80 "$work/apk-cache.log" >&2
            exit 1
        fi
    fi
    [ -d "$cache" ] && [ ! -L "$cache" ] || {
        echo '[M205 CACHE FAIL] missing real signed package cache directory' >&2
        exit 1
    }
    # Require the exact two official repository indexes and canonical hashed
    # package cache files; the independent install below also verifies their
    # actual transitive dependency closure, beyond a file-count check.
    archive_count=$(find "$cache" -maxdepth 1 -type f -name '*.apk' -printf '%f\n' |
        grep -Ec '\.[0-9a-f]{8}\.apk$' || :)
    index_count=$(find "$cache" -maxdepth 1 -type f -name 'APKINDEX*' | wc -l)
    [ "$archive_count" -ge "$min_archives" ] && [ "$index_count" -ge 2 ] || {
        echo "[M205 CACHE FAIL] canonical hashed APK/index shortage: $archive_count APKs $index_count indexes" >&2
        find "$cache" -maxdepth 1 -type f -name '*INDEX*' -print >&2
        if [ -f "$work/apk-cache.log" ]; then
            tail -25 "$work/apk-cache.log" >&2
        fi
        exit 1
    }
    [ -z "$(find "$cache" -maxdepth 1 -type l -print -quit)" ] || {
        echo '[M205 CACHE FAIL] cached archive path is a symlink' >&2
        exit 1
    }
    # A digest manifest is generated solely from fixed-depth regular files;
    # each path starts with ./ and is checked again after staging into ext4.
    cache_manifest="$PWD/$work/signed-cache.sha256"
    ( cd "$cache"
      find . -maxdepth 1 -type f -print0 | sort -z | xargs -0 sha256sum
    ) > "$cache_manifest"
    ( cd "$cache" && sha256sum -c "$cache_manifest" >/dev/null ) || {
        echo '[M205 CACHE FAIL] native archive/index hash mismatch' >&2
        exit 1
    }
    # The inventory root has ALREADY installed all packages; `apk add` there
    # would be a misleading no-op. Use an independent EMPTY target root and
    # install the real signed archives without network or target-ISA scripts.
    # Alpine apk-tools resolves --cache-dir differently across root modes.
    # Copy the signed cache to both potential lookup paths in this separate
    # ephemeral verifier; the original source is mounted read-only. apk
    # requires a writable LOCAL cache
    # even for --no-network transactions, so clone it into this disposable
    # Docker verifier, never modify the signed source or guest staging.
    if ! docker run --rm \
        --volume "$root_canonical:/target:ro" \
        --volume "$root_canonical/var/cache/apk:/original-cache:ro" \
        --env APK_ARCH="$arch" --env APK_PACKAGES="$package_list" \
        alpine:3.23 sh -ec '
            mkdir -p /verify/lib/apk/db /verify/etc/apk/keys \
                /verify/var/cache/apk /var/cache/apk
            cp -a /original-cache/. /verify/var/cache/apk/
            cp -a /original-cache/. /var/cache/apk/
            # A simulated transaction intentionally performs no --initdb
            # writes. Give this pristine verification root a genuine empty
            # installed/world database so the solver reaches repo metadata.
            : > /verify/lib/apk/db/installed
            : > /verify/etc/apk/world
            cp -a /target/etc/apk/keys/. /etc/apk/keys/
            cp -a /target/etc/apk/keys/. /verify/etc/apk/keys/
            ln -s /var/cache/apk /verify/etc/apk/cache
            printf "%s\n" \
                https://dl-cdn.alpinelinux.org/alpine/v3.23/main \
                https://dl-cdn.alpinelinux.org/alpine/v3.23/community \
                > /verify/etc/apk/repositories
            apk --arch "$APK_ARCH" --root /verify --initdb \
                --cache-dir /var/cache/apk --keys-dir /etc/apk/keys \
                --no-network --simulate add $APK_PACKAGES
            apk --arch "$APK_ARCH" --root /verify \
                --cache-dir /var/cache/apk --keys-dir /etc/apk/keys \
                --no-network --no-scripts add $APK_PACKAGES
        ' > "$work/apk-offline-proof.log" 2>&1; then
        echo '[M205 CACHE FAIL] independent empty-root APK offline resolution' >&2
        if [ -f "$work/apk-cache.log" ]; then
            tail -18 "$work/apk-cache.log" >&2
        fi
        tail -60 "$work/apk-offline-proof.log" >&2
        exit 1
    fi
    if [ "$profile" = m215 ] || [ "$profile" = m218 ] || [ "$profile" = m219 ] || [ "$profile" = m220 ]; then
        touch "$m215_verified"
    fi
    echo "[M205 SIGNED CACHE PASS] $port $archive_count upstream APK archives, $index_count retained indexes, SHA256 pinned; independent empty-root offline install PASS"
fi

report="$work/candidates.tsv"
printf 'package\telf_path\tstatus\tsha256\n' > "$report"
tab=$(printf '\t')
tail -n +2 "$manifest" | while IFS="$tab" read -r package path family probe; do
    case "$package" in ''|'#'*) continue ;; esac
    target=$(readlink -f "$root/$path" || true)
    status=MISSING
    sha=-
    case "$target" in
        "$root_canonical"/*)
            if [ -f "$target" ] && [ -x "$target" ]; then
                if readelf -h "$target" 2>/dev/null | grep -Fq "$machine"; then
                    sha=$(sha256sum "$target" | awk '{print $1}')
                    status=NATIVE_ELF
                elif readelf -h "$target" >/dev/null 2>&1; then
                    status=WRONG_ISA
                else
                    status=NOT_ELF
                fi
            fi ;;
        '') status=MISSING ;;
        *) status=ESCAPES_ROOT ;;
    esac
    printf '%s\t%s\t%s\t%s\n' "$package" "$path" "$status" "$sha" >> "$report"
done

native=$(awk -F '\t' '$3 == "NATIVE_ELF" { n++ } END { print n+0 }' "$report")
unique=$(awk -F '\t' '$3 == "NATIVE_ELF" { print $4 }' "$report" | sort -u | wc -l)
total=$(awk 'END {print NR-1}' "$report")
printf '[%s CANDIDATE INVENTORY] %s: %s/%s ELF paths, %s unique SHA\n' \
    "$profile" "$port" "$native" "$total" "$unique"
awk -F '\t' -v p="$profile" 'NR > 1 && $3 != "NATIVE_ELF" { print "[" p " FILTER] " $2 " " $3 }' "$report"
printf '[%s NOT ACCEPTED] native QEMU semantics and cross-ISA SHA gates pending\n' "$profile"
